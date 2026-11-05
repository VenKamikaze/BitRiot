#include "NetGame.h"

#include <cstring>
#include <sstream>
#include <thread>

#include "MickLogger.h"
#include "Simulation.h"
#include "vnet.h"

NetGame::~NetGame()
{
  m_match.reset();
  if (m_session)
  {
    m_session->leave();   // a no-op if it has ended; otherwise the others are told at once
    m_session->flushOutgoing();   // under VNET_IMPAIR the LEAVEs are still held back by the simulated latency
    m_session.reset();
  }
  if (m_v != nullptr)
  {
    vnet_destroy(m_v);
  }
}

int NetResolve::defaultResolver(const std::string& hostPort, vnet_addr* out)
{
  return vnet_resolve_host(hostPort.c_str(), VNET_FAMILY_ANY, out);
}

NetResolve::NetResolve(const std::string& hostPort, Resolver resolver) : m_state(std::make_shared<State>()), m_hostPort(hostPort)
{
  std::memset(&m_copy, 0, sizeof(m_copy));
  std::shared_ptr<State> state = m_state;
#ifdef __EMSCRIPTEN__
  // A browser has no threads here and no resolver (vnet_resolve_host answers VNET_ENOTSUP at once), so the lookup
  // is not worth a worker: it is finished before the constructor returns.
  {
    vnet_addr addr;
    std::memset(&addr, 0, sizeof(addr));
    const int rc = resolver(hostPort, &addr);
    state->rc = rc;
    state->addr = addr;
    state->done = true;
  }
#else
  std::thread([state, hostPort, resolver]() {
    vnet_addr addr;
    std::memset(&addr, 0, sizeof(addr));
    const int rc = resolver(hostPort, &addr);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->rc = rc;
    state->addr = addr;
    state->done = true;
  }).detach();
#endif
}

bool NetResolve::done() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return m_state->done;
}

int NetResolve::result() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return m_state->rc;
}

const vnet_addr& NetResolve::address() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  m_copy = m_state->addr;
  return m_copy;
}

namespace
{
void (*g_wakeHandler)() = nullptr;
void wakeTrampoline(void*)
{
  if (g_wakeHandler != nullptr)
  {
    g_wakeHandler();
  }
}
}

void NetGame::setWakeHandler(void (*handler)())
{
  g_wakeHandler = handler;
}

std::string NetGame::roomCode() const
{
  vnet_status_info st;
  if (m_v == nullptr || !m_params.relay || vnet_status(m_v, &st) != VNET_STATUS_READY)
  {
    return std::string();
  }
  return st.room;
}

std::string NetGame::transport() const
{
  vnet_status_info st;
  if (m_v == nullptr)
  {
    return std::string();
  }
  vnet_status(m_v, &st);
  return st.transport != nullptr ? st.transport : "";
}

int NetGame::transportState() const
{
  return m_v == nullptr ? VNET_STATUS_FAILED : vnet_status(m_v, nullptr);
}

int NetGame::relayRttMs() const
{
  vnet_status_info st;
  if (m_v == nullptr || vnet_status(m_v, &st) != VNET_STATUS_READY)
  {
    return -1;
  }
  return st.relay_rtt_ms;
}

std::unique_ptr<NetGame> NetGame::create(const Params& params, Simulation& sim, std::string* error)
{
  if (!params.host && !params.relay)
  {
    vnet_addr addr;
    const int rc = vnet_resolve_host(params.join.c_str(), VNET_FAMILY_ANY, &addr);
    if (rc != 0)
    {
      if (error) *error = "cannot find " + params.join + ": " + vnet_strerror(rc) + " (use host:port, for example 192.168.1.20:7777)";
      return nullptr;
    }
    return build(params, sim, &addr, error);
  }
  return build(params, sim, nullptr, error);
}

std::unique_ptr<NetGame> NetGame::createResolved(const Params& params, Simulation& sim, const vnet_addr& hostAddr, std::string* error)
{
  return build(params, sim, &hostAddr, error);
}

netplay::SessionOptions NetGame::sessionOptionsFor(const Params& params)
{
  netplay::SessionOptions so;
  so.buildId = Simulation::buildId();
  so.dataHash = Simulation::dataHash();
  so.name = netplay::cleanPlayerName(params.name);
  if (so.name.empty()) so.name = params.host ? "host" : "player";
  so.localPlayers = params.localPlayers;
  so.tickMs = std::GameSettings::NORMAL_TICKS_SPEED;
  so.requireReady = params.requireReady;
  so.reconnectToken = params.reconnectToken;
  so.spectate = params.spectate && !params.host;
  so.tolerateSparsePump = true;   // a hidden tab, a sleeping laptop or a debugger pause must not time the others out
  if (params.relay)
  {
    so.maxDelay = 12;   // two hops through the relay: a longer D than a LAN or a direct link needs
  }
  return so;
}

std::unique_ptr<NetGame> NetGame::build(const Params& params, Simulation& sim, const vnet_addr* resolved, std::string* error)
{
  std::unique_ptr<NetGame> g(new NetGame());
  g->m_params = params;
  g->m_sim = &sim;

  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_UDP;   // VNET_IMPAIR in the environment adds simulated latency and loss
  if (params.relay)
  {
    // One configuration for both worlds: a browser uses the WebSocket URL (and WebTransport when it works), a native
    // build the relay's UDP address.
    cfg.backend = VNET_BACKEND_RELAY_AUTO;
    cfg.relay = params.relayWs.empty() ? nullptr : params.relayWs.c_str();
    cfg.relay_udp = params.relayUdp.empty() ? nullptr : params.relayUdp.c_str();
    cfg.room = params.host ? nullptr : params.room.c_str();
    cfg.relay_wt = params.relayWt.empty() ? nullptr : params.relayWt.c_str();
    cfg.relay_cert_sha256 = params.relayCertSha256.empty() ? nullptr : params.relayCertSha256.c_str();
    cfg.relay_timeout_ms = 15000;   // the WebTransport attempt can take 3 s of it
    cfg.keepalive_ms = 2000;        // the relay drops a silent UDP member after 15 s; a lobby shows the relay's round trip
    cfg.wake = &wakeTrampoline;
  }
  if (!params.impair.empty() && vnet_impair_parse(params.impair.c_str(), &cfg.impair) != 0)
  {
    if (error) *error = "bad network impairment '" + params.impair + "'";
    return nullptr;
  }
  int err = 0;
  g->m_v = vnet_create(&cfg, &err);
  if (g->m_v == nullptr)
  {
    if (error) *error = std::string("cannot start the network: ") + vnet_strerror(err);
    return nullptr;
  }

  const netplay::SessionOptions so = sessionOptionsFor(params);

  std::string sessionError;
  if (params.host)
  {
    const uint16_t port = params.relay ? (uint16_t) netplay::RELAY_SESSION_PORT : params.port;
    g->m_session = netplay::LockstepSession::host(g->m_v, port, so, &sessionError);
  }
  else
  {
    vnet_addr hostAddr;
    if (params.relay)
    {
      // The room's creator is 10.0.0.1; this works before the room has been entered.
      const std::string roomHost = "room-host:" + std::to_string(netplay::RELAY_SESSION_PORT);
      const int rc = vnet_resolve(g->m_v, roomHost.c_str(), &hostAddr);
      if (rc != 0)
      {
        if (error) *error = std::string("cannot address the room's host: ") + vnet_strerror(rc);
        return nullptr;
      }
      resolved = &hostAddr;
    }
    g->m_session = netplay::LockstepSession::join(g->m_v, *resolved, so, &sessionError);
  }
  if (!g->m_session)
  {
    if (error) *error = sessionError;
    return nullptr;
  }

  netplay::NetMatchOptions mo;
  mo.outputDir = params.recordDir;
  g->m_match.reset(new netplay::NetMatch(*g->m_session, sim, mo));

  std::GameSettings* settings = std::GameSettings::getInstance();
  g->m_saved = settings->snapshot();
  g->m_haveSaved = true;
  g->m_hostSettings.mapWidth = g->m_saved.mapWidth;
  g->m_hostSettings.mapHeight = g->m_saved.mapHeight;
  g->m_hostSettings.blockPercentage = g->m_saved.blockPercentage;
  g->m_hostSettings.npcSpawner = g->m_saved.npcSpawnerEnabled;
  g->m_hostSettings.totalSlots = params.totalSlots;
  g->m_hostSettings.teams.assign(netplay::MAX_SLOTS, -1);
  g->updateLobbySettings();
  return g;
}

void NetGame::restoreSettings()
{
  if (m_haveSaved)
  {
    std::GameSettings::getInstance()->restore(m_saved);
    m_haveSaved = false;
  }
}

void NetGame::pump()
{
  m_session->pump();
  if (m_session->ended())
  {
    restoreSettings();   // the match is over: the player's own settings are theirs again
  }
}

bool NetGame::wait(uint32_t ms)
{
  return vnet_wait(m_v, (int) ms) >= 0;
}

void NetGame::updateLobbySettings()
{
  if (!m_params.host)
  {
    return;
  }
  // The host's choices in the lobby (the settings it started with are the menu's: a running round has overwritten
  // GameSettings with its own).
  netplay::LobbySettings ls = m_hostSettings;
  const int slots = (int) m_session->lobby().slots.size();
  const int needed = std::GameSettings::getInstance()->requiredMapHeightFor(std::max(slots, ls.totalSlots));
  if (slots > std::GameSettings::MAX_KEYBOARD_PLAYERS && ls.mapHeight < needed)
  {
    ls.mapHeight = needed;   // the menu would insist on the same: the portraits must fit
  }
  m_session->setLobbySettings(ls);
}

void NetGame::cycleSetting(HostSetting which)
{
  if (!m_params.host)
  {
    return;
  }
  typedef std::GameSettings GS;
  // Step from what the lobby shows (the map may have been raised for five players), not from what was typed before.
  const netplay::LobbySettings shown = m_session->lobby().settings;
  switch (which)
  {
    case SET_MAP_WIDTH:
      m_hostSettings.mapWidth = (shown.mapWidth >= GS::MAX_MAP_WIDTH || shown.mapWidth < GS::MIN_MAP_WIDTH) ? GS::MIN_MAP_WIDTH : shown.mapWidth + GS::MAP_SIZE_STEP;
      break;
    case SET_MAP_HEIGHT:
      m_hostSettings.mapHeight = (shown.mapHeight >= GS::MAX_MAP_HEIGHT || shown.mapHeight < GS::MIN_MAP_HEIGHT) ? GS::MIN_MAP_HEIGHT : shown.mapHeight + GS::MAP_SIZE_STEP;
      break;
    case SET_BLOCKS:
      m_hostSettings.blockPercentage = (shown.blockPercentage >= GS::MAX_BLOCK_PCT || shown.blockPercentage < GS::MIN_BLOCK_PCT) ? GS::MIN_BLOCK_PCT : shown.blockPercentage + 5;
      break;
    case SET_SPAWNER:
      m_hostSettings.npcSpawner = !shown.npcSpawner;
      break;
    case SET_SLOTS:
    {
      int t = m_hostSettings.totalSlots;
      t = (t == 0) ? 2 : (t >= netplay::MAX_SLOTS ? 0 : t + 1);
      m_hostSettings.totalSlots = t;
      break;
    }
  }
  updateLobbySettings();
}

void NetGame::cycleTeam(int slot)
{
  const int slots = (int) m_session->lobby().slots.size();
  if (!m_params.host || slot < 0 || slot >= slots)
  {
    return;
  }
  const int current = slot < (int) m_session->lobby().slots.size() ? m_session->lobby().slots[slot].team : slot;
  m_hostSettings.teams.resize(netplay::MAX_SLOTS, -1);
  m_hostSettings.teams[slot] = (current + 1) % slots;
  updateLobbySettings();
}

bool NetGame::returnToLobby()
{
  return m_session->returnToLobby();
}

netplay::LobbyContext NetGame::lobbyContext() const
{
  using netplay::LockstepSession;
  netplay::LobbyContext ctx;
  ctx.myId = m_session->myId();
  ctx.isHost = m_params.host;
  ctx.canStart = m_params.host && m_session->canStartRound();
  ctx.startBlocked = m_params.host ? m_session->cannotStartBecause() : std::string();
  ctx.myReady = m_session->wantsReady();
  ctx.viaRelay = m_params.relay;
  ctx.roomCode = roomCode();
  ctx.transport = m_params.relay ? transport() : std::string();
  ctx.hostSilenceMs = m_params.host ? 0 : m_session->hostSilenceMs();
  // A peer in the first lobby is LOBBY; one that joined between rounds is LOBBY too, but the host says so.
  ctx.inLobby = m_session->lobby().phase == netplay::PHASE_LOBBY || m_session->state() == LockstepSession::CONNECTING;
  if (m_params.host)
  {
    ctx.inLobby = m_session->state() == LockstepSession::LOBBY;
  }
  return ctx;
}

netplay::LobbyScreen NetGame::lobbyScreen() const
{
  return netplay::buildLobbyScreen(m_session->lobby(), lobbyContext());
}

netplay::EndMessage NetGame::endMessage() const
{
  if (m_params.relay && m_session->endReason() == netplay::LockstepSession::END_NETWORK)
  {
    vnet_status_info st;
    if (vnet_status(m_v, &st) == VNET_STATUS_FAILED)
    {
      return netplay::describeRelayFailure(st.error, m_params.relayDisplay.empty() ? m_params.relayUdp : m_params.relayDisplay, m_params.room, m_params.host);
    }
  }
  if (m_params.relay && m_session->endReason() == netplay::LockstepSession::END_CONNECT_TIMEOUT)
  {
    netplay::EndMessage m;
    m.title = "The host did not answer";
    m.lines.push_back("You are in room " + m_params.room + " on the relay, but the game that made the room sent nothing back.");
    m.lines.push_back("The room code may belong to something else than a Bit Riot game, or the host may be busy or in a background browser tab. Ask the host to check.");
    return m;
  }
  netplay::Identity mine;
  mine.protocol = netplay::PROTOCOL_VERSION;
  mine.buildId = Simulation::buildId();
  mine.dataHash = Simulation::dataHash();
  return netplay::describeEnd(m_session->endReason(), m_session->rejectInfo(), m_session->endText(), mine, m_params.join);
}

bool NetGame::startRound(std::string* error)
{
  updateLobbySettings();
  // A second pass: the slot count may just have changed with the settings.
  updateLobbySettings();
  const uint64_t seed = std::GameSettings::getInstance()->pickRoundSeed();
  return m_session->startRound(seed, error);
}

std::string NetGame::stallText() const
{
  using netplay::LockstepSession;
  const std::string waiting = m_session->waitingText();
  if (!m_params.host && m_session->state() == LockstepSession::RUNNING && m_session->hostSilenceMs() >= 1500)
  {
    // Nothing from the host. In a browser game the common reason is the host's tab being in the background.
    return "the host is away (nothing heard for " + std::to_string(m_session->hostSilenceMs() / 1000) + " s)";
  }
  return waiting;
}

std::string NetGame::joinText() const
{
  const netplay::LockstepSession& s = *m_session;
  if (s.state() != netplay::LockstepSession::RUNNING)
  {
    return "";
  }
  // A machine with no slot in a round's START (a late joiner the seats ran out for) watches that round like one that asked to.
  const bool watchingFromStart = s.joinStage() == netplay::LockstepSession::JOIN_NONE && s.spectating();
  return netplay::joinSentence(s.joinStage(), s.catchUpFraction(), s.takeoverReason(), m_params.spectate || watchingFromStart);
}

std::string NetGame::statusLine() const
{
  using netplay::LockstepSession;
  std::ostringstream t;
  const LockstepSession& s = *m_session;
  t << "NET " << (m_params.host ? "host" : "peer");
  if (s.state() == LockstepSession::RUNNING)
  {
    t << " D=" << s.delay();
    if (!m_params.host)
    {
      for (const netplay::LobbyEntry& e : s.lobby().peers)
      {
        if (e.id == s.myId() && e.rttMs >= 0) t << " ping " << e.rttMs << " ms";
      }
    }
    if (m_params.relay)
    {
      t << " " << transport();
    }
    const std::string stall = stallText();
    if (!stall.empty()) t << " - " << stall;
  }
  return t.str();
}
