/*
 * MatchReplay.h
 *
 * Playing a MatchRecord back (replayRecord) and making one (recordRound), on a
 * Simulation the caller owns. This is what bitriot_headless's `replay`, `run` and
 * `dump` commands are made of, and what the tests, the determinism gate (05b) and
 * the hot-join fast-forward (13) call. It needs the simulation library and nothing
 * else.
 */

#ifndef _MATCHREPLAY_H
#define _MATCHREPLAY_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "MatchRecord.h"

class Simulation;

struct ReplayOptions
{
  // Refuse (DATA_HASH_MISMATCH) a record made on different game data. Off only to
  // see how far a mismatched replay gets.
  bool checkDataHash = true;

  // Stop after this many ticks (the `dump` command: replay to tick N, then look).
  // Checkpoints up to there are still verified. The default plays the whole record.
  uint64_t stopAfterTick = UINT64_MAX;

  // Called with (tick, stateHash) after tick 0 and every step, so a tool can print
  // every tick's hash. Taking the hash of every tick is slower than checkpoints only.
  std::function<void(uint64_t, uint64_t)> onTick;

  // Keep replaying after a checkpoint mismatch (to list them all); the result still
  // reports the first.
  bool continueAfterMismatch = false;
};

struct ReplayResult
{
  enum Status
  {
    OK = 0,
    BAD_CONFIG,           // the record's MatchConfig cannot be played
    DATA_ERROR,           // the simulation's data files cannot be loaded
    DATA_HASH_MISMATCH,   // made on different game data: not comparable
    HASH_VERSION_MISMATCH,// the checkpoints are of another state-hash version: not comparable
    CHECKPOINT_MISMATCH,  // a checkpoint's hash differs from the replay's
    SIM_ERROR,            // the simulation refused a tick (e.g. an event for a slot the round lacks)
    ROUND_END_MISMATCH    // every checkpoint agreed but the round's end (roundOver) differs from the record's
  };

  Status status = OK;
  std::string message;           // one line saying what happened; empty for OK

  uint64_t ticksPlayed = 0;      // steps taken
  uint64_t checkpointsChecked = 0;
  uint64_t finalHash = 0;        // stateHash() after the last step played
  bool roundOver = false;        // Simulation::roundOver() after the last step played

  // For CHECKPOINT_MISMATCH: the first checkpoint that differs. The replay diverged
  // from the record somewhere in the ticks (lastGoodTick, mismatchTick]; with a
  // checkpoint every tick that is exactly one tick. lastGoodTick is -1 if no
  // checkpoint was verified before it. For SIM_ERROR mismatchTick is the tick being
  // stepped.
  uint64_t mismatchTick = 0;
  int64_t lastGoodTick = -1;
  uint64_t expectedHash = 0;     // the record's
  uint64_t actualHash = 0;       // this build's

  // For DATA_HASH_MISMATCH.
  uint64_t expectedDataHash = 0;
  uint64_t actualDataHash = 0;

  bool ok() const { return status == OK; }
};

// Starts `sim` from record.config and steps it through the record's ticks, comparing
// the state hash with each checkpoint (tick 0 included). Never throws for a bad record
// (that is a status); the simulation is left at the last tick played.
ReplayResult replayRecord(const MatchRecord& record, Simulation& sim, const ReplayOptions& options = ReplayOptions());

// Starts `sim` from `config` and plays a round, recording it. `script` (may be null)
// supplies the TickInputs of ticks 1..n; after it runs out, ticks are empty (AI only,
// no events). Stops when the round is over (the countdown included) or after maxTicks.
// Throws what Simulation::start() / step() throw (a bad config; a script event for a
// slot the round lacks).
MatchRecord recordRound(Simulation& sim, const MatchConfig& config, const std::vector<TickInputs>* script,
                        uint64_t maxTicks, uint32_t checkpointInterval, bool rle);

#endif /* _MATCHREPLAY_H */
