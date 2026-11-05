#include "NetGame.h"

#include <sstream>

#include "MickLogger.h"
#include "Simulation.h"
#include "vnet.h"

NetGame::~NetGame()
{
  m_match.reset();
  if (m_session)
  {
    m_session->leave();   // a no-op if it has ended; otherwise the others are told at once
    m_session.reset();
  }
  if (m_v != nullptr)
  {
    vnet_destroy(m_v);
  }
}

std::unique_ptr<NetGame> NetGame::create(const Params& params, Simulation& sim, std::string* error)
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
  so.name = params.name.empty() ? (params.host ? "host" : "player") : params.name;
  so.localPlayers = params.localPlayers;
  so.tickMs = std::GameSettings::NORMAL_TICKS_SPEED;

  std::string sessionError;
  if (params.host)
  {
    g->m_session = netplay::LockstepSession::host(g->m_v, params.port, so, &sessionError);
  }
  else
  {
    vnet_addr addr;
    const int rc = vnet_resolve_host(params.join.c_str(), VNET_FAMILY_ANY, &addr);
    if (rc != 0)
    {
      if (error) *error = "cannot find " + params.join + ": " + vnet_strerror(rc) + " (use host:port, for example 192.168.1.20:7777)";
      return nullptr;
    }
    g->m_session = netplay::LockstepSession::join(g->m_v, addr, so, &sessionError);
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
  // The menu's choices are the host's (the saved ones: a running round has overwritten
  // GameSettings with its own).
  netplay::LobbySettings ls;
  ls.mapWidth = m_saved.mapWidth;
  ls.mapHeight = m_saved.mapHeight;
  ls.blockPercentage = m_saved.blockPercentage;
  ls.npcSpawner = m_saved.npcSpawnerEnabled;
  ls.totalSlots = m_params.totalSlots;
  const int slots = (int) m_session->lobby().slots.size();
  const int needed = std::GameSettings::getInstance()->requiredMapHeightFor(std::max(slots, ls.totalSlots));
  if (slots > std::GameSettings::MAX_KEYBOARD_PLAYERS && ls.mapHeight < needed)
  {
    ls.mapHeight = needed;   // the menu would insist on the same: the portraits must fit
  }
  m_session->setLobbySettings(ls);
}

bool NetGame::startRound(std::string* error)
{
  updateLobbySettings();
  // A second pass: the slot count may just have changed with the settings.
  updateLobbySettings();
  const uint64_t seed = std::GameSettings::getInstance()->pickRoundSeed();
  return m_session->startRound(seed, error);
}

std::string NetGame::lobbyTitle() const
{
  using netplay::LockstepSession;
  switch (m_session->state())
  {
    case LockstepSession::CONNECTING: return "Connecting...";
    case LockstepSession::ENDED: return "Disconnected";
    default: break;
  }
  return m_params.host ? "Hosting a network game" : "Network game";
}

std::string NetGame::lobbyText() const
{
  using netplay::LockstepSession;
  std::ostringstream t;
  const LockstepSession& s = *m_session;
  if (s.state() == LockstepSession::ENDED)
  {
    t << s.endText() << "\n\nPress Leave to go back to the menu.";
    return t.str();
  }
  if (s.state() == LockstepSession::CONNECTING)
  {
    t << "Asking " << m_params.join << " to let us in...";
    return t.str();
  }
  if (m_params.host)
  {
    t << "Listening on UDP port " << s.localPort() << ". Others join with:  --join <your address>:" << s.localPort() << "\n";
  }
  else
  {
    t << "Joined " << m_params.join << " as " << (m_params.name.empty() ? "player" : m_params.name) << ".\n";
  }
  const netplay::LobbyView& v = s.lobby();
  t << "Map " << v.settings.mapWidth << "x" << v.settings.mapHeight << ", " << v.settings.blockPercentage << "% blocks, "
    << (v.settings.npcSpawner ? "enemies on" : "enemies off") << ".\n\nPlayers:\n";
  for (const netplay::LobbyEntry& e : v.peers)
  {
    t << "  " << e.name << (e.id == s.myId() ? " (you)" : "") << " - slot";
    bool first = true;
    for (int i = 0; i < 6; i++)
    {
      if (e.slotMask & (1u << i)) { t << (first ? " " : ", ") << (i + 1); first = false; }
    }
    if (e.rttMs >= 0 && e.id != netplay::HOST_ID) t << "  [" << e.rttMs << " ms]";
    t << "\n";
  }
  int bots = 0;
  for (const netplay::SlotInfo& si : v.slots) if (si.owner == netplay::NO_PEER) bots++;
  if (bots > 0) t << "  " << bots << " computer player" << (bots == 1 ? "" : "s") << "\n";
  if (m_params.host)
  {
    t << "\nInput delay for the next round: " << s.proposedDelay() << " ticks.\n";
  }
  else
  {
    t << "\nWaiting for the host to start the match.";
  }
  return t.str();
}

std::string NetGame::stallText() const
{
  return m_session->waitingText();
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
