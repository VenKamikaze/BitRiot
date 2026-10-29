// The recorder and the replayer (brief 05): a round is recorded as it is played and
// played back to the same state hash on every tick; every way a replay can disagree is
// reported, and names the tick.

#include "support/GoldenScript.h"
#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include <vector>

#include "DataHash.h"
#include "MatchRecord.h"
#include "MatchReplay.h"

using bitriot_test::goldenConfig;
using bitriot_test::goldenScript;
using bitriot_test::HeadlessGameTest;

class ReplayTest : public HeadlessGameTest
{
  protected:
    // Plays the golden round's script for `ticks` ticks (or until the round ends)
    // and returns the hash after every tick (index 0 is tick 0) alongside the record.
    struct Played
    {
      MatchRecord record;
      std::vector<uint64_t> hashes;
    };

    Played play(uint64_t seed, int ticks, uint32_t interval, bool rle = true)
    {
      Played p;
      const std::vector<TickInputs> script = goldenScript(1, ticks);
      MatchRecorder recorder(interval, rle);
      sim().start(goldenConfig(seed));
      recorder.begin(sim());
      p.hashes.push_back(sim().stateHash());
      for (int t = 0; t < ticks && !sim().roundOver(); t++)
      {
        sim().step(script[t]);
        recorder.onStep(script[t], sim());
        p.hashes.push_back(sim().stateHash());
      }
      p.record = recorder.finish(sim());
      return p;
    }

    // A tick of the first human, between 100 and 390, on which it does nothing with the
    // buttons and neither did it the tick before: pressing ACTION2 there starts a hold,
    // which is in the state hash whatever the map around the player looks like.
    static size_t anIdleTick(const MatchRecord& r)
    {
      for (size_t i = 100; i < 390; i++)
      {
        const uint8_t actions = PlayerInput::heldBit(PlayerInput::ACTION1) | PlayerInput::heldBit(PlayerInput::ACTION2)
            | PlayerInput::ACTION1_RELEASED | PlayerInput::ACTION2_RELEASED;
        if ((r.ticks[i].slot[0].bits & actions) == 0 && (r.ticks[i - 1].slot[0].bits & actions) == 0)
        {
          return i;
        }
      }
      return 0;
    }

    static void pressActionTwo(MatchRecord& r, size_t index)
    {
      r.ticks[index].slot[0].setHeld(PlayerInput::ACTION2);
    }

    ReplayResult replay(const MatchRecord& r, const ReplayOptions& options = ReplayOptions())
    {
      return replayRecord(r, sim(), options);
    }
};

// ---------------------------------------------------------------------------
// Record, then replay
// ---------------------------------------------------------------------------

TEST_F(ReplayTest, ASeededRoundWithScriptedHumansIsRecordedAndReplaysOnEveryTick)
{
  const Played played = play(21, 700, 1);
  ASSERT_EQ(played.record.ticks.size(), 700u);

  // Replay it, comparing the hash of EVERY tick with the one taken while playing.
  std::vector<uint64_t> replayed;
  ReplayOptions options;
  options.onTick = [&replayed](uint64_t tick, uint64_t hash)
  {
    EXPECT_EQ(tick, replayed.size());
    replayed.push_back(hash);
  };
  const ReplayResult r = replay(played.record, options);
  EXPECT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(replayed, played.hashes);
  EXPECT_EQ(r.ticksPlayed, 700u);
  EXPECT_EQ(r.checkpointsChecked, 701u) << "tick 0 and a checkpoint on every tick";
  EXPECT_EQ(r.finalHash, played.hashes.back());
}

TEST_F(ReplayTest, TheSameRoundReplaysFromTheBytes)
{
  const Played played = play(22, 500, 30);
  const std::vector<uint8_t> bytes = played.record.serialize();
  const MatchRecord loaded = MatchRecord::parse(bytes);
  EXPECT_EQ(loaded.serialize(), bytes);
  const ReplayResult r = replay(loaded);
  EXPECT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.finalHash, played.hashes.back());
}

TEST_F(ReplayTest, ARecordedRoundCarriesControlEventsAndHumanInputThatDecideTheRound)
{
  // The golden script has a takeover, a return and a leave among its inputs. Each of
  // those is a difference in the round: leave one out of the replay and it diverges.
  const Played played = play(23, 900, 1);
  int events = 0;
  for (const TickInputs& t : played.record.ticks) { events += (int) t.control.size(); }
  EXPECT_EQ(events, 2) << "the takeover at tick 400 and the return at 700 (the leave is at tick 1500)";

  MatchRecord stripped = played.record;
  for (TickInputs& t : stripped.ticks) { t.control.clear(); }
  EXPECT_FALSE(replay(stripped).ok());
  MatchRecord idle = played.record;
  for (TickInputs& t : idle.ticks) { t.slot = {}; }
  EXPECT_FALSE(replay(idle).ok());
}

TEST_F(ReplayTest, RecordingDoesNotChangeTheRound)
{
  // Hashing at a checkpoint reads the simulation and nothing else: a round stepped with
  // no recorder, and hashed only at the end, ends in the state a recorded one (hashed
  // every tick) does.
  const std::vector<TickInputs> script = goldenScript(1, 600);
  sim().start(goldenConfig(24));
  for (int t = 0; t < 600; t++) { sim().step(script[t]); }
  const uint64_t quiet = sim().stateHash();

  const Played loud = play(24, 600, 1);
  EXPECT_EQ(loud.hashes.back(), quiet);
  EXPECT_EQ(loud.record.checkpoints.back().hash, quiet);
}

TEST_F(ReplayTest, CheckpointsAreTakenAtTickZeroEveryIntervalAndAtTheEnd)
{
  {
    const Played p = play(25, 100, 30);
    std::vector<uint32_t> ticks;
    for (const MatchRecord::Checkpoint& c : p.record.checkpoints) { ticks.push_back(c.tick); }
    EXPECT_EQ(ticks, (std::vector<uint32_t>{ 0, 30, 60, 90, 100 }));
    EXPECT_EQ(p.record.checkpointInterval, 30u);
    for (const MatchRecord::Checkpoint& c : p.record.checkpoints)
    {
      EXPECT_EQ(c.hash, p.hashes[c.tick]);
    }
  }
  {
    const Played p = play(25, 90, 30);
    EXPECT_EQ(p.record.checkpoints.size(), 4u) << "the last tick is a multiple: no second checkpoint there";
  }
  {
    const Played p = play(25, 50, 0);
    std::vector<uint32_t> ticks;
    for (const MatchRecord::Checkpoint& c : p.record.checkpoints) { ticks.push_back(c.tick); }
    EXPECT_EQ(ticks, (std::vector<uint32_t>{ 0, 50 })) << "interval 0: only the first and the last";
  }
}

TEST_F(ReplayTest, TheRecordHoldsTheBuildTheDataAndTheConfig)
{
  const Played p = play(26, 40, 10);
  EXPECT_EQ(p.record.buildId, Simulation::buildId());
  EXPECT_NE(p.record.buildId, 0u);
  EXPECT_EQ(p.record.dataHash, Simulation::dataHash());
  EXPECT_EQ(p.record.stateHashVersion, (uint32_t) Simulation::STATE_HASH_VERSION);
  EXPECT_EQ(p.record.config, goldenConfig(26));
  EXPECT_FALSE(p.record.roundOver);
}

TEST_F(ReplayTest, ARoundThatEndsIsMarkedAsOverAndReplaysToItsEnd)
{
  MatchConfig c = goldenConfig(27);
  c.mapWidth = 7; // a tiny map: the bots meet quickly
  c.mapHeight = 7;
  c.blockPercentage = 0;
  c.slots[0].ai = true;
  c.slots[1].ai = true;
  const MatchRecord r = recordRound(sim(), c, nullptr, 20000, 30, true);
  ASSERT_TRUE(r.roundOver);
  EXPECT_TRUE(sim().roundOver());
  EXPECT_EQ(r.ticks.size(), sim().roundEndedAtTick());
  const ReplayResult again = replay(r);
  EXPECT_TRUE(again.ok()) << again.message;
  EXPECT_TRUE(again.roundOver);
}

TEST_F(ReplayTest, TheRecorderRefusesToGetOutOfStepWithTheSimulation)
{
  MatchRecorder recorder(30, true);
  EXPECT_THROW(recorder.onStep(TickInputs(), sim()), std::logic_error) << "before begin()";
  sim().start(goldenConfig(28));
  recorder.begin(sim());
  sim().step();
  sim().step();
  EXPECT_THROW(recorder.onStep(TickInputs(), sim()), std::logic_error) << "two steps, one recorded";
  EXPECT_THROW(recorder.finish(sim()), std::logic_error);
  MatchRecorder late(30, true);
  EXPECT_THROW(late.begin(sim()), std::logic_error) << "begin() is for straight after start()";
}

// ---------------------------------------------------------------------------
// Mismatch detection
// ---------------------------------------------------------------------------

TEST_F(ReplayTest, ACorruptedInputIsNamedAtItsTickWhenEveryTickIsACheckpoint)
{
  const Played p = play(31, 400, 1);
  const size_t victim = anIdleTick(p.record);
  ASSERT_GT(victim, 0u);
  MatchRecord broken = p.record;
  pressActionTwo(broken, victim);

  const ReplayResult r = replay(broken);
  ASSERT_EQ(r.status, ReplayResult::CHECKPOINT_MISMATCH) << r.message;
  EXPECT_EQ(r.mismatchTick, (uint64_t) victim + 1) << "ticks[i] is tick i + 1";
  EXPECT_EQ(r.lastGoodTick, (int64_t) victim);
  EXPECT_EQ(r.expectedHash, p.hashes[victim + 1]);
  EXPECT_NE(r.actualHash, r.expectedHash);
  EXPECT_NE(r.message.find("tick " + std::to_string(victim + 1)), std::string::npos) << r.message;
  EXPECT_EQ(r.ticksPlayed, (uint64_t) victim + 1) << "the replay stops at the first mismatch";
}

TEST_F(ReplayTest, WithCoarserCheckpointsTheDivergenceIsPinnedToTheWindow)
{
  const Played p = play(32, 400, 30);
  const size_t victim = anIdleTick(p.record);
  ASSERT_GT(victim, 0u);
  MatchRecord broken = p.record;
  pressActionTwo(broken, victim);

  const ReplayResult r = replay(broken);
  ASSERT_EQ(r.status, ReplayResult::CHECKPOINT_MISMATCH) << r.message;
  const uint64_t tickOfVictim = victim + 1;
  const uint64_t firstCheckpointAfter = ((tickOfVictim + 29) / 30) * 30;
  EXPECT_EQ(r.mismatchTick, firstCheckpointAfter);
  EXPECT_EQ(r.lastGoodTick, (int64_t) (firstCheckpointAfter - 30));
  EXPECT_GT(tickOfVictim, (uint64_t) r.lastGoodTick);
  EXPECT_LE(tickOfVictim, r.mismatchTick) << "the corrupted tick is inside the reported window";
}

TEST_F(ReplayTest, ACorruptedCheckpointIsNamed)
{
  const Played p = play(33, 200, 10);
  MatchRecord broken = p.record;
  broken.checkpoints[5].hash ^= 1; // tick 50
  const ReplayResult r = replay(broken);
  ASSERT_EQ(r.status, ReplayResult::CHECKPOINT_MISMATCH);
  EXPECT_EQ(r.mismatchTick, 50u);
  EXPECT_EQ(r.lastGoodTick, 40);
  EXPECT_EQ(r.actualHash, p.hashes[50]);
}

TEST_F(ReplayTest, ADifferentSeedDisagreesAtTickZero)
{
  Played p = play(34, 60, 10);
  p.record.config.seed = 35;
  const ReplayResult r = replay(p.record);
  ASSERT_EQ(r.status, ReplayResult::CHECKPOINT_MISMATCH);
  EXPECT_EQ(r.mismatchTick, 0u);
  EXPECT_EQ(r.lastGoodTick, -1);
}

TEST_F(ReplayTest, KeepingGoingFindsLaterMismatchesButStillReportsTheFirst)
{
  const Played p = play(36, 400, 10);
  MatchRecord broken = p.record;
  const size_t victim = anIdleTick(p.record);
  ASSERT_GT(victim, 0u);
  pressActionTwo(broken, victim);
  ReplayOptions options;
  options.continueAfterMismatch = true;
  const ReplayResult r = replay(broken, options);
  EXPECT_EQ(r.status, ReplayResult::CHECKPOINT_MISMATCH);
  EXPECT_EQ(r.ticksPlayed, 400u) << "it played to the end of the record";
  EXPECT_GT(r.mismatchTick, victim);
  EXPECT_LE(r.mismatchTick, victim + 10);
}

TEST_F(ReplayTest, ARecordMadeOnOtherDataIsRefusedAsDataNotAsAMismatch)
{
  Played p = play(37, 40, 10);
  p.record.dataHash ^= 1;
  const ReplayResult r = replay(p.record);
  EXPECT_EQ(r.status, ReplayResult::DATA_HASH_MISMATCH);
  EXPECT_EQ(r.expectedDataHash, p.record.dataHash);
  EXPECT_EQ(r.actualDataHash, Simulation::dataHash());
  EXPECT_EQ(r.ticksPlayed, 0u) << "nothing was played";

  ReplayOptions ignore;
  ignore.checkDataHash = false;
  EXPECT_TRUE(replay(p.record, ignore).ok());
}

TEST_F(ReplayTest, CheckpointsOfAnotherHashVersionAreNotComparable)
{
  Played p = play(38, 40, 10);
  p.record.stateHashVersion = 1;
  EXPECT_EQ(replay(p.record).status, ReplayResult::HASH_VERSION_MISMATCH);
  p.record.checkpoints.clear(); // nothing to compare: fine
  EXPECT_TRUE(replay(p.record).ok());
}

TEST_F(ReplayTest, AConfigThatCannotBePlayedIsReportedNotThrown)
{
  Played p = play(39, 10, 5);
  p.record.config.slots.resize(1);
  p.record.ticks.clear();
  p.record.checkpoints.clear();
  EXPECT_EQ(replay(p.record).status, ReplayResult::BAD_CONFIG);
}

TEST_F(ReplayTest, AnEventForASlotTheRoundLacksIsASimulationErrorAtThatTick)
{
  Played p = play(40, 60, 10);
  p.record.ticks[44].control.push_back(ControlEvent(9, ControlEvent::LEAVE));
  const ReplayResult r = replay(p.record);
  ASSERT_EQ(r.status, ReplayResult::SIM_ERROR);
  EXPECT_EQ(r.mismatchTick, 45u);
  EXPECT_EQ(r.ticksPlayed, 44u);
  EXPECT_NE(r.message.find("tick 45"), std::string::npos) << r.message;
}

TEST_F(ReplayTest, ARoundThatEndsWhenTheRecordSaysItDidNotIsCaught)
{
  MatchConfig c = goldenConfig(41);
  c.mapWidth = 7;
  c.mapHeight = 7;
  c.blockPercentage = 0;
  c.slots[0].ai = true;
  c.slots[1].ai = true;
  MatchRecord r = recordRound(sim(), c, nullptr, 20000, 0, true);
  ASSERT_TRUE(r.roundOver);
  r.roundOver = false;
  EXPECT_EQ(replay(r).status, ReplayResult::ROUND_END_MISMATCH);
}

TEST_F(ReplayTest, ReplayCanStopPartWayForADump)
{
  const Played p = play(42, 200, 10);
  ReplayOptions options;
  options.stopAfterTick = 120;
  const ReplayResult r = replay(p.record, options);
  EXPECT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.ticksPlayed, 120u);
  EXPECT_EQ(sim().tick(), 120u);
  EXPECT_EQ(sim().stateHash(), p.hashes[120]);
  EXPECT_EQ(r.checkpointsChecked, 13u);
}

// ---------------------------------------------------------------------------
// recordRound: the script is a record
// ---------------------------------------------------------------------------

TEST_F(ReplayTest, ARecordIsAScriptForAnotherRun)
{
  const Played p = play(43, 300, 30);
  // Play the same config again from the record's own inputs: the record comes out the
  // same (checkpoints included) apart from nothing at all.
  const MatchRecord again = recordRound(sim(), p.record.config, &p.record.ticks, 300, 30, true);
  EXPECT_EQ(again, p.record);
}

TEST_F(ReplayTest, ARunPastTheEndOfItsScriptPlaysOnWithNoInput)
{
  const std::vector<TickInputs> short_script = goldenScript(1, 50);
  const MatchRecord r = recordRound(sim(), goldenConfig(44), &short_script, 200, 30, true);
  EXPECT_EQ(r.ticks.size(), 200u);
  for (size_t i = 50; i < r.ticks.size(); i++)
  {
    EXPECT_EQ(r.ticks[i].slot[0].bits, 0) << "tick " << i + 1;
    EXPECT_TRUE(r.ticks[i].control.empty());
  }
  EXPECT_TRUE(replay(r).ok());
}

TEST_F(ReplayTest, AScriptMadeForABiggerRoundHasItsExtraSlotsDroppedNotRefused)
{
  std::vector<TickInputs> script(10);
  script[3].slot[5] = PlayerInput(0xFF); // a sixth player this 4-player round does not have
  const MatchRecord r = recordRound(sim(), goldenConfig(45), &script, 10, 5, false);
  EXPECT_EQ(r.ticks.size(), 10u);
  EXPECT_NO_THROW(r.serialize());
}
