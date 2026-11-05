#include "NetMatch.h"

#include <fstream>
#include <sstream>

#include "MickLogger.h"
#include "Simulation.h"

namespace netplay
{
NetMatch::NetMatch(LockstepSession& session, Simulation& sim, const Options& options)
  : m_session(session), m_sim(sim), m_opt(options), m_recorder(options.checkpointInterval, true)
{
}

std::string NetMatch::path(const std::string& name) const
{
  if (m_opt.outputDir.empty())
  {
    return name;
  }
  return m_opt.outputDir + "/" + name;
}

void NetMatch::beginRound(const RoundStart& round)
{
  m_round = round;
  m_dumps.clear();
  m_filesWritten = false;
  m_files = DesyncFiles();
  m_lastCheckpointTick = 0;
  m_lastCheckpointHash = 0;
  m_recorder = MatchRecorder(m_opt.checkpointInterval, true);
  m_sim.start(round.config);
  m_recorder.begin(m_sim);
  m_recording = true;
  m_active = true;
}

const TickInputs& NetMatch::step(const TickInputs& localSample, uint8_t sampledMask)
{
  // The inputs the whole session agreed on for this tick (a copy: the session drops its
  // own once the tick is stepped).
  m_lastInputs = m_session.stepInputs();
  m_session.submitLocal(localSample, sampledMask);
  m_sim.step(m_lastInputs);
  if (m_recording)
  {
    try
    {
      m_recorder.onStep(m_lastInputs, m_sim);
    }
    catch (const std::exception& e)
    {
      // Never let the recorder stop a game (past MatchRecord::MAX_TICKS, about 9.7 hours, it refuses).
      std::MickLogger::getInstance()->error(nullptr, std::string("Match record abandoned: ").append(e.what()));
      m_recording = false;
    }
  }

  const uint32_t tick = (uint32_t) m_sim.tick();
  const bool last = m_sim.roundOver();
  if ((m_opt.checkpointInterval != 0 && tick % m_opt.checkpointInterval == 0) || last)
  {
    const uint64_t hash = m_sim.stateHash();
    m_lastCheckpointTick = tick;
    m_lastCheckpointHash = hash;
    Dump d;
    d.tick = tick;
    std::ostringstream text;
    m_sim.dumpState(text);
    d.text = text.str();
    m_dumps.push_back(std::move(d));
    while (m_dumps.size() > m_opt.dumpRingSize)
    {
      m_dumps.pop_front();
    }
    m_session.reportChecksum(tick, hash);
  }
  m_session.tickStepped();
  if (last)
  {
    if (m_recording)
    {
      try { m_recorder.finish(m_sim); } catch (const std::exception&) { m_recording = false; }
    }
    m_active = false;
    m_session.roundFinished();
  }
  return m_lastInputs;
}

const MatchRecord& NetMatch::record()
{
  return m_recorder.finish(m_sim);
}

std::vector<uint32_t> NetMatch::keptDumpTicks() const
{
  std::vector<uint32_t> ticks;
  for (const Dump& d : m_dumps)
  {
    ticks.push_back(d.tick);
  }
  return ticks;
}

namespace
{
bool writeText(const std::string& file, const std::string& text, std::string* error)
{
  std::ofstream out(file.c_str(), std::ios::binary);
  out << text;
  out.flush();
  if (!out)
  {
    if (error) *error = "cannot write " + file;
    return false;
  }
  return true;
}
} // namespace

bool NetMatch::writeDesyncFiles(DesyncFiles& out, std::string* error)
{
  if (m_filesWritten)
  {
    out = m_files;
    return true;
  }
  const DesyncInfo& d = m_session.desyncInfo();
  const std::string stem = "desync-r" + std::to_string(d.round) + "-t" + std::to_string(d.tick) + "-p"
                         + std::to_string((int) m_session.myId());
  DesyncFiles f;
  // The state at the tick the hashes first disagreed, if it is still in the ring.
  for (const Dump& dump : m_dumps)
  {
    if (dump.tick == d.tick)
    {
      f.dumpAtTick = path(stem + "-state.txt");
      if (!writeText(f.dumpAtTick, dump.text, error)) return false;
    }
  }
  if (m_sim.started())
  {
    std::ostringstream now;
    m_sim.dumpState(now);
    f.dumpNow = path(stem + "-now-t" + std::to_string(m_sim.tick()) + "-state.txt");
    if (!writeText(f.dumpNow, now.str(), error)) return false;
    try
    {
      f.record = path(stem + ".brmr");
      m_recorder.finish(m_sim).writeFile(f.record);
    }
    catch (const std::exception& e)
    {
      if (error) *error = std::string("cannot write the record: ") + e.what();
      return false;
    }
  }
  m_files = f;
  m_filesWritten = true;
  out = f;
  return true;
}

bool NetMatch::writeRoundRecord(std::string* file, std::string* error)
{
  try
  {
    const std::string p = path("round-" + std::to_string(m_round.round) + "-peer" + std::to_string((int) m_session.myId()) + ".brmr");
    m_recorder.finish(m_sim).writeFile(p);
    if (file) *file = p;
    return true;
  }
  catch (const std::exception& e)
  {
    if (error) *error = std::string("cannot write the record: ") + e.what();
    return false;
  }
}
} // namespace netplay
