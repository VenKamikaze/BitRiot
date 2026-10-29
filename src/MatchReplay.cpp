#include "MatchReplay.h"

#include <stdexcept>

#include "DataHash.h"
#include "Simulation.h"

namespace
{
std::string hex64(uint64_t v)
{
  static const char DIGITS[] = "0123456789abcdef";
  std::string s = "0x0000000000000000";
  for (int i = 17; i >= 2; i--)
  {
    s[i] = DIGITS[v & 0xf];
    v >>= 4;
  }
  return s;
}
} // namespace

ReplayResult replayRecord(const MatchRecord& record, Simulation& sim, const ReplayOptions& options)
{
  ReplayResult r;

  // 1. Is this record comparable with this build at all?
  try
  {
    r.actualDataHash = Simulation::dataHash();
  }
  catch (const std::exception& e)
  {
    r.status = ReplayResult::DATA_ERROR;
    r.message = e.what();
    return r;
  }
  r.expectedDataHash = record.dataHash;
  if (options.checkDataHash && record.dataHash != r.actualDataHash)
  {
    r.status = ReplayResult::DATA_HASH_MISMATCH;
    r.message = "the record was made on different game data: data hash " + hex64(record.dataHash) + ", this build has "
        + hex64(r.actualDataHash);
    return r;
  }
  if (!record.checkpoints.empty() && record.stateHashVersion != (uint32_t) Simulation::STATE_HASH_VERSION)
  {
    r.status = ReplayResult::HASH_VERSION_MISMATCH;
    r.message = "the record's checkpoints are state-hash version " + std::to_string(record.stateHashVersion)
        + ", this build writes version " + std::to_string(Simulation::STATE_HASH_VERSION);
    return r;
  }
  const std::string problem = record.config.runnableProblem();
  if (!problem.empty())
  {
    r.status = ReplayResult::BAD_CONFIG;
    r.message = "the record's config cannot be played: " + problem;
    return r;
  }

  // 2. Replay.
  try
  {
    sim.start(record.config);
  }
  catch (const std::exception& e)
  {
    r.status = ReplayResult::BAD_CONFIG;
    r.message = std::string("the record's config was refused: ") + e.what();
    return r;
  }

  size_t next = 0; // next checkpoint to verify
  bool mismatched = false;
  const uint64_t last = record.ticks.size() < options.stopAfterTick ? record.ticks.size() : options.stopAfterTick;

  // Verifies tick `t` (already stepped to) if the record has a checkpoint there, and
  // reports it to onTick. Returns false to stop.
  auto atTick = [&](uint64_t t) -> bool
  {
    const bool hasCheckpoint = next < record.checkpoints.size() && record.checkpoints[next].tick == t;
    if (!hasCheckpoint && !options.onTick)
    {
      return true;
    }
    const uint64_t hash = sim.stateHash();
    if (options.onTick)
    {
      options.onTick(t, hash);
    }
    if (hasCheckpoint)
    {
      const MatchRecord::Checkpoint& c = record.checkpoints[next++];
      if (c.hash == hash)
      {
        r.checkpointsChecked++;
        r.lastGoodTick = (int64_t) t;
      }
      else if (!mismatched)
      {
        mismatched = true;
        r.status = ReplayResult::CHECKPOINT_MISMATCH;
        r.mismatchTick = t;
        r.expectedHash = c.hash;
        r.actualHash = hash;
        r.message = "state hash differs at tick " + std::to_string(t) + ": the record has " + hex64(c.hash) + ", the replay has "
            + hex64(hash) + (r.lastGoodTick >= 0 ? " (last agreeing checkpoint: tick " + std::to_string(r.lastGoodTick) + ")"
                                                  : " (no earlier checkpoint agreed)");
        if (!options.continueAfterMismatch)
        {
          return false;
        }
      }
    }
    return true;
  };

  r.finalHash = 0;
  bool go = atTick(0);
  uint64_t t = 0;
  while (go && t < last)
  {
    t++;
    try
    {
      sim.step(record.ticks[t - 1]);
    }
    catch (const std::exception& e)
    {
      r.status = ReplayResult::SIM_ERROR;
      r.mismatchTick = t;
      r.message = "the simulation refused tick " + std::to_string(t) + ": " + e.what();
      r.ticksPlayed = t - 1;
      r.finalHash = sim.stateHash();
      r.roundOver = sim.roundOver();
      return r;
    }
    go = atTick(t);
  }
  r.ticksPlayed = t;
  r.finalHash = sim.stateHash();
  r.roundOver = sim.roundOver();

  if (r.status == ReplayResult::OK && t == record.ticks.size() && sim.roundOver() != record.roundOver)
  {
    r.status = ReplayResult::ROUND_END_MISMATCH;
    r.mismatchTick = t;
    r.message = std::string("every checkpoint agreed but the round is ") + (sim.roundOver() ? "over" : "not over")
        + " after tick " + std::to_string(t) + ", and the record says it " + (record.roundOver ? "was" : "was not");
  }
  return r;
}

MatchRecord recordRound(Simulation& sim, const MatchConfig& config, const std::vector<TickInputs>* script,
                        uint64_t maxTicks, uint32_t checkpointInterval, bool rle)
{
  MatchRecorder recorder(checkpointInterval, rle);
  sim.start(config);
  recorder.begin(sim);
  const TickInputs none;
  uint64_t t = 0;
  while (!sim.roundOver() && t < maxTicks && t < MatchRecord::MAX_TICKS)
  {
    TickInputs inputs = (script != nullptr && t < script->size()) ? (*script)[t] : none;
    // A script made for a bigger round may carry bytes for slots this round lacks;
    // the simulation ignores them and the record cannot hold them.
    for (int slot = config.numPlayers(); slot < std::GameSettings::MAX_PLAYERS; slot++)
    {
      inputs.slot[slot] = PlayerInput();
    }
    sim.step(inputs);
    recorder.onStep(inputs, sim);
    t++;
  }
  return recorder.finish(sim);
}
