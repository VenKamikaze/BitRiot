// SessionHarness.h
//
// N machines, each a LockstepSession with a fake "game" on top, in ONE process on vnet's
// loopback network, in virtual time (nothing sleeps). The simulation allows one
// Simulation per process, but LockstepSession knows nothing of it: a fake game only
// samples a deterministic input, steps when canStep() says so, and logs the TickInputs it
// stepped. The assertions compare those logs across machines.
//
//   Cluster c(3, opts);          // machine 0 hosts, 1 and 2 join; runs until all are in the lobby
//   c.startRound(seed);          // the host starts, runs until every machine has the START
//   c.run(5000);                 // 5 s of virtual time: pump, sample, step, at 30 ticks a second
//   c.machine(1).log             // what machine 1 stepped
//
// The fake game steps like FrameLoop does: one tick is owed every 33 ms (at most 5 owed), a
// tick runs when it is owed and canStep() is true.

#ifndef _SESSIONHARNESS_H
#define _SESSIONHARNESS_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "LockstepSession.h"
#include "vnet.h"

namespace netharness
{
using netplay::LockstepSession;
using netplay::SessionOptions;
using netplay::RoundStart;

// The byte a machine samples for a slot for a tick. Deterministic, and different for every
// slot, tick and machine (peer id), so that a mix-up of any of the three shows. Only the
// six held bits.
inline uint8_t sampleByte(int slot, uint32_t tick, int machine)
{
  return (uint8_t) ((slot * 37 + tick * 11 + machine * 19 + 5) & 0x3F);
}

struct Machine
{
  vnet* v = nullptr;
  std::unique_ptr<LockstepSession> s;
  std::string name;
  // The fake game.
  std::vector<TickInputs> log;          // log[t-1] = what was stepped for tick t
  std::vector<uint64_t> stepTimes;      // virtual ms at which each tick was stepped
  int owed = 0;
  uint64_t nextTickAt = 0;
  bool paused = false;                  // the game does not step (a window being dragged)
  bool frozen = false;                  // nothing runs at all: not even pump() (a crashed or suspended process)
  uint32_t roundTicks = 0;              // finish the round after this many ticks (0 = never)
  bool roundIsOver = false;
  RoundStart round;
  bool haveRound = false;
  int roundsStarted = 0;
  uint8_t localMask = 0;
  // Control events to request: tick being stepped -> events (submitted with that sample).
  std::map<uint32_t, std::vector<ControlEvent>> wantEvents;
  // Hash to report at a checkpoint tick (default: a function of the tick; a test changes it).
  std::function<uint64_t(uint32_t)> hashFor = [](uint32_t t) { return 0x1000u + t; };
  uint32_t checkpointEvery = 30;
  std::vector<std::pair<uint32_t, uint64_t>> reported;
};

struct ClusterOptions
{
  std::string impair;                  // a vnet impairment spec applied to every machine ("" = none)
  uint32_t delayMs = 0;                // shorthand: lat=<delayMs> (each way)
  int localPlayers[6] = {1, 1, 1, 1, 1, 1};
  SessionOptions session;              // base options (identity is filled in)
  netplay::LobbySettings lobby;
  uint64_t seedBase = 100;             // impairment seeds are seedBase + the machine's index
};

class Cluster
{
  public:
    explicit Cluster(int machines, const ClusterOptions& opts = ClusterOptions());
    ~Cluster();

    Machine& machine(int i) { return *m_machines[i]; }
    int size() const { return (int) m_machines.size(); }
    vnet_loopback_network* network() { return m_net; }
    uint64_t now() const { return vnet_loopback_network_time(m_net); }

    // Virtual milliseconds, one at a time: network step, every session pumped, the fake games.
    void run(uint32_t ms);
    // Until `done()` or `limitMs` of virtual time. Returns whether done() became true.
    bool runUntil(const std::function<bool()>& done, uint32_t limitMs);

    // The host starts a round; runs until every connected machine has taken its RoundStart.
    bool startRound(uint64_t seed, uint32_t roundTicks, std::string* error = nullptr);
    // True when every machine in `which` has stepped at least `ticks` ticks.
    bool allStepped(uint32_t ticks, const std::vector<int>& which) const;
    bool allStepped(uint32_t ticks) const;
    // Applies impairment to one machine's sends (nullptr spec = none).
    void impair(int i, const std::string& spec);
    void impairAll(const std::string& spec);

    // Adds a machine that joins the host's session now (late joiners, mismatched builds). 0 = the default identity.
    Machine& addMachine(int localPlayers = 1, uint64_t buildId = 0, uint64_t dataHash = 0);

    // A raw vnet socket on the network, for crafting packets by hand.
    struct Raw
    {
      vnet* v = nullptr;
      int sock = -1;
    };
    Raw openRaw();
    const vnet_addr& hostAddr() const { return m_hostAddr; }

    // The ClusterOptions' lobby settings are applied at construction; this changes them.
    void setLobby(const netplay::LobbySettings& l) { m_machines[0]->s->setLobbySettings(l); }

  private:
    void tickMachine(Machine& m);
    vnet* makeContext(int index, const std::string& impairSpec);
    vnet_loopback_network* m_net = nullptr;
    std::vector<std::unique_ptr<Machine>> m_machines;
    std::vector<vnet*> m_rawContexts;
    vnet_addr m_hostAddr;
    ClusterOptions m_opts;
};

inline std::string impairSpecFor(const std::string& base, int index, uint64_t seedBase = 100)
{
  std::string s = base;
  if (s.empty())
  {
    return s;
  }
  return s + ",seed=" + std::to_string(seedBase + (uint64_t) index);
}

inline vnet* Cluster::makeContext(int index, const std::string& impairSpec)
{
  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_LOOPBACK;
  cfg.loopback_network = m_net;
  cfg.ignore_env = 1;
  if (!impairSpec.empty())
  {
    vnet_impair parsed;
    if (vnet_impair_parse(impairSpecFor(impairSpec, index, m_opts.seedBase).c_str(), &parsed) == 0)
    {
      cfg.impair = parsed;
    }
  }
  int err = 0;
  vnet* v = vnet_create(&cfg, &err);
  return v;
}

inline Cluster::Cluster(int machines, const ClusterOptions& opts) : m_opts(opts)
{
  m_net = vnet_loopback_network_create();
  std::string spec = opts.impair;
  if (opts.delayMs != 0)
  {
    spec = (spec.empty() ? "" : spec + ",") + "lat=" + std::to_string(opts.delayMs);
  }
  std::memset(&m_hostAddr, 0, sizeof(m_hostAddr));
  for (int i = 0; i < machines; i++)
  {
    std::unique_ptr<Machine> m(new Machine());
    m->name = i == 0 ? "host" : "peer" + std::to_string(i);
    m->v = makeContext(i, spec);
    SessionOptions so = opts.session;
    so.buildId = 0xB111D;
    so.dataHash = 0xDA7A;
    so.name = m->name;
    so.localPlayers = opts.localPlayers[i];
    so.randomSeed = 7000 + (uint64_t) i;
    std::string error;
    if (i == 0)
    {
      m->s = LockstepSession::host(m->v, 0, so, &error);
      vnet_addr local;
      std::memset(&local, 0, sizeof(local));
      char buf[64];
      std::snprintf(buf, sizeof(buf), "127.1.0.1:%u", (unsigned) m->s->localPort());
      vnet_resolve(m->v, buf, &m_hostAddr);
      m->s->setLobbySettings(opts.lobby);
    }
    else
    {
      m->s = LockstepSession::join(m->v, m_hostAddr, so, &error);
    }
    m_machines.push_back(std::move(m));
  }
  // Everybody into the lobby (a peer that is refused stays CONNECTING / ENDED; tests look).
  runUntil([this] {
    for (auto& m : m_machines)
    {
      if (m->s->state() == LockstepSession::CONNECTING) return false;
    }
    return true;
  }, 20000);
  // Let the lobby pings measure RTTs: five pings.
  run(1500);
}

inline Machine& Cluster::addMachine(int localPlayers, uint64_t buildId, uint64_t dataHash)
{
  const int i = (int) m_machines.size();
  std::unique_ptr<Machine> m(new Machine());
  m->name = "peer" + std::to_string(i);
  std::string spec = m_opts.impair;
  if (m_opts.delayMs != 0)
  {
    spec = (spec.empty() ? "" : spec + ",") + "lat=" + std::to_string(m_opts.delayMs);
  }
  m->v = makeContext(i, spec);
  SessionOptions so = m_opts.session;
  so.buildId = buildId != 0 ? buildId : 0xB111D;
  so.dataHash = dataHash != 0 ? dataHash : 0xDA7A;
  so.name = m->name;
  so.localPlayers = localPlayers;
  so.randomSeed = 7000 + (uint64_t) i;
  std::string error;
  m->s = LockstepSession::join(m->v, m_hostAddr, so, &error);
  m_machines.push_back(std::move(m));
  return *m_machines.back();
}

inline Cluster::~Cluster()
{
  m_machines.clear();   // sessions close their sockets first
  for (vnet* v : m_rawContexts) vnet_destroy(v);
  vnet_loopback_network_destroy(m_net);
}

inline void Cluster::tickMachine(Machine& m)
{
  LockstepSession& s = *m.s;
  RoundStart rs;
  if (s.takeRoundStart(rs))
  {
    m.round = rs;
    m.haveRound = true;
    m.roundsStarted++;
    m.localMask = rs.localSlotMask;
    m.log.clear();
    m.stepTimes.clear();
    m.reported.clear();
    m.roundIsOver = false;
    m.owed = 0;
  }
  if (!m.haveRound || m.roundIsOver || s.state() != LockstepSession::RUNNING)
  {
    return;
  }
  const uint64_t t = now();
  if (m.nextTickAt == 0 || t >= m.nextTickAt)
  {
    if (m.nextTickAt == 0) m.nextTickAt = t;
    // The clock: one tick every 33 ms, and a stall is forgiven beyond 5 ticks.
    while (t >= m.nextTickAt)
    {
      m.owed = std::min(m.owed + 1, 5);
      m.nextTickAt += 33;
    }
  }
  if (m.paused)
  {
    return;
  }
  int extra = s.extraTicksWanted();
  while ((m.owed > 0 || extra > 0) && s.canStep() && !m.roundIsOver)
  {
    if (m.owed > 0) m.owed--; else extra--;
    const uint32_t tick = s.nextStepTick();
    TickInputs sample;
    auto ev = m.wantEvents.find(tick);
    if (ev != m.wantEvents.end())
    {
      sample.control = ev->second;
      // Like the real input layer: the local view changes when the event is produced.
      for (const ControlEvent& e : ev->second)
      {
        if (e.slot < 0 || e.slot >= netplay::MAX_SLOTS) continue;
        if (e.kind == ControlEvent::HUMAN_TAKES_OVER) m.localMask |= (uint8_t) (1u << e.slot);
        else m.localMask &= (uint8_t) ~(1u << e.slot);
      }
    }
    for (int slot = 0; slot < netplay::MAX_SLOTS; slot++)
    {
      if (m.localMask & (1u << slot))
      {
        sample.slot[slot] = PlayerInput(sampleByte(slot, tick + (uint32_t) m.round.delay, s.myId()));
      }
    }
    m.log.push_back(s.stepInputs());
    m.stepTimes.push_back(t);
    s.submitLocal(sample, m.localMask);
    if (m.checkpointEvery != 0 && tick % m.checkpointEvery == 0)
    {
      const uint64_t h = m.hashFor(tick);
      m.reported.push_back(std::make_pair(tick, h));
      s.reportChecksum(tick, h);
    }
    s.tickStepped();
    if (m.roundTicks != 0 && tick >= m.roundTicks)
    {
      m.roundIsOver = true;
      s.roundFinished();
    }
  }
}

inline void Cluster::run(uint32_t ms)
{
  for (uint32_t i = 0; i < ms; i++)
  {
    vnet_loopback_network_step(m_net, 1);
    for (auto& m : m_machines)
    {
      if (m->frozen) continue;
      m->s->pump();
      tickMachine(*m);
    }
  }
}

inline bool Cluster::runUntil(const std::function<bool()>& done, uint32_t limitMs)
{
  for (uint32_t i = 0; i < limitMs; i++)
  {
    if (done())
    {
      return true;
    }
    run(1);
  }
  return done();
}

inline bool Cluster::startRound(uint64_t seed, uint32_t roundTicks, std::string* error)
{
  for (auto& m : m_machines)
  {
    m->roundTicks = roundTicks;
  }
  if (!m_machines[0]->s->startRound(seed, error))
  {
    return false;
  }
  const uint16_t round = m_machines[0]->s->round();
  return runUntil([this, round] {
    for (auto& m : m_machines)
    {
      if (m->s->state() == LockstepSession::ENDED) continue;
      if (!m->haveRound || m->round.round != round) return false;
    }
    return true;
  }, 5000);
}

inline bool Cluster::allStepped(uint32_t ticks, const std::vector<int>& which) const
{
  for (int i : which)
  {
    if (m_machines[i]->log.size() < ticks) return false;
  }
  return true;
}

inline bool Cluster::allStepped(uint32_t ticks) const
{
  for (const auto& m : m_machines)
  {
    if (m->s->state() == LockstepSession::ENDED) continue;
    if (m->log.size() < ticks) return false;
  }
  return true;
}

inline void Cluster::impair(int i, const std::string& spec)
{
  if (spec.empty())
  {
    vnet_set_impair(m_machines[i]->v, nullptr);
    return;
  }
  vnet_impair imp;
  if (vnet_impair_parse(impairSpecFor(spec, i + 50, m_opts.seedBase).c_str(), &imp) == 0)
  {
    vnet_set_impair(m_machines[i]->v, &imp);
  }
}

inline void Cluster::impairAll(const std::string& spec)
{
  for (int i = 0; i < size(); i++) impair(i, spec);
}

inline Cluster::Raw Cluster::openRaw()
{
  Raw r;
  r.v = makeContext(90 + (int) m_rawContexts.size(), "");
  m_rawContexts.push_back(r.v);
  r.sock = vnet_open(r.v, 0);
  return r;
}
} // namespace netharness

#endif
