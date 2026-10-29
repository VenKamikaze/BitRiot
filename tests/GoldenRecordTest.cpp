// The golden match record (brief 05): one complete round on a small real map, committed
// as tests/data/golden-round.brmr. Replaying it must reproduce the state hash of every
// tick. THIS TEST BREAKS ON PURPOSE when the simulation's behaviour changes: the record
// is the proof that it did not. tests/README.md says how to regenerate it and what the
// commit message must then say.

#include "support/GoldenScript.h"
#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

#include "DynamicMap.h"
#include "MatchRecord.h"
#include "MatchReplay.h"

using bitriot_test::goldenConfig;
using bitriot_test::goldenScript;
using bitriot_test::HeadlessGameTest;

namespace
{
const uint64_t GOLDEN_SEED = 2;
const uint64_t GOLDEN_SCRIPT_SEED = 1;
const int GOLDEN_MAX_TICKS = 8000;

std::string goldenPath()
{
  return std::string(BITRIOT_SOURCE_DIR) + "/tests/data/golden-round.brmr";
}
} // namespace

class GoldenRecordTest : public HeadlessGameTest
{
  protected:
    // Plays the golden script on the current simulation (a checkpoint every tick).
    MatchRecord playGolden()
    {
      const std::vector<TickInputs> script = goldenScript(GOLDEN_SCRIPT_SEED, GOLDEN_MAX_TICKS);
      return recordRound(sim(), goldenConfig(GOLDEN_SEED), &script, GOLDEN_MAX_TICKS, 1, true);
    }

    // Rewrites the committed file when asked to (tests/README.md, "Regenerating the
    // golden record"). The test then goes on and checks the file it just wrote.
    void regenerateIfAsked()
    {
      const char* ask = std::getenv("BITRIOT_REGEN_GOLDEN");
      if (ask != nullptr && ask[0] != '\0' && ask[0] != '0')
      {
        const MatchRecord fresh = playGolden();
        fresh.writeFile(goldenPath());
        std::cerr << "[bitriot] REGENERATED " << goldenPath() << ": " << fresh.ticks.size() << " ticks, "
                  << fresh.serialize().size() << " bytes. The commit message must say so (tests/README.md).\n";
      }
    }
};

TEST_F(GoldenRecordTest, TheGoldenRecordReplaysOnEveryTick)
{
  regenerateIfAsked();
  const MatchRecord golden = MatchRecord::readFile(goldenPath());

  uint64_t seen = 0;
  ReplayOptions options;
  options.onTick = [&seen](uint64_t, uint64_t) { seen++; };
  const ReplayResult r = replayRecord(golden, sim(), options);
  EXPECT_TRUE(r.ok()) << r.message
                      << "\nThe simulation no longer plays the golden round the way it was recorded. If that is deliberate,"
                         " regenerate the record (tests/README.md) and say so in the commit message.";
  EXPECT_EQ(r.ticksPlayed, golden.ticks.size());
  EXPECT_EQ(golden.checkpoints.size(), golden.ticks.size() + 1) << "a checkpoint on every tick, tick 0 included";
  EXPECT_EQ(r.checkpointsChecked, golden.checkpoints.size());
  EXPECT_EQ(seen, golden.ticks.size() + 1);
}

TEST_F(GoldenRecordTest, TheGoldenRecordIsOneCompleteRound)
{
  const MatchRecord golden = MatchRecord::readFile(goldenPath());
  EXPECT_TRUE(golden.roundOver) << "the round played to its natural end, countdown included";
  EXPECT_GT(golden.ticks.size(), 2000u) << "a few hundred ticks barely gets past the AI's opening WAIT";
  EXPECT_LT(golden.ticks.size(), 6000u) << "kept small enough to replay in every build";
  EXPECT_EQ(golden.config, goldenConfig(GOLDEN_SEED));
  EXPECT_EQ(golden.config.mapWidth, 13) << "a small real map (the smallest the menu offers)";
  EXPECT_EQ(golden.config.mapHeight, 11);
  EXPECT_GE(golden.config.blockPercentage, 50) << "with blocks";
  EXPECT_EQ(golden.stateHashVersion, (uint32_t) Simulation::STATE_HASH_VERSION);
  EXPECT_LT(golden.serialize().size(), 100u * 1024u) << "a small file";

  sim().start(golden.config);
  for (const TickInputs& t : golden.ticks)
  {
    EXPECT_FALSE(sim().roundOver()) << "the record goes on after the round was over";
    sim().step(t);
  }
  EXPECT_TRUE(sim().roundOver());
  EXPECT_EQ(sim().roundEndedAtTick(), golden.ticks.size()) << "the record stops on the tick the round ended";
}

TEST_F(GoldenRecordTest, TheGoldenRecordIsInItsCanonicalForm)
{
  std::ifstream in(goldenPath().c_str(), std::ios::binary);
  ASSERT_TRUE(in.is_open()) << goldenPath();
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(MatchRecord::parse(bytes).serialize(), bytes);
}

TEST_F(GoldenRecordTest, TheGoldenRecordIsWhatTheScriptPlaysToday)
{
  // The committed file's inputs are the script in tests/support/GoldenScript.h, and its
  // checkpoints are what the simulation produces from them. (The build id is the one
  // thing that differs: it names the build that wrote the file.)
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  MatchRecord fresh = playGolden();
  EXPECT_EQ(golden.ticks.size(), fresh.ticks.size());
  golden.buildId = fresh.buildId;
  EXPECT_EQ(golden, fresh);
}

TEST_F(GoldenRecordTest, TheGoldenRoundExercisesWhatItIsThereToExercise)
{
  const MatchRecord golden = MatchRecord::readFile(goldenPath());

  // What the script does with the buttons: both actions with no direction and with each
  // of the four directions, for both human players.
  std::set<int> gestures; // action * 5 + direction (0 = none)
  for (size_t i = 1; i < golden.ticks.size(); i++)
  {
    for (int slot = 0; slot < 2; slot++)
    {
      for (int action = 0; action < 2; action++)
      {
        const PlayerInput::Button button = action == 0 ? PlayerInput::ACTION1 : PlayerInput::ACTION2;
        if (!golden.ticks[i].slot[slot].released(button))
        {
          continue;
        }
        const PlayerInput before = golden.ticks[i - 1].slot[slot];
        int dir = 0;
        if (before.held(PlayerInput::UP)) { dir = 1; }
        else if (before.held(PlayerInput::DOWN)) { dir = 2; }
        else if (before.held(PlayerInput::LEFT)) { dir = 3; }
        else if (before.held(PlayerInput::RIGHT)) { dir = 4; }
        gestures.insert(action * 5 + dir);
      }
    }
  }
  EXPECT_EQ(gestures.size(), 10u) << "ACTION1 and ACTION2, each with no direction and with up, down, left and right";

  // What happens in the world, tick by tick.
  const long pathsBefore = PlayerCharacterEntity::pathSearchesRun();
  std::set<EntityType> everyone;     // every entity type seen, whoever owned it
  std::set<EntityType> humansLaid;   // what players 1 and 2 (the humans) put on the map
  int humansAliveBothUntil = 0;
  bool slot2WasHuman = false;
  bool slot2WasBotAfter = false;
  bool slot0Left = false;
  sim().start(golden.config);
  for (size_t i = 0; i < golden.ticks.size(); i++)
  {
    sim().step(golden.ticks[i]);
    const uint64_t tick = sim().tick();
    for (const auto& kv : sim().entityManager().getDynamicMap()->entityList)
    {
      const EntityType type = kv.second->getType();
      everyone.insert(type);
      if (kv.second->getPlayerNumber() == 1 || kv.second->getPlayerNumber() == 2)
      {
        humansLaid.insert(type);
      }
    }
    bool bothAlive = true;
    for (const std::shared_ptr<PlayerCharacterEntity>& p : sim().roster())
    {
      if ((p->getPlayerNumber() == 1 || p->getPlayerNumber() == 2) && !p->isAlive())
      {
        bothAlive = false;
      }
      if (p->getPlayerNumber() == 3)
      {
        if (tick >= 400 && tick < 700 && p->isAlive() && !p->isControlledByAI()) { slot2WasHuman = true; }
        if (tick >= 700 && p->isAlive() && p->isControlledByAI()) { slot2WasBotAfter = true; }
      }
      if (p->getPlayerNumber() == 1 && tick > 1500 && p->isAlive() && p->isControlledByAI()) { slot0Left = true; }
    }
    if (bothAlive) { humansAliveBothUntil = (int) tick; }
  }
  const long pathSearches = PlayerCharacterEntity::pathSearchesRun() - pathsBefore;

  if (std::getenv("BITRIOT_GOLDEN_STATS") != nullptr)
  {
    std::cerr << "[golden] ticks " << golden.ticks.size() << ", bytes " << golden.serialize().size() << ", path searches "
              << pathSearches << ", both humans alive until tick " << humansAliveBothUntil << "\n[golden] entity types seen:";
    for (EntityType t : everyone) { std::cerr << " " << (int) t; }
    std::cerr << "\n[golden] laid by the humans:";
    for (EntityType t : humansLaid) { std::cerr << " " << (int) t; }
    std::cerr << "\n";
  }

  // The ten things a human can lay, all laid by a human.
  for (EntityType t : { BLOCK, EGG1, EGG2, EGG3, EGG4, EGG5, BOMB, MINE, ROCKET_SPIN })
  {
    EXPECT_EQ(humansLaid.count(t), 1u) << "a human never laid entity type " << (int) t;
  }
  // The things those and the AI set off.
  for (EntityType t : { ROCKET_FIRED, EXPLOSION, ENERGY_DROP, EGG_THIEF, WALL_EATER, DRONE, ROBOT1, DAMAGE_EFFECT, MUNCH_EFFECT, ENPLUS_POWERUP })
  {
    EXPECT_EQ(everyone.count(t), 1u) << "the round never produced entity type " << (int) t;
  }
  EXPECT_GT(pathSearches, 50) << "the bots' A* pathfinding";
  EXPECT_GT(humansAliveBothUntil, 500) << "both humans played for a good while";
  EXPECT_TRUE(slot2WasHuman) << "the takeover at tick 400";
  EXPECT_TRUE(slot2WasBotAfter) << "the return to the AI at tick 700";
  EXPECT_TRUE(slot0Left) << "player 1 left at tick 1500 and is a bot";
}
