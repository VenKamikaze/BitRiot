#include "HeadlessNet.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

#include "HeadlessCli.h"
#include "LockstepSession.h"
#include "MatchRecord.h"
#include "NetMatch.h"
#include "Simulation.h"
#include "vnet.h"

namespace headless
{
namespace
{
class UsageError : public std::runtime_error
{
  public:
    explicit UsageError(const std::string& what) : std::runtime_error(what) {}
};

std::string hex(uint64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%016llx", (unsigned long long) v);
  return buf;
}

struct Options
{
  bool host = false;
  std::string joinAddr;
  uint16_t port = 0;
  std::string portFile;
  int peers = 1;
  int rounds = 1;
  uint64_t seed = 1;
  std::string config;
  std::string scriptFile;
  int localPlayers = 1;
  std::string name;
  uint32_t tickMs = 33;
  int delay = 0;
  bool requireReady = false;   // host: wait for every peer's READY (as a host opened from the game's menus does)
  std::string outDir;
  std::vector<std::pair<int, uint32_t>> takeovers;   // slot, tick
  uint32_t leaveAt = 0;
  uint32_t perturbAt = 0;
  bool spectate = false;               // join: watch only
  uint32_t token = 0;                  // join: the reconnect token of an earlier session with this host
  std::string tokenFile;               // join: written (once) when the host has issued the token
  int timeoutSecs = 120;
  int waitSecs = 20;
};

unsigned long long number(const std::string& text, const std::string& what, unsigned long long max)
{
  const unsigned long long v = parseNumber(text, what);
  if (v > max)
  {
    throw UsageError(what + " " + text + " is too big (at most " + std::to_string(max) + ")");
  }
  return v;
}

Options parseOptions(const std::vector<std::string>& args)
{
  Options o;
  std::string command = args.at(0);
  if (command.size() > 2 && command[0] == '-')
  {
    command = command.substr(2);
  }
  o.host = command == "host";
  size_t i = 1;
  if (!o.host)
  {
    if (args.size() < 2 || args[1].empty() || args[1][0] == '-')
    {
      throw UsageError("join needs HOST:PORT");
    }
    o.joinAddr = args[1];
    i = 2;
  }
  for (; i < args.size(); i++)
  {
    std::string arg = args[i];
    if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-')
    {
      throw UsageError("unexpected argument " + arg);
    }
    std::string name = arg.substr(2);
    std::string value;
    const size_t eq = name.find('=');
    if (eq != std::string::npos)
    {
      value = name.substr(eq + 1);
      name = name.substr(0, eq);
    }
    else
    {
      if (i + 1 >= args.size())
      {
        throw UsageError("--" + name + " needs a value");
      }
      value = args[++i];
    }
    if (name == "port") o.port = (uint16_t) number(value, "--port", 65535);
    else if (name == "port-file") o.portFile = value;
    else if (name == "peers") o.peers = (int) number(value, "--peers", 5);
    else if (name == "rounds") o.rounds = (int) number(value, "--rounds", 1000);
    else if (name == "seed") o.seed = number(value, "--seed", ~0ull);
    else if (name == "config") o.config = value;
    else if (name == "script") o.scriptFile = value;
    else if (name == "local-players") o.localPlayers = (int) number(value, "--local-players", 6);
    else if (name == "name") o.name = value;
    else if (name == "tick-ms") o.tickMs = (uint32_t) number(value, "--tick-ms", 1000);
    else if (name == "delay") o.delay = (int) number(value, "--delay", 16);
    else if (name == "require-ready") o.requireReady = value != "0";
    else if (name == "out") o.outDir = value;
    else if (name == "leave-at") o.leaveAt = (uint32_t) number(value, "--leave-at", 1000000);
    else if (name == "perturb-at") o.perturbAt = (uint32_t) number(value, "--perturb-at", 1000000);
    else if (name == "spectate") o.spectate = value != "0";
    else if (name == "token") o.token = (uint32_t) number(value, "--token", 0xFFFFFFFFull);
    else if (name == "token-file") o.tokenFile = value;
    else if (name == "timeout-secs") o.timeoutSecs = (int) number(value, "--timeout-secs", 100000);
    else if (name == "wait-secs") o.waitSecs = (int) number(value, "--wait-secs", 100000);
    else if (name == "takeover")
    {
      const size_t at = value.find('@');
      if (at == std::string::npos)
      {
        throw UsageError("--takeover takes SLOT@TICK");
      }
      o.takeovers.push_back(std::make_pair((int) number(value.substr(0, at), "--takeover slot", 5),
                                            (uint32_t) number(value.substr(at + 1), "--takeover tick", 1000000)));
    }
    else throw UsageError("unknown option --" + name);
  }
  if (o.localPlayers < 1 || o.localPlayers > 6) throw UsageError("--local-players must be 1 to 6");
  if (o.rounds < 1) throw UsageError("--rounds must be at least 1");
  if (o.tickMs < 1) throw UsageError("--tick-ms must be at least 1");
  return o;
}

const char* kindName(ControlEvent::Kind k)
{
  switch (k)
  {
    case ControlEvent::HUMAN_TAKES_OVER: return "HUMAN_TAKES_OVER";
    case ControlEvent::RETURN_TO_AI: return "RETURN_TO_AI";
    case ControlEvent::LEAVE: return "LEAVE";
  }
  return "?";
}

class Runner
{
  public:
    Runner(const Options& o, std::ostream& out, std::ostream& err) : m_o(o), m_out(out), m_err(err) {}
    ~Runner()
    {
      if (m_session) m_session->flushOutgoing();
      m_session.reset();
      if (m_v != nullptr)
      {
        vnet_destroy(m_v);
      }
    }
    int run();

  private:
    uint64_t now() const { return vnet_time(m_v); }
    void idle();
    bool setup();
    bool handleDesync(int* exitCode);
    void beginRound(const netplay::RoundStart& rs);
    void stepOnce();
    void afterStep();
    void fastForwardChunk();
    void saveToken();
    int onEnded();
    void printRoundHeader(const netplay::RoundStart& rs);
    void printRoundEnd();

    Options m_o;
    std::ostream& m_out;
    std::ostream& m_err;
    Simulation m_sim;
    vnet* m_v = nullptr;
    std::unique_ptr<netplay::LockstepSession> m_session;
    std::unique_ptr<netplay::NetMatch> m_net;
    MatchRecord m_script;
    bool m_haveScript = false;
    uint8_t m_extraMask = 0;
    int m_round = 0;            // rounds begun
    int m_roundsDone = 0;
    uint32_t m_lastPrintedCheckpoint = 0;
    uint64_t m_startMs = 0;
    uint64_t m_nextTickAt = 0;
    int m_owed = 0;
    bool m_peersReady = false;
    uint64_t m_peersReadyAt = 0;
    uint64_t m_doneAt = 0;
    bool m_lingering = false;
    bool m_tokenSaved = false;
    bool m_wasCatchingUp = false;
    std::string m_desyncNote;
};

void Runner::idle()
{
  if (vnet_wait(m_v, 2) < 0)
  {
    usleep(1000);
  }
}

bool Runner::setup()
{
  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_UDP;
  cfg.family = VNET_FAMILY_IPV4;   // 127.0.0.1 and the lab networks of the tests; the game itself can use ANY
  int err = 0;
  m_v = vnet_create(&cfg, &err);
  if (m_v == nullptr)
  {
    throw UsageError(std::string("cannot create the network context: ") + vnet_strerror(err));
  }
  netplay::SessionOptions so;
  so.buildId = Simulation::buildId();
  so.dataHash = Simulation::dataHash();
  so.name = m_o.name.empty() ? (m_o.host ? "host" : "peer") : m_o.name;
  so.localPlayers = m_o.localPlayers;
  so.tickMs = m_o.tickMs;
  so.fixedDelay = m_o.delay;
  so.requireReady = m_o.requireReady && m_o.host;
  so.spectate = m_o.spectate && !m_o.host;
  so.reconnectToken = m_o.token;
  std::string error;
  if (m_o.host)
  {
    m_session = netplay::LockstepSession::host(m_v, m_o.port, so, &error);
    if (!m_session)
    {
      throw UsageError(error);
    }
    netplay::LobbySettings ls;
    if (!m_o.config.empty())
    {
      MatchConfig base;
      base.slots.resize(2);
      const MatchConfig c = parseConfigSpec(m_o.config, base);
      ls.mapWidth = c.mapWidth;
      ls.mapHeight = c.mapHeight;
      ls.blockPercentage = c.blockPercentage;
      ls.npcSpawner = c.npcSpawnerEnabled;
      ls.totalSlots = c.numPlayers();
      for (const MatchConfig::Slot& s : c.slots) ls.teams.push_back(s.team);
    }
    m_session->setLobbySettings(ls);
    if (!m_o.portFile.empty())
    {
      // Written to a temporary name and renamed, so a reader never sees half of it.
      const std::string tmp = m_o.portFile + ".tmp";
      {
        std::ofstream f(tmp.c_str());
        f << m_session->localPort() << "\n";
      }
      if (std::rename(tmp.c_str(), m_o.portFile.c_str()) != 0)
      {
        throw UsageError("cannot write " + m_o.portFile);
      }
    }
    m_err << "hosting on UDP port " << m_session->localPort() << "\n";
  }
  else
  {
    vnet_addr addr;
    const int rc = vnet_resolve_host(m_o.joinAddr.c_str(), VNET_FAMILY_IPV4, &addr);
    if (rc != 0)
    {
      throw UsageError("cannot resolve " + m_o.joinAddr + ": " + vnet_strerror(rc));
    }
    m_session = netplay::LockstepSession::join(m_v, addr, so, &error);
    if (!m_session)
    {
      throw UsageError(error);
    }
  }
  if (!m_o.scriptFile.empty())
  {
    try
    {
      m_script = MatchRecord::readFile(m_o.scriptFile);
      m_haveScript = true;
    }
    catch (const MatchRecordError& e)
    {
      throw UsageError(std::string("script: ") + e.what());
    }
  }
  netplay::NetMatchOptions no;
  no.outputDir = m_o.outDir;
  if (!m_o.outDir.empty())
  {
    std::error_code ec;
    std::filesystem::create_directories(m_o.outDir, ec);
    if (ec)
    {
      throw UsageError("cannot create " + m_o.outDir + ": " + ec.message());
    }
  }
  m_net.reset(new netplay::NetMatch(*m_session, m_sim, no));
  return true;
}

void Runner::printRoundHeader(const netplay::RoundStart& rs)
{
  m_out << "round " << rs.round << ": seed " << hex(rs.config.seed) << " map " << rs.config.mapWidth << "x" << rs.config.mapHeight
        << " blocks " << rs.config.blockPercentage << " spawner " << (rs.config.npcSpawnerEnabled ? "on" : "off")
        << " players " << rs.config.numPlayers() << " delay " << rs.delay << " owners";
  for (int s = 0; s < rs.config.numPlayers(); s++)
  {
    m_out << " " << s << ":";
    if (rs.owner[s] == netplay::NO_PEER) m_out << "ai"; else m_out << "p" << (int) rs.owner[s];
  }
  m_out << "\n";
  m_out.flush();
}

void Runner::beginRound(const netplay::RoundStart& rs)
{
  m_round++;
  m_extraMask = 0;
  m_lastPrintedCheckpoint = 0;
  m_owed = 0;
  m_nextTickAt = 0;
  m_net->beginRound(rs);
  printRoundHeader(rs);
  m_err << "round " << rs.round << " begins; this machine plays slots mask 0x" << std::hex << (int) rs.localSlotMask << std::dec
        << ", delay " << rs.delay << " (" << m_session->delayNote() << ")\n";
}

// A late joiner steps the host's record without a clock (NetMatch::fastForward), in slices short enough for the network to
// keep being served between them, printing exactly what a machine that stepped those ticks live printed.
void Runner::fastForwardChunk()
{
  const uint64_t until = now() + 20;
  for (int i = 0; i < 400 && now() < until; i++)
  {
    if (m_o.perturbAt != 0 && m_sim.tick() + 1 == m_o.perturbAt)
    {
      perturbSimulation(m_sim);   // (test only) a joiner whose state is wrong from here on
      m_err << "perturbed the state before tick " << m_o.perturbAt << "\n";
    }
    if (m_net->fastForward(1) == 0)
    {
      break;
    }
    afterStep();
    if (!m_net->active())
    {
      break;
    }
  }
}

void Runner::saveToken()
{
  if (m_tokenSaved || m_o.tokenFile.empty() || m_session->token() == 0)
  {
    return;
  }
  const std::string tmp = m_o.tokenFile + ".tmp";
  {
    std::ofstream f(tmp.c_str());
    f << m_session->token() << "\n";
  }
  if (std::rename(tmp.c_str(), m_o.tokenFile.c_str()) == 0)
  {
    m_tokenSaved = true;
  }
}

void Runner::printRoundEnd()
{
  const std::shared_ptr<PlayerCharacterEntity> w = m_sim.winner();
  m_out << "round " << m_net->round().round << ": over at tick " << m_sim.tick() << ", winner ";
  if (w) m_out << "player " << w->getPlayerNumber(); else m_out << "none";
  m_out << ", final hash " << hex(m_sim.stateHash()) << "\n";
  m_out.flush();
}

void Runner::stepOnce()
{
  const uint32_t tick = m_session->nextStepTick();
  if (m_o.leaveAt != 0 && tick == m_o.leaveAt)
  {
    m_session->leave();
    return;
  }
  uint8_t mask = (uint8_t) (m_net->localSlotMask() | m_extraMask);
  TickInputs sample;
  if (m_haveScript && tick >= 1 && tick - 1 < m_script.ticks.size())
  {
    for (int s = 0; s < netplay::MAX_SLOTS; s++)
    {
      if (mask & (1u << s)) sample.slot[s] = m_script.ticks[tick - 1].slot[s];
    }
  }
  if (m_net->round().round == 1)
  {
    for (const auto& t : m_o.takeovers)
    {
      if (t.second == tick)
      {
        sample.control.push_back(ControlEvent(t.first, ControlEvent::HUMAN_TAKES_OVER));
        m_extraMask = (uint8_t) (m_extraMask | (1u << t.first));
        mask = (uint8_t) (mask | (1u << t.first));
      }
    }
  }
  if (m_o.perturbAt != 0 && m_sim.tick() + 1 == m_o.perturbAt)
  {
    perturbSimulation(m_sim);
    m_err << "perturbed the state before tick " << m_o.perturbAt << "\n";
  }
  m_net->step(sample, mask);
  afterStep();
}

// What every machine prints after a tick, the late joiner's fast-forwarded ones included: the events of the tick, a
// checkpoint line, and the end of the round.
void Runner::afterStep()
{
  const TickInputs& stepped = m_net->lastInputs();
  for (const ControlEvent& e : stepped.control)
  {
    m_out << "event " << m_net->round().round << " " << m_sim.tick() << " slot " << e.slot << " " << kindName(e.kind) << "\n";
  }
  if (m_net->lastCheckpointTick() != m_lastPrintedCheckpoint)
  {
    m_lastPrintedCheckpoint = m_net->lastCheckpointTick();
    m_out << "checkpoint " << m_net->round().round << " " << m_lastPrintedCheckpoint << " " << hex(m_net->lastCheckpointHash()) << "\n";
    m_out.flush();
  }
  if (!m_net->active())
  {
    // The round ended with this tick (NetMatch told the session).
    m_roundsDone++;
    printRoundEnd();
    if (!m_o.outDir.empty())
    {
      std::string path, error;
      if (m_net->writeRoundRecord(&path, &error)) m_err << "record: " << path << "\n"; else m_err << "warning: " << error << "\n";
    }
  }
}

// A desync: write the evidence, say so, stay long enough for the others to be told.
bool Runner::handleDesync(int* exitCode)
{
  if (!m_session->desynced())
  {
    return false;
  }
  const netplay::DesyncInfo& d = m_session->desyncInfo();
  if (!m_lingering)
  {
    m_out << "desync round " << d.round << " tick " << d.tick << " host " << hex(d.hostHash) << " peer " << hex(d.peerHash)
          << " offender " << (int) d.offender << "\n";
    m_out.flush();
    netplay::NetMatch::DesyncFiles files;
    std::string error;
    if (m_net->writeDesyncFiles(files, &error))
    {
      m_err << "desync: state at the tick: " << (files.dumpAtTick.empty() ? "(no longer kept)" : files.dumpAtTick)
            << "\ndesync: state now: " << files.dumpNow << "\ndesync: record: " << files.record << "\n";
    }
    else
    {
      m_err << "desync: could not write the evidence: " << error << "\n";
    }
    m_lingering = true;
    m_doneAt = now() + 1500;
  }
  if (now() >= m_doneAt)
  {
    m_session->leave(netplay::LEAVE_DESYNC);
    *exitCode = EXIT_FAILED;
    return true;
  }
  return false;
}

int Runner::onEnded()
{
  using netplay::LockstepSession;
  const bool allDone = m_roundsDone >= m_o.rounds;
  if (m_session->desynced())
  {
    return EXIT_FAILED;   // the host closed after telling us the state diverged
  }
  switch (m_session->endReason())
  {
    case LockstepSession::END_REJECTED:
    {
      m_err << "error: " << m_session->endText() << "\n";
      const uint8_t r = m_session->rejectInfo().reason;
      return (r == netplay::REJECT_BUILD || r == netplay::REJECT_DATA || r == netplay::REJECT_VERSION) ? EXIT_DATA : EXIT_FAILED;
    }
    case LockstepSession::END_LEFT:
      return EXIT_OK;   // --leave-at
    case LockstepSession::END_DESYNC:
      return EXIT_FAILED;
    default:
      break;
  }
  if (allDone && m_session->endReason() == LockstepSession::END_HOST_CLOSED)
  {
    return EXIT_OK;
  }
  m_err << "error: the session ended: " << m_session->endText() << "\n";
  return EXIT_FAILED;
}

int Runner::run()
{
  setup();
  m_startMs = now();
  const uint64_t deadline = m_startMs + (uint64_t) m_o.timeoutSecs * 1000;
  uint64_t lastRoundEndAt = 0;
  for (;;)
  {
    m_session->pump();
    const uint64_t t = now();
    if (t > deadline)
    {
      m_err << "error: gave up after " << m_o.timeoutSecs << " s\n";
      m_session->leave();
      return EXIT_FAILED;
    }
    int code = 0;
    if (m_session->ended())
    {
      return onEnded();
    }
    if (handleDesync(&code))
    {
      return code;
    }
    netplay::RoundStart rs;
    if (m_session->takeRoundStart(rs))
    {
      beginRound(rs);
    }

    using netplay::LockstepSession;
    if (!m_o.host && m_session->state() == LockstepSession::LOBBY)
    {
      // A host opened from the game's menus waits for every peer to say ready (protocol 2); this runner has
      // nobody to press a button, so it is ready as soon as it is in a lobby (a no-op once it is).
      m_session->setReady(true);
    }
    saveToken();
    if (m_session->state() == LockstepSession::RUNNING && m_session->catchingUp() && m_net->active() && !m_session->desynced())
    {
      if (!m_wasCatchingUp)
      {
        m_wasCatchingUp = true;
        m_err << "joined a round in progress; catching up to tick " << m_session->catchUpHead() << "\n";
      }
      fastForwardChunk();
      continue;   // no sleep: the next slice, after the network has had its turn
    }
    if (m_wasCatchingUp && !m_session->catchingUp())
    {
      m_wasCatchingUp = false;
      m_err << "caught up at tick " << m_session->nextStepTick() - 1 << "\n";
    }
    if (m_session->state() == LockstepSession::RUNNING && m_net->active() && !m_session->desynced())
    {
      // The clock: one tick every tickMs, a stall forgiven beyond five owed.
      if (m_nextTickAt == 0)
      {
        m_nextTickAt = t;
      }
      while (t >= m_nextTickAt)
      {
        m_owed = std::min(m_owed + 1, 5);
        m_nextTickAt += m_o.tickMs;
      }
      int extra = m_session->extraTicksWanted();
      while ((m_owed > 0 || extra > 0) && m_net->canStep())
      {
        if (m_owed > 0) m_owed--; else extra--;
        stepOnce();
        if (m_session->ended() || !m_net->active())
        {
          break;
        }
      }
      if (m_session->ended())
      {
        continue;
      }
    }
    else if (m_o.host && (m_session->state() == LockstepSession::LOBBY || m_session->state() == LockstepSession::BETWEEN_ROUNDS))
    {
      if (m_session->state() == LockstepSession::BETWEEN_ROUNDS && lastRoundEndAt == 0)
      {
        lastRoundEndAt = t;
      }
      if (m_roundsDone >= m_o.rounds)
      {
        // Every round has been played. Stay until the peers hold the last tick and have had
        // time to step it, then close.
        if (m_session->canStartRound() && t - lastRoundEndAt >= 1500)
        {
          m_session->leave(netplay::LEAVE_HOST_CLOSED);
          return EXIT_OK;
        }
      }
      else
      {
        if (!m_peersReady && m_session->peerCount() >= m_o.peers)
        {
          m_peersReady = true;
          m_peersReadyAt = t;
        }
        if (!m_peersReady && t - m_startMs > (uint64_t) m_o.waitSecs * 1000)
        {
          m_err << "error: only " << m_session->peerCount() << " of " << m_o.peers << " peers joined in " << m_o.waitSecs << " s\n";
          m_session->leave();
          return EXIT_FAILED;
        }
        // A beat after the last peer joined, so the pings have measured the round trips.
        if (m_peersReady && t - m_peersReadyAt >= 1200 && m_session->canStartRound())
        {
          std::string error;
          if (!m_session->startRound(m_o.seed + (uint64_t) m_roundsDone, &error))
          {
            m_err << "error: " << error << "\n";
            m_session->leave();
            return EXIT_FAILED;
          }
          lastRoundEndAt = 0;
        }
      }
    }
    else if (!m_o.host && m_session->state() == LockstepSession::BETWEEN_ROUNDS && m_roundsDone >= m_o.rounds)
    {
      // The last round is done here. The host closes the session once everyone holds the last tick;
      // wait for it a few seconds, then go anyway.
      if (lastRoundEndAt == 0)
      {
        lastRoundEndAt = t;
      }
      if (t - lastRoundEndAt > 6000)
      {
        m_session->leave();
        return EXIT_OK;
      }
    }
    idle();
  }
}

} // namespace

int runNetCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  Options o;
  try
  {
    o = parseOptions(args);
  }
  catch (const std::invalid_argument& e)
  {
    err << "error: " << e.what() << "\n(bitriot_headless help shows the usage)\n";
    return EXIT_USAGE;
  }
  catch (const UsageError& e)
  {
    err << "error: " << e.what() << "\n(bitriot_headless help shows the usage)\n";
    return EXIT_USAGE;
  }
  try
  {
    Runner runner(o, out, err);
    return runner.run();
  }
  catch (const UsageError& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
  catch (const std::exception& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
}
} // namespace headless
