// Simulation, MatchConfig and the state hash (netplay brief 03a).
//
// The harness now starts every round through Simulation::start() and
// runRoundToCompletion() steps it with Simulation::step(), exactly what
// GameEngine does, so these tests are about that shared path: what a config
// builds, what a tick does, when a round is over, that a round is a pure function
// of its config, and what stateHash() does and does not notice.

#include "support/HeadlessGame.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <vector>

#include "AIGameView.h"
#include "EntityFactory.h"
#include "EntityMessageQueue.h"
#include "GameOverTimer.h"
#include "GameSettings.h"
#include "Map.h"
#include "MatchConfig.h"
#include "SimRandom.h"
#include "Simulation.h"
#include "StateHash.h"

namespace bitriot_test
{
namespace
{

// Pointers to protected members, so a test can change exactly one field of an
// entity and ask whether the hash noticed (the same trick HeadlessGame.cpp uses
// for health).
struct Access : public PlayerCharacterEntity
{
  static int GameEntity::* health() { return &Access::m_health; }
  static int GameEntity::* animState() { return &Access::m_currentAnimState; }
  static int GameEntity::* animFrame() { return &Access::m_currentAnimFrame; }
  static fixed_t GameEntity::* velocity() { return &Access::m_velocity; }
  static int GameEntity::* owner() { return &Access::m_playerNumber; }
  static bool GameEntity::* walkThroughBlocks() { return &Access::m_walkThroughBlocks; }
  static int PlayerCharacterEntity::* iq() { return &Access::m_IQ; }
  static int PlayerCharacterEntity::* animCounter() { return &Access::m_animCounter; }
};

MatchConfig makeConfig(uint64_t seed, int players, int width, int height, int blockPercentage,
                       bool npcSpawner = true)
{
  MatchConfig c;
  c.seed = seed;
  c.mapWidth = width;
  c.mapHeight = height;
  c.blockPercentage = blockPercentage;
  c.npcSpawnerEnabled = npcSpawner;
  for (int i = 0; i < players; i++)
  {
    MatchConfig::Slot slot;
    slot.team = i;
    slot.ai = true;
    c.slots.push_back(slot);
  }
  return c;
}

// FNV-1a over 64-bit values, to fingerprint a list of per-tick hashes.
uint64_t digestOf(const std::vector<uint64_t>& values)
{
  StateHasher h;
  for (size_t i = 0; i < values.size(); i++)
  {
    h.u64(values[i]);
  }
  return h.value();
}

class SimulationTest : public HeadlessGameTest
{
  protected:
    // Resizes the Map the way the engine does before a round, then starts it.
    void startConfig(const MatchConfig& config)
    {
      setMapSize(config.mapWidth, config.mapHeight);
      sim().start(config);
    }

    // Starts the config and records the state hash at tick 0 and after each of
    // up to `ticks` steps (fewer if the round ends).
    std::vector<uint64_t> playHashes(const MatchConfig& config, int ticks, bool resizeMap = true)
    {
      if (resizeMap)
      {
        setMapSize(config.mapWidth, config.mapHeight);
      }
      sim().start(config);
      std::vector<uint64_t> hashes;
      hashes.push_back(sim().stateHash());
      for (int i = 0; i < ticks && !sim().roundOver(); i++)
      {
        sim().step();
        hashes.push_back(sim().stateHash());
      }
      return hashes;
    }

    // Two human players who never move (no input is ever fed), no monsters: a
    // world in which only what a test changes changes.
    void startStillWorld()
    {
      configurePlayers(2);
      std::GameSettings::getInstance()->setPlayerAI(0, false);
      std::GameSettings::getInstance()->setPlayerAI(1, false);
      std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
      startRound();
    }
};

// ===========================================================================
// MatchConfig
// ===========================================================================

TEST_F(SimulationTest, ConfigCapturesTheMatchSettingsAndAppliesThemBack)
{
  std::GameSettings* gs = std::GameSettings::getInstance();
  configurePlayers(5);
  gs->setPlayerTeam(0, 0);
  gs->setPlayerTeam(1, 0);
  gs->setPlayerTeam(2, 1);
  gs->setPlayerTeam(3, 1);
  gs->setPlayerTeam(4, 2);
  gs->setPlayerAI(2, false);
  gs->setBlockSpawnPercentage(33);
  gs->setNpcSpawnerEnabled(false);
  setMapSize(21, 15);

  const MatchConfig config = MatchConfig::fromSettings(*gs);
  EXPECT_EQ(config.numPlayers(), 5);
  EXPECT_EQ(config.mapWidth, 21);
  EXPECT_EQ(config.mapHeight, 15);
  EXPECT_EQ(config.blockPercentage, 33);
  EXPECT_FALSE(config.npcSpawnerEnabled);
  ASSERT_EQ(config.slots.size(), 5u);
  EXPECT_EQ(config.slots[1].team, 0);
  EXPECT_EQ(config.slots[3].team, 1);
  EXPECT_EQ(config.slots[4].team, 2);
  EXPECT_FALSE(config.slots[2].ai);
  EXPECT_TRUE(config.slots[0].ai);

  // Scramble the settings, then put the snapshot back (what a netplay client
  // does around a match).
  configurePlayers(3);
  gs->setBlockSpawnPercentage(75);
  gs->setNpcSpawnerEnabled(true);
  config.applyTo(*gs);

  const MatchConfig again = MatchConfig::fromSettings(*gs);
  EXPECT_TRUE(again == config);
}

TEST_F(SimulationTest, StartAppliesTheConfigIntoGameSettings)
{
  // The sim keeps reading GameSettings (teams on every collision, the spawner
  // toggle, the map size), so start() has to put the config there.
  std::GameSettings* gs = std::GameSettings::getInstance();
  MatchConfig config = makeConfig(777, 4, 13, 11, 20, /*npcSpawner=*/false);
  config.slots[0].team = 5;
  config.slots[1].team = 5;
  config.slots[2].ai = false;

  // Settings that disagree with the config in every respect.
  configurePlayers(2);
  gs->setNpcSpawnerEnabled(true);
  gs->setBlockSpawnPercentage(70);
  setMapSize(13, 11); // the Map is the caller's job (it needs the primary surface)

  sim().start(config);

  EXPECT_EQ(gs->getNumberOfPlayers(), 4);
  EXPECT_FALSE(gs->isNpcSpawnerEnabled());
  EXPECT_EQ(gs->getBlockSpawnPercentage(), 20);
  EXPECT_EQ(gs->getMapWidth(), 13);
  EXPECT_EQ(gs->getMapHeight(), 11);
  EXPECT_EQ(gs->getRoundSeed(), 777u);
  EXPECT_EQ(gs->teamNumberForPlayer(1), 6);
  EXPECT_EQ(gs->teamNumberForPlayer(2), 6);
  EXPECT_EQ(gs->teamNumberForPlayer(3), 3);
  EXPECT_TRUE(gs->getPlayerAIs()->at(0));
  EXPECT_FALSE(gs->getPlayerAIs()->at(2));
  EXPECT_FALSE(player(3)->isControlledByAI());
  EXPECT_TRUE(player(1)->isControlledByAI());
}

TEST_F(SimulationTest, AConfigThatCannotBePlayedIsRefused)
{
  setMapSize(17, 13);
  MatchConfig oneShort = makeConfig(1, 2, 17, 13, 0);
  oneShort.slots.pop_back();
  EXPECT_FALSE(oneShort.problem().empty());
  EXPECT_THROW(sim().start(oneShort), std::invalid_argument);

  MatchConfig tooMany = makeConfig(1, 7, 17, 13, 0);
  EXPECT_FALSE(tooMany.problem().empty());
  EXPECT_THROW(sim().start(tooMany), std::invalid_argument);

  MatchConfig negativeTeam = makeConfig(1, 3, 17, 13, 0);
  negativeTeam.slots[1].team = -1;
  EXPECT_FALSE(negativeTeam.problem().empty());
  EXPECT_THROW(sim().start(negativeTeam), std::invalid_argument);

  MatchConfig tiny = makeConfig(1, 2, 3, 13, 0);
  EXPECT_FALSE(tiny.problem().empty());
  EXPECT_THROW(sim().start(tiny), std::invalid_argument);

  EXPECT_TRUE(makeConfig(1, 2, 17, 13, 0).problem().empty());
  EXPECT_TRUE(makeConfig(1, 6, 33, 25, 100).problem().empty());
}

TEST_F(SimulationTest, StartRefusesAMapThatWasNotResizedFirst)
{
  // Rebuilding the Map needs the primary SDL surface (until brief 03b), so the
  // caller resizes it. A forgotten resize must be loud, not a silent mismatch.
  setMapSize(17, 13);
  EXPECT_THROW(sim().start(makeConfig(1, 2, 21, 15, 0)), std::runtime_error);
}

// ===========================================================================
// One Simulation per process
// ===========================================================================

TEST_F(SimulationTest, OnlyOneSimulationMayExistPerProcess)
{
  startRound();
  ASSERT_EQ(Simulation::instancesAlive(), 1);
  EXPECT_THROW(Simulation second, std::logic_error);
  EXPECT_EQ(Simulation::instancesAlive(), 1) << "a refused second Simulation must not count";
}

TEST_F(SimulationTest, TheSlotIsFreeAgainOnceTheSimulationIsGone)
{
  startRound();
  discardSimulation();
  EXPECT_EQ(Simulation::instancesAlive(), 0);
  {
    Simulation another;
    EXPECT_EQ(Simulation::instancesAlive(), 1);
    another.start(MatchConfig::fromSettings(*std::GameSettings::getInstance()));
    EXPECT_TRUE(another.started());
  }
  EXPECT_EQ(Simulation::instancesAlive(), 0);
}

TEST_F(SimulationTest, SteppingOrHashingBeforeStartIsAnError)
{
  Simulation& fresh = sim();
  ASSERT_FALSE(fresh.started());
  EXPECT_THROW(fresh.step(), std::logic_error);
  EXPECT_THROW(fresh.stateHash(), std::logic_error);
}

// ===========================================================================
// The world start() builds
// ===========================================================================

TEST_F(SimulationTest, StartBuildsOnePlayerPerSlotOnItsFixedStartTile)
{
  // The harness used to carry its own copy of these tiles.
  const int w = 17;
  const int h = 13;
  startConfig(makeConfig(5, 6, w, h, 0));

  const int expected[6][2] = {{1, 1}, {w - 2, h - 2}, {w - 2, 1}, {1, h - 2}, {w / 2, 1}, {w / 2, h - 2}};
  for (int i = 1; i <= 6; i++)
  {
    ASSERT_NE(player(i), nullptr) << "player " << i;
    EXPECT_EQ(player(i)->getTileX(), expected[i - 1][0]) << "player " << i;
    EXPECT_EQ(player(i)->getTileY(), expected[i - 1][1]) << "player " << i;
    EXPECT_EQ(player(i)->getID(), i) << "entity ids restart at 1 and players come first";
  }
  EXPECT_EQ(countAllEntities(), 6) << "0% blocks: nothing but the players";
  EXPECT_EQ((int) sim().roster().size(), 6);
}

TEST_F(SimulationTest, StartLeavesTheTickAtZeroAndTheRoundRunning)
{
  startRound();
  EXPECT_EQ(sim().tick(), 0u);
  EXPECT_FALSE(sim().roundOver());
  EXPECT_EQ(sim().roundEndedAtTick(), 0u);
  EXPECT_EQ(sim().winner(), nullptr);

  sim().step();
  sim().step();
  EXPECT_EQ(sim().tick(), 2u);

  startRound();
  EXPECT_EQ(sim().tick(), 0u) << "each start() begins a new round";
}

// A tile a block may be put on, by the game's own rule: inside the walls, not in
// a player's start corner, not on a wall pillar.
bool eligibleForBlock(int x, int y, int w, int h)
{
  if (x < 1 || y < 1 || x > w - 2 || y > h - 2) return false;
  if (x < 4 && y < 4) return false;
  if (x < 4 && y > h - 5) return false;
  if (x > w - 5 && y < 4) return false;
  if (x > w - 5 && y > h - 5) return false;
  return Map::getInstance()->staticTileAt(x, y) != Map::WALL;
}

TEST_F(SimulationTest, RealRoundsSeedBlocksAccordingToTheConfig)
{
  const int sizes[][2] = {{17, 13}, {25, 19}, {13, 11}};
  for (const auto& size : sizes)
  {
    const int w = size[0];
    const int h = size[1];

    // 100%: every eligible tile, and only those.
    startConfig(makeConfig(11, 4, w, h, 100));
    int eligible = 0;
    int blockTiles = 0;
    for (int x = 0; x < w; x++)
    {
      for (int y = 0; y < h; y++)
      {
        const bool isEligible = eligibleForBlock(x, y, w, h);
        const bool isBlock = Map::getInstance()->staticTileAt(x, y) == Map::BLOCK;
        eligible += isEligible ? 1 : 0;
        blockTiles += isBlock ? 1 : 0;
        EXPECT_EQ(isBlock, isEligible) << "100% blocks at (" << x << "," << y << ") on " << w << "x" << h;
      }
    }
    EXPECT_GT(eligible, 20);
    EXPECT_EQ(countEntities(BLOCK_GROUP), blockTiles) << "a block entity per block tile";

    // 0%: none.
    startConfig(makeConfig(11, 4, w, h, 0));
    EXPECT_EQ(countEntities(BLOCK_GROUP), 0);

    // The game's default, 55%: about that share of the eligible tiles, never
    // outside the eligible ones, each tile's block entity matching the map.
    startConfig(makeConfig(11, 4, w, h, 55));
    int placed = 0;
    for (int x = 0; x < w; x++)
    {
      for (int y = 0; y < h; y++)
      {
        const bool isBlock = Map::getInstance()->staticTileAt(x, y) == Map::BLOCK;
        if (isBlock)
        {
          placed++;
          EXPECT_TRUE(eligibleForBlock(x, y, w, h)) << "block on an ineligible tile (" << x << "," << y << ")";
        }
      }
    }
    EXPECT_EQ(countEntities(BLOCK_GROUP), placed);
    EXPECT_GT(placed, eligible * 35 / 100) << placed << " of " << eligible << " eligible tiles at 55%";
    EXPECT_LT(placed, eligible * 75 / 100) << placed << " of " << eligible << " eligible tiles at 55%";
  }
}

TEST_F(SimulationTest, BlocksBelongToTheTeamOfTheirCorner)
{
  startConfig(makeConfig(21, 4, 25, 19, 100));
  DynamicMap* map = em().getDynamicMap();
  int checked = 0;
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    GameEntity* block = it->second.get();
    if (block->getGroupType() != BLOCK_GROUP) continue;
    const int x = block->getTileX();
    const int y = block->getTileY();
    int team = 0;
    if (x < 6 && y < 5) team = 1;
    if (x >= 25 - 6 && y >= 19 - 5) team = 2;
    if (x >= 25 - 6 && y < 5) team = 3;
    if (x < 6 && y >= 19 - 5) team = 4;
    EXPECT_EQ(block->getPlayerNumber(), team) << "block at (" << x << "," << y << ")";
    checked++;
  }
  EXPECT_GT(checked, 100);
}

TEST_F(SimulationTest, ARealRoundOnARealMapPlaysToItsNaturalEnd)
{
  // The game's default block density on the game's default map, two bots: blocks
  // everywhere, the spawning pool on, the countdown and all.
  startConfig(makeConfig(1234, 2, 17, 13, 55));
  const RoundOutcome outcome = runRoundToCompletion(60000);
  EXPECT_TRUE(outcome.ended) << outcome.describe();
  EXPECT_GT(outcome.frames, 0) << outcome.describe();
  EXPECT_LE(outcome.aliveTeams, 1) << outcome.describe();
  EXPECT_TRUE(sim().roundOver());
}

// ===========================================================================
// Round-end semantics
// ===========================================================================

TEST_F(SimulationTest, TheRoundEndsASixtyTickCountdownAfterTheConditionFirstHolds)
{
  startStillWorld();
  const int n = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;
  ASSERT_EQ(n, 60);

  for (int i = 0; i < 10; i++)
  {
    sim().step();
  }
  ASSERT_EQ(sim().tick(), 10u);
  ASSERT_FALSE(sim().roundOver());
  EXPECT_FALSE(std::GameOverTimer::getInstance()->isTimerTriggered());

  destroyPlayer(1); // between ticks: from the next tick on one team is left

  // Tick 11 is the first tick on which the end condition holds (t0). It starts
  // the countdown and the round goes on...
  sim().step();
  ASSERT_EQ(sim().tick(), 11u);
  EXPECT_TRUE(std::GameOverTimer::getInstance()->isTimerTriggered());
  EXPECT_EQ(std::GameOverTimer::getInstance()->getTriggeredAtTick(), 11u);

  // ...through tick t0 + 59...
  while (sim().tick() < 11u + n - 1)
  {
    sim().step();
    ASSERT_FALSE(sim().roundOver()) << "over early, at tick " << sim().tick();
  }
  ASSERT_EQ(sim().tick(), 70u);

  // ...and it is over after the whole of tick t0 + 60 = 71.
  sim().step();
  EXPECT_EQ(sim().tick(), 71u);
  EXPECT_TRUE(sim().roundOver());
  EXPECT_EQ(sim().roundEndedAtTick(), 71u);
  ASSERT_NE(sim().winner(), nullptr);
  EXPECT_EQ(sim().winner()->getPlayerNumber(), 2);
}

TEST_F(SimulationTest, TheFinalTickRunsInFullAndTheWinnerIsFixedAtTheTopOfIt)
{
  startStillWorld();
  const int n = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;
  destroyPlayer(1);
  while (sim().tick() < (uint64_t) n) // tick 1 is t0, so tick 60 is the last before the end
  {
    sim().step();
  }
  ASSERT_FALSE(sim().roundOver());
  ASSERT_EQ(sim().tick(), (uint64_t) n);

  // Things that happen during the final tick's pool update and runFrame:
  //  - a MUNCH_EFFECT loses a point of health in runFrame (all updates run);
  //  - the survivor's pending DESTROY is processed by runFrame, which flags them
  //    dead.
  spawnEntity(MUNCH_EFFECT, 0, 5, 5);
  DynamicMap* map = em().getDynamicMap();
  GameEntity* effect = nullptr;
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getType() == MUNCH_EFFECT) effect = it->second.get();
  }
  ASSERT_NE(effect, nullptr);
  const int effectHealth = effect->getHealth();

  std::shared_ptr<PlayerCharacterEntity> survivor = player(2);
  ASSERT_TRUE(survivor->isAlive());
  EntityMessageQueue::getInstance()->postDestroyMessage(survivor->getID());

  StateHasher poolBefore;
  sim().spawningPool().hashState(poolBefore);

  sim().step(); // tick n + 1: the round ends

  ASSERT_TRUE(sim().roundOver());
  EXPECT_EQ(sim().roundEndedAtTick(), (uint64_t) n + 1);
  EXPECT_EQ(effect->getHealth(), effectHealth - 1) << "runFrame ran on the final tick";
  EXPECT_FALSE(survivor->isAlive()) << "and so did its message processing";
  StateHasher poolAfter;
  sim().spawningPool().hashState(poolAfter);
  EXPECT_NE(poolAfter.value(), poolBefore.value()) << "and the spawning pool's update";

  // The winner was decided at the top of the tick, before the survivor died; the
  // state afterwards is the state after the whole step.
  ASSERT_NE(sim().winner(), nullptr);
  EXPECT_EQ(sim().winner()->getPlayerNumber(), 2);
  EXPECT_EQ(sim().currentWinner(), nullptr) << "right now nobody is left";
}

TEST_F(SimulationTest, AHarnessRoundTakesTheCountdownLongerThanItsEndCondition)
{
  // runRoundToCompletion() steps Simulation::step() to the end: `frames` is the
  // time before the end condition first held, `ticks` includes the countdown.
  configurePlayers(2);
  useTestMap();
  startRound();
  const RoundOutcome outcome = runRoundToCompletion(60000);
  ASSERT_TRUE(outcome.ended) << outcome.describe();
  EXPECT_EQ(outcome.ticks, outcome.frames + 1 + std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS)
      << outcome.describe();
  EXPECT_EQ(sim().tick(), (uint64_t) outcome.ticks);
  EXPECT_EQ(sim().roundEndedAtTick(), (uint64_t) outcome.ticks);
  EXPECT_EQ(std::GameOverTimer::getInstance()->getTriggeredAtTick(), (uint64_t) outcome.frames + 1);
}

TEST_F(SimulationTest, SteppingARoundThatIsOverKeepsItOverAndKeepsItsResult)
{
  startStillWorld();
  destroyPlayer(1);
  while (!sim().roundOver())
  {
    sim().step();
  }
  const uint64_t endedAt = sim().roundEndedAtTick();
  const std::shared_ptr<PlayerCharacterEntity> winner = sim().winner();

  sim().step();
  sim().step();

  EXPECT_TRUE(sim().roundOver());
  EXPECT_EQ(sim().roundEndedAtTick(), endedAt);
  EXPECT_EQ(sim().winner(), winner);
  EXPECT_EQ(sim().tick(), endedAt + 2);
}

TEST_F(SimulationTest, TheGameOverTimerRunsOnTheSimulationsTick)
{
  // One clock: the timer keeps none of its own, so the tick it records is the
  // Simulation's tick. (Before 03a the engine and the timer each counted.)
  startStillWorld();
  for (int i = 0; i < 7; i++)
  {
    sim().step();
  }
  destroyPlayer(1);
  sim().step();
  EXPECT_EQ(std::GameOverTimer::getInstance()->getTriggeredAtTick(), sim().tick());
  EXPECT_EQ(sim().tick(), 8u);
}

// ===========================================================================
// Determinism: a round is a pure function of its config
// ===========================================================================

TEST_F(SimulationTest, TheSameConfigGivesTheSameHashOnEveryTick)
{
  const MatchConfig config = makeConfig(4242, 4, 17, 13, 55);
  const std::vector<uint64_t> first = playHashes(config, 900);
  const std::vector<uint64_t> second = playHashes(config, 900);
  ASSERT_GT(first.size(), 500u) << "the round should still be running";
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); i++)
  {
    ASSERT_EQ(first[i], second[i]) << "the runs diverge at tick " << i;
  }
  // and the hash is not stuck
  std::set<uint64_t> distinct(first.begin(), first.end());
  EXPECT_EQ(distinct.size(), first.size()) << "every tick should have its own hash";
}

TEST_F(SimulationTest, DifferentSeedsGiveDifferentHashes)
{
  std::set<uint64_t> atStart;
  std::set<uint64_t> later;
  for (uint64_t seed = 1; seed <= 8; seed++)
  {
    const std::vector<uint64_t> h = playHashes(makeConfig(seed, 4, 17, 13, 55), 100);
    atStart.insert(h.front());
    later.insert(h.back());
  }
  EXPECT_EQ(atStart.size(), 8u);
  EXPECT_EQ(later.size(), 8u);
}

TEST_F(SimulationTest, TheConfigIsPartOfTheHash)
{
  const uint64_t base = [&] {
    startConfig(makeConfig(9, 3, 17, 13, 0));
    return sim().stateHash();
  }();

  startConfig(makeConfig(9, 3, 17, 13, 0));
  EXPECT_EQ(sim().stateHash(), base);

  startConfig(makeConfig(9, 4, 17, 13, 0));
  EXPECT_NE(sim().stateHash(), base) << "player count";

  startConfig(makeConfig(9, 3, 19, 13, 0));
  EXPECT_NE(sim().stateHash(), base) << "map width";

  startConfig(makeConfig(9, 3, 17, 15, 0));
  EXPECT_NE(sim().stateHash(), base) << "map height";

  startConfig(makeConfig(9, 3, 17, 13, 0, /*npcSpawner=*/false));
  EXPECT_NE(sim().stateHash(), base) << "NPC spawner";

  MatchConfig teams = makeConfig(9, 3, 17, 13, 0);
  teams.slots[1].team = 0;
  startConfig(teams);
  EXPECT_NE(sim().stateHash(), base) << "teams";

  MatchConfig human = makeConfig(9, 3, 17, 13, 0);
  human.slots[1].ai = false;
  startConfig(human);
  EXPECT_NE(sim().stateHash(), base) << "a human slot (no AI plan, no RNG draws for it)";

  startConfig(makeConfig(10, 3, 17, 13, 0));
  EXPECT_NE(sim().stateHash(), base) << "seed";
}

// ===========================================================================
// Singleton reset
// ===========================================================================

class SingletonResetTest : public SimulationTest
{
  protected:
    // Round A, a different round B, then A again. Between B and A the given
    // singleton is left holding last round's state (poison). The second A must
    // play exactly the first A, tick for tick.
    enum Poison { NONE, QUEUE, TIMER, IDS, AIVIEW, MAP_BLOCKS };

    void runAbA(Poison poison)
    {
      const MatchConfig a = makeConfig(4242, 4, 17, 13, 55);
      const MatchConfig b = makeConfig(99, 3, 21, 15, 30);

      const std::vector<uint64_t> first = playHashes(a, 400);
      ASSERT_GT(first.size(), 100u);

      // B runs until the game-over countdown has latched the timer, then stops
      // mid-round, so entities, ids and queued messages are all left behind.
      startConfig(b);
      destroyPlayer(1);
      destroyPlayer(2);
      for (int i = 0; i < 30; i++)
      {
        sim().step();
      }
      EXPECT_TRUE(std::GameOverTimer::getInstance()->isTimerTriggered()) << "precondition: B latched the timer";
      EXPECT_GT(countAllEntities(), 3) << "precondition: B leaves a world behind";

      // A's map size is the caller's job (setMapSize rebuilds the Map and
      // AIGameView), so poison goes in after it, just before start().
      setMapSize(a.mapWidth, a.mapHeight);
      plant(poison);

      const std::vector<uint64_t> second = playHashes(a, 400, /*resizeMap=*/false);
      ASSERT_EQ(first.size(), second.size());
      for (size_t i = 0; i < first.size(); i++)
      {
        ASSERT_EQ(first[i], second[i]) << "A's second run diverges from its first at tick " << i;
      }

      // Direct checks, for state the hash does not (or cannot) see: ids are not
      // hashed, so the restart is checked on its own.
      EXPECT_EQ(player(1)->getID(), 1) << "entity ids must restart every round";
      EXPECT_EQ(player(4)->getID(), 4);
    }

    void plant(Poison poison)
    {
      switch (poison)
      {
        case NONE:
          break;
        case QUEUE:
          // A message left over from the last round, for a tile that would hold
          // a block in this one.
          EntityMessageQueue::getInstance()->postCreateMessage(EGG1, 0, 5, 5, 0);
          break;
        case TIMER:
          std::GameOverTimer::getInstance()->setTimerTriggered(3);
          break;
        case IDS:
          for (int i = 0; i < 40; i++)
          {
            EntityMessageQueue::getInstance()->postCreateMessage(EGG1, 0, 5, 5, 0);
          }
          em().processMessageQueue(); // the ids move on (the entities die with the world)
          break;
        case AIVIEW:
        {
          EntityInfo ghost;
          ghost.id = 4242;
          ghost.playerNumber = 1;
          ghost.type = PLAYER_CHARACTER;
          ghost.groupType = CHARACTER_GROUP;
          ghost.staticFlag = false;
          AIGameView::getInstance()->addNewEntity(ghost, 5, 5);
          break;
        }
        case MAP_BLOCKS:
          Map::getInstance()->setBlockAt(3, 2, true);
          break;
      }
    }
};

TEST_F(SingletonResetTest, ARepeatedRoundReproducesTheFirstWithNothingPlanted)
{
  runAbA(NONE);
}

TEST_F(SingletonResetTest, ALeftOverMessageDoesNotLeakIntoTheNextRound)
{
  runAbA(QUEUE);
}

TEST_F(SingletonResetTest, ALatchedGameOverTimerDoesNotLeakIntoTheNextRound)
{
  runAbA(TIMER);
}

TEST_F(SingletonResetTest, EntityIdsRestartInTheNextRound)
{
  runAbA(IDS);
}

TEST_F(SingletonResetTest, AGhostInTheAIViewDoesNotLeakIntoTheNextRound)
{
  runAbA(AIVIEW);
}

TEST_F(SingletonResetTest, AStrayBlockInTheMapDoesNotLeakIntoTheNextRound)
{
  runAbA(MAP_BLOCKS);
}

// ===========================================================================
// What the hash notices
// ===========================================================================

class StateHashTest : public SimulationTest
{
  protected:
    uint64_t hash() { return sim().stateHash(); }

    // Applies `change` to a settled world and returns whether the hash moved.
    template <typename Change>
    bool changeMovesTheHash(Change change)
    {
      startStillWorld();
      sim().step();
      sim().step();
      const uint64_t before = hash();
      EXPECT_EQ(hash(), before) << "hashing must not change anything";
      change();
      return hash() != before;
    }

    GameEntity* findOfType(EntityType type)
    {
      DynamicMap* map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        if (it->second->getType() == type) return it->second.get();
      }
      return nullptr;
    }
};

TEST_F(StateHashTest, HashingIsRepeatableAndNeedsNoTick)
{
  startRound();
  const uint64_t h = hash();
  EXPECT_EQ(hash(), h);
  EXPECT_NE(h, StateHasher().value());
}

TEST_F(StateHashTest, ATickChangesTheHash)
{
  startStillWorld();
  const uint64_t before = hash();
  sim().step();
  EXPECT_NE(hash(), before) << "at least the tick number and the spawning pool's counter move";
}

TEST_F(StateHashTest, AnEntitysHealthIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { setPlayerHealth(1, 7); }));
}

TEST_F(StateHashTest, AnEntitysTileIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->setTileX(player(1)->getTileX() + 1); }));
}

TEST_F(StateHashTest, AnEntitysOffsetsAreHashedAsRawBits)
{
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->setOffsetX(player(1)->getOffsetX() + 1); }))
      << "one least-significant bit of Q16.16 is a different state";
  EXPECT_TRUE(changeMovesTheHash([&] { player(2)->setOffsetY(player(2)->getOffsetY() - 1); }));
}

TEST_F(StateHashTest, AnEntitysVelocityOwnerAndBlockPassageAreHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { (*player(1)).*Access::velocity() += 1; })) << "velocity";
  EXPECT_TRUE(changeMovesTheHash([&] { (*player(1)).*Access::owner() = 2; })) << "owner (egg theft changes it)";
  EXPECT_TRUE(changeMovesTheHash([&] { (*player(1)).*Access::walkThroughBlocks() = true; })) << "walk through blocks";
}

TEST_F(StateHashTest, AnEntitysAnimStateIsHashedBecauseItDrivesMovement)
{
  EXPECT_TRUE(changeMovesTheHash([&] { (*player(1)).*Access::animState() = 3; }));
}

TEST_F(StateHashTest, APlayersFlagsAndCountersAreHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->setAlive(false); })) << "alive";
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->m_controlledByAI = true; })) << "controlled by AI";
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->incNumberEggsPlaced(); })) << "eggs placed";
  EXPECT_TRUE(changeMovesTheHash([&] { player(1)->incNumberBombsPlaced(); })) << "bombs placed";
  EXPECT_TRUE(changeMovesTheHash([&] { (*player(2)).*Access::iq() += 1; })) << "IQ";
}

TEST_F(StateHashTest, TheSimRngStateIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { SimRandom::sim().nextU32(); }));
}

TEST_F(StateHashTest, TheStaticMapIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { Map::getInstance()->setBlockAt(7, 5, true); }));
}

TEST_F(StateHashTest, TheGameOverTimerIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] { std::GameOverTimer::getInstance()->setTimerTriggered(2); }));
}

TEST_F(StateHashTest, AQueuedMessageIsHashed)
{
  EXPECT_TRUE(changeMovesTheHash([&] {
    EntityMessageQueue::getInstance()->postCreateMessage(MINE, 1, 4, 4, 0);
  })) << "a message waiting for the next tick is state";
  EXPECT_TRUE(changeMovesTheHash([&] {
    EntityMessageQueue::getInstance()->postDestroyMessage(player(1)->getID());
  }));
}

TEST_F(StateHashTest, ThePositionOfAnEntityInATilesListIsHashed)
{
  // Two entities on one tile: collisions and the AI's "first match" depend on
  // which is first in the tile's list, so swapping them alone must show.
  startStillWorld();
  const int x = player(1)->getTileX();
  const int y = player(1)->getTileY();
  spawnEntity(MUNCH_EFFECT, 0, x, y);
  DynamicMap* map = em().getDynamicMap();
  ASSERT_EQ(map->entityMap[x][y].size(), 2u);

  const uint64_t before = hash();
  std::swap(map->entityMap[x][y][0], map->entityMap[x][y][1]);
  EXPECT_NE(hash(), before);
  std::swap(map->entityMap[x][y][0], map->entityMap[x][y][1]);
  EXPECT_EQ(hash(), before) << "and swapping back restores it";
}

TEST_F(StateHashTest, TheOrderOfAnAIViewTilesListIsHashed)
{
  startStillWorld();
  const int x = player(1)->getTileX();
  const int y = player(1)->getTileY();
  spawnEntity(MUNCH_EFFECT, 0, x, y);
  AIGameView* view = AIGameView::getInstance();
  const std::vector<EntityInfo>& tile = view->getEntityInfoAtLocation(x, y);
  ASSERT_EQ(tile.size(), 2u);

  const uint64_t before = hash();
  // Re-add the first entry at the back, in place of the move API, to reorder.
  const EntityInfo first = tile[0];
  ASSERT_TRUE(view->removeEntity(first.id, x, y));
  view->addNewEntity(first, x, y);
  EXPECT_NE(hash(), before);
}

TEST_F(StateHashTest, AnEggsHatchCounterIsHashed)
{
  startStillWorld();
  spawnEntity(EGG3, 1, 5, 5);
  const uint64_t before = hash();
  runFrames(1); // the egg counts down; nothing else in this world moves or draws
  EXPECT_NE(hash(), before);
}

TEST_F(StateHashTest, FusesAndLifespansAreHashed)
{
  const EntityType types[] = {BOMB, MINE, ROCKET_SPIN, ENPLUS_POWERUP};
  for (EntityType type : types)
  {
    startStillWorld();
    spawnEntity(type, 1, 5, 5);
    const uint64_t before = hash();
    runFrames(1);
    EXPECT_NE(hash(), before) << "entity type " << (int) type;
  }
}

TEST_F(StateHashTest, EffectsAndExplosionsHealthCountdownIsHashed)
{
  const EntityType types[] = {EXPLOSION, MUNCH_EFFECT, DAMAGE_EFFECT};
  for (EntityType type : types)
  {
    startStillWorld();
    spawnEntity(type, 0, 5, 5);
    const uint64_t before = hash();
    runFrames(1);
    EXPECT_NE(hash(), before) << "entity type " << (int) type;
  }
}

TEST_F(StateHashTest, ARobotsOwnCountersAreHashed)
{
  // The robot moves too, so this asserts the hash moves, not why; the counters
  // are covered by the golden digests below, which change if any is dropped.
  startStillWorld();
  spawnEntity(ROBOT1, 0, 5, 5, GameEntity::LAY_MINES);
  const uint64_t before = hash();
  runFrames(1);
  EXPECT_NE(hash(), before);
}

// -- what must NOT move the hash -------------------------------------------

TEST_F(StateHashTest, RenderOnlyAnimationStateIsNotHashed)
{
  EXPECT_FALSE(changeMovesTheHash([&] {
    (*player(1)).*Access::animFrame() = 3;
    (*player(2)).*Access::animCounter() = 1;
    player(1)->incAnimFrame();
  })) << "m_currentAnimFrame and the anim counters are render-only";
}

TEST_F(StateHashTest, ADamageEffectsCosmeticFrameIsNotHashed)
{
  startStillWorld();
  spawnEntity(DAMAGE_EFFECT, 0, 4, 4);
  GameEntity* effect = findOfType(DAMAGE_EFFECT);
  ASSERT_NE(effect, nullptr);
  const uint64_t before = hash();
  for (int frame = 0; frame < 5; frame++)
  {
    (*effect).*Access::animFrame() = frame;
    EXPECT_EQ(hash(), before) << "frame " << frame;
  }
}

TEST_F(StateHashTest, TheCosmeticRngIsNotHashed)
{
  EXPECT_FALSE(changeMovesTheHash([&] {
    for (int i = 0; i < 10; i++) SimRandom::cosmetic().nextU32();
  }));
}

TEST_F(StateHashTest, ALocalControllerAssignmentIsNotHashed)
{
  EXPECT_FALSE(changeMovesTheHash([&] { player(1)->attachedController = 3; }))
      << "which device drives a player is local, not part of the match";
}

TEST_F(StateHashTest, CosmeticDrawsDuringARoundDoNotChangeItsHashes)
{
  const MatchConfig config = makeConfig(31, 4, 17, 13, 55);
  const std::vector<uint64_t> quiet = playHashes(config, 400);

  startConfig(config);
  std::vector<uint64_t> noisy;
  noisy.push_back(hash());
  for (int i = 0; i < 400 && !sim().roundOver(); i++)
  {
    SimRandom::cosmetic().nextU32();
    sim().step();
    noisy.push_back(hash());
  }
  EXPECT_EQ(quiet, noisy);
}

// ===========================================================================
// The hasher itself
// ===========================================================================

TEST(StateHasherTest, IsFnv1a64OverTheBytes)
{
  // Published FNV-1a 64 test vectors.
  StateHasher empty;
  EXPECT_EQ(empty.value(), 0xcbf29ce484222325ULL);

  StateHasher a;
  a.u8('a');
  EXPECT_EQ(a.value(), 0xaf63dc4c8601ec8cULL);

  StateHasher foobar;
  for (char c : std::string("foobar"))
  {
    foobar.u8((uint8_t) c);
  }
  EXPECT_EQ(foobar.value(), 0x85944171f73967e8ULL);
}

TEST(StateHasherTest, IntegersAreFedLeastSignificantByteFirst)
{
  StateHasher viaI32;
  viaI32.i32(0x04030201);
  StateHasher viaBytes;
  viaBytes.u8(1);
  viaBytes.u8(2);
  viaBytes.u8(3);
  viaBytes.u8(4);
  EXPECT_EQ(viaI32.value(), viaBytes.value());

  StateHasher negative;
  negative.i32(-2); // 0xfffffffe
  StateHasher negativeBytes;
  negativeBytes.u8(0xfe);
  negativeBytes.u8(0xff);
  negativeBytes.u8(0xff);
  negativeBytes.u8(0xff);
  EXPECT_EQ(negative.value(), negativeBytes.value());

  StateHasher wide;
  wide.u64(0x0807060504030201ULL);
  StateHasher wideBytes;
  for (int b = 1; b <= 8; b++) wideBytes.u8((uint8_t) b);
  EXPECT_EQ(wide.value(), wideBytes.value());

  StateHasher flagTrue;
  flagTrue.flag(true);
  StateHasher one;
  one.u8(1);
  EXPECT_EQ(flagTrue.value(), one.value());
}

TEST(StateHasherTest, EntityReferencesAreWalkPositionsNotIds)
{
  StateHasher h;
  h.setEntityIds({3, 7, 9, 12});
  EXPECT_EQ(h.refOf(3), 0);
  EXPECT_EQ(h.refOf(7), 1);
  EXPECT_EQ(h.refOf(12), 3);
  EXPECT_EQ(h.refOf(8), -1);
  EXPECT_EQ(h.refOf(0), -1);
  EXPECT_EQ(h.refOf(99), -1);

  // The same walk with every id shifted hashes the same.
  StateHasher shifted;
  shifted.setEntityIds({103, 107, 109, 112});
  StateHasher x;
  x.setEntityIds({3, 7, 9, 12});
  x.ref(9);
  x.ref(8);
  shifted.ref(109);
  shifted.ref(108);
  EXPECT_EQ(x.value(), shifted.value());
}

// ===========================================================================
// Golden digests: fixed rounds, fixed hashes
// ===========================================================================

// A fingerprint of the per-tick hashes of a few pinned rounds. It pins the
// hash specification and the simulation together, so it moves when either does
// and has to be regenerated deliberately (BITRIOT_HASH_DUMP=1 prints the values,
// and every tick's hash for cross-compiler comparison). It is also what makes
// the suite itself prove that GCC and Clang agree: the same values must pass on
// both.
struct GoldenRound
{
  const char* name;
  uint64_t seed;
  int players;
  int width;
  int height;
  int blockPercentage;
  int ticks;
  uint64_t digest;
};

const GoldenRound GOLDEN_ROUNDS[] = {
  {"four bots, default map, 55% blocks", 4242, 4, 17, 13, 55, 20000, 0xbe604f0bebd2fa62ULL},
  {"six bots, big map, 55% blocks",      777,  6, 25, 19, 55, 20000, 0x6f06ba6653ff95d7ULL},
  {"two bots, small map, no blocks",     31,   2, 9,  9,  0,  20000, 0x30b88c0d114f2b58ULL},
  {"two bots, 13x11, 55% blocks",        4242, 2, 13, 11, 55, 20000, 0xa5275a95eb757c98ULL},
};

TEST_F(SimulationTest, PinnedRoundsKeepTheirHashes)
{
  const bool dump = std::getenv("BITRIOT_HASH_DUMP") != nullptr;
  for (const GoldenRound& g : GOLDEN_ROUNDS)
  {
    const std::vector<uint64_t> hashes = playHashes(makeConfig(g.seed, g.players, g.width, g.height, g.blockPercentage), g.ticks);
    const uint64_t digest = digestOf(hashes);
    if (dump)
    {
      std::fprintf(stderr, "HASHDUMP %s digest=0x%016llxULL ticks=%zu\n", g.name,
                   (unsigned long long) digest, hashes.size());
      const char* every = std::getenv("BITRIOT_HASH_DUMP");
      if (every[0] == 'a') // BITRIOT_HASH_DUMP=all: one line per tick
      {
        for (size_t i = 0; i < hashes.size(); i++)
        {
          std::fprintf(stderr, "HASH %s %zu 0x%016llx\n", g.name, i, (unsigned long long) hashes[i]);
        }
      }
    }
    else
    {
      EXPECT_EQ(digest, g.digest) << g.name << " (" << hashes.size() << " hashes)";
    }
  }
}

} // namespace
} // namespace bitriot_test
