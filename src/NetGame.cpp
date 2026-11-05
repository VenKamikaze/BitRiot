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
  std::thread([state, hostPort, resolver]() {
    vnet_addr addr;
    std::memset(&addr, 0, sizeof(addr));
    const int rc = resolver(hostPort, &addr);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->rc = rc;
    state->addr = addr;
    state->done = true;
  }).detach();
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

std::unique_ptr<NetGame> NetGame::create(const Params& params, Simulation& sim, std::string* error)
{
  if (!params.host)
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

std::unique_ptr<NetGame> NetGame::build(const Params& params, Simulation& sim, const vnet_addr* resolved, std::string* error)
{
  std::unique_ptr<NetGame> g(new NetGame());
  g->m_params = params;
  g->m_sim = &sim;

  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_UDP;   // VNET_IMPAIR in the environment adds simulated latency and loss
  int err = 0;
  g->m_v = vnet_create(&cfg, &err);
  if (g->m_v == nullptr)
  {
    if (error) *error = std::string("cannot start the network: ") + vnet_strerror(err);
    return nullptr;
  }

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

  std::string sessionError;
  if (params.host)
  {
    g->m_session = netplay::LockstepSession::host(g->m_v, params.port, so, &sessionError);
  }
  else
  {
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
  return m_session->waitingText();
}

std::string NetGame::joinText() const
{
  const netplay::LockstepSession& s = *m_session;
  if (s.state() != netplay::LockstepSession::RUNNING)
  {
    return "";
  }
  return netplay::joinSentence(s.joinStage(), s.catchUpFraction(), s.spectating(), m_params.spectate);
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
    const std::string stall = s.waitingText();
    if (!stall.empty()) t << " - " << stall;
  }
  return t.str();
}
