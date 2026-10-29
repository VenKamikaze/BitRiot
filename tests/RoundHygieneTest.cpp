// Round-lifecycle hygiene (netplay brief 01).
//
// Things that must be the same at the start of every round, and must not depend
// on the wall clock: the game-over countdown, entity ids, the static map after a
// resize, and uninitialised entity state.

#include "support/HeadlessGame.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>

#include "AIGameView.h"
#include "BlockEntity.h"
#include "EntityFactory.h"
#include "GameOverTimer.h"
#include "GameSettings.h"
#include "Map.h"

using bitriot_test::HeadlessGameTest;

namespace
{
// Mirrors how Simulation::step() drives the countdown each tick, via the same
// GameTimer::advance() it calls. The timer keeps no clock of its own: the caller
// passes the tick number, so these tests count ticks in `clock`.
//
// Returns the number of ticks from the first tick the end condition holds (which
// is tick 0) until the tick on which the round ends, or -1 if it did not end
// within maxTicks.
int ticksUntilRoundEnds(std::GameTimer* timer, uint64_t& clock, int maxTicks)
{
  for (int tick = 0; tick < maxTicks; tick++)
  {
    if (timer->advance(++clock, true))
    {
      return tick;
    }
  }
  return -1;
}
} // namespace

class RoundHygieneTest : public HeadlessGameTest
{
  protected:
    // Every wall the map must have at its current size: outer ring and the
    // even/even pillar grid. Returns an empty string if all are intact, else a
    // description of the first broken tile.
    std::string firstBrokenWall()
    {
      Map* map = Map::getInstance();
      const int w = Map::MAP_WIDTH;
      const int h = Map::MAP_HEIGHT;
      for (int x = 0; x < w; x++)
      {
        for (int y = 0; y < h; y++)
        {
          bool ring = (x == 0 || y == 0 || x == w - 1 || y == h - 1);
          bool pillar = (x >= 2 && x < w - 2 && y >= 2 && y < h - 2 && x % 2 == 0 && y % 2 == 0);
          Map::StaticTile expected = (ring || pillar) ? Map::WALL : Map::EMPTY;
          if (map->staticTileAt(x, y) != expected)
          {
            return "tile (" + std::to_string(x) + "," + std::to_string(y) + ") of "
                + std::to_string(w) + "x" + std::to_string(h) + " map is "
                + std::to_string((int) map->staticTileAt(x, y)) + ", expected "
                + std::to_string((int) expected);
          }
        }
      }
      return "";
    }
};

// ---------------------------------------------------------------------------
// GameOverTimer
// ---------------------------------------------------------------------------

TEST_F(RoundHygieneTest, GameOverCountdownIsTwoSecondsOfTicks)
{
  // Brief 01 / Q1: the intended countdown is 2 s. The base constructor used to
  // call the virtual getDefaultTimeout() and so always got the base class's 5 s.
  const int ticks = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;
  EXPECT_EQ(ticks, 60);
  EXPECT_EQ(std::GameOverTimer::getInstance()->getTimeoutTicks(), ticks);

  const int ms = ticks * std::GameSettings::NORMAL_TICKS_SPEED;
  EXPECT_LE(ms, 2000);
  EXPECT_GT(ms, 2000 - std::GameSettings::NORMAL_TICKS_SPEED);
}

TEST_F(RoundHygieneTest, GameOverTimerEndsExactlyNTicksAfterTheConditionFirstHolds)
{
  std::GameTimer* timer = std::GameOverTimer::getInstance();
  timer->reset();
  uint64_t clock = 0;
  const int n = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;

  // The condition is false for a while first: that must not start the clock.
  for (int i = 0; i < 25; i++)
  {
    ASSERT_FALSE(timer->advance(++clock, false));
  }
  EXPECT_FALSE(timer->isTimerTriggered());

  // First tick it holds starts the countdown; it ends n ticks later, not before.
  EXPECT_EQ(ticksUntilRoundEnds(timer, clock, n * 3), n);
}

TEST_F(RoundHygieneTest, GameOverTimerIgnoresWallClockTime)
{
  std::GameTimer* timer = std::GameOverTimer::getInstance();
  timer->reset();
  uint64_t clock = 0;
  const int n = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;

  ASSERT_FALSE(timer->advance(++clock, true)); // triggers

  // Real time passing without sim ticks must not move the countdown: the sim
  // clock only advances when the loop passes a new tick number to advance().
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(timer->getTimerCompleted(clock));

  for (int i = 1; i < n; i++)
  {
    ASSERT_FALSE(timer->advance(++clock, true)) << "ended early at tick " << i;
  }
  EXPECT_TRUE(timer->advance(++clock, true));
}

TEST_F(RoundHygieneTest, GameOverTimerWaitsTheFullCountdownInEveryRound)
{
  // Today's bug: timerTriggered was never cleared, so from round 2 on the round
  // ended on the first tick the condition held. Drive the real round setup and the
  // real tick (Simulation::start() and step()) three times over.
  configurePlayers(2);
  std::GameTimer* timer = std::GameOverTimer::getInstance();
  const int n = std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS;

  for (int round = 1; round <= 3; round++)
  {
    startRound();
    EXPECT_FALSE(timer->isTimerTriggered()) << "round " << round << " starts latched";

    // Mid-round, the condition holds for the first time.
    for (int i = 0; i < 10; i++)
    {
      sim().step();
    }
    ASSERT_FALSE(sim().roundOver());
    killPlayer(1);
    ASSERT_TRUE(em().oneOrZeroPlayersRemain(pool().canSpawnMore()));

    // `tick` counts steps from the first one that sees the condition; the round
    // ends on the n-th after it.
    int ticks = -1;
    for (int tick = 0; tick < n * 3; tick++)
    {
      sim().step();
      if (sim().roundOver())
      {
        ticks = tick;
        break;
      }
    }
    EXPECT_EQ(ticks, n) << "round " << round;
  }
}

// ---------------------------------------------------------------------------
// Entity ids
// ---------------------------------------------------------------------------

TEST_F(RoundHygieneTest, EntityIdsRestartEveryRound)
{
  configurePlayers(3);

  startRound();
  const int firstIdRound1 = player(1)->getID();
  const int lastIdRound1 = player(3)->getID();
  spawnEntity(BOMB, 1, 3, 3);
  runFrames(50);

  startRound();
  const int firstIdRound2 = player(1)->getID();
  const int lastIdRound2 = player(3)->getID();

  EXPECT_EQ(firstIdRound1, 1);
  EXPECT_EQ(firstIdRound2, firstIdRound1);
  EXPECT_EQ(lastIdRound2, lastIdRound1);
}

// ---------------------------------------------------------------------------
// Map resize between rounds
// ---------------------------------------------------------------------------

TEST_F(RoundHygieneTest, ShrinkingTheMapWithBlocksPresentLeavesTheNewWallsIntact)
{
  // GameEngine::applyMapDimensionsIfChanged() rebuilds Map *before* resetGame()
  // destroys the old EntityManager. setMapSize() followed by startRound() is the
  // same order. Old blocks must not write into the new map.
  //
  // Old map 17x13. The new map is 9x7, so:
  //   (8,3)  is the new east wall
  //   (3,6)  is the new south wall
  //   (4,5)  is an interior tile
  startRound();
  spawnEntity(BLOCK, 0, 8, 3);
  spawnEntity(BLOCK, 0, 3, 6);
  spawnEntity(BLOCK, 0, 4, 5);
  ASSERT_EQ(Map::getInstance()->staticTileAt(8, 3), Map::BLOCK);

  setMapSize(9, 7);
  startRound(); // old EntityManager (and its blocks) dies here

  EXPECT_EQ(firstBrokenWall(), "");
}

TEST_F(RoundHygieneTest, ShrinkingTheMapNeverWritesOutsideIt)
{
  // Blocks at coordinates that do not exist in the new map at all. Without the
  // fix their destructors write past the end of the new (smaller) tile array;
  // run under AddressSanitizer this is a heap-buffer-overflow. Without ASan it is
  // silent heap corruption that this test cannot observe (the indices are past
  // the array), so it only fails under ASan; the walls check is a sanity check.
  startRound();
  spawnEntity(BLOCK, 0, 15, 11); // 15*7+11 = 116: well past a 9x7 map's 63 tiles
  spawnEntity(BLOCK, 0, 13, 9);

  setMapSize(9, 7);
  startRound();

  EXPECT_EQ(firstBrokenWall(), "");
}

TEST_F(RoundHygieneTest, ResizingWiderButShorterWithBlocksPresentLeavesTheNewWallsIntact)
{
  // A resize where one dimension grows and the other shrinks. The stale write
  // goes to index x*newHeight+y of the new array, which is in bounds here but
  // lands on a different tile than (x,y).
  //
  // Old map 17x13, new map 21x11. A block at (3,11) (interior in the old map:
  // y = 11 is its last row before the south wall) gives index 3*11+11 = 44, which
  // in the new map is tile (44/11, 44%11) = (4,0): part of the new top wall.
  // The array has 231 tiles, so nothing is out of bounds and ASan sees nothing;
  // only the wall check can catch it.
  startRound();
  spawnEntity(BLOCK, 0, 3, 11);
  ASSERT_EQ(Map::getInstance()->staticTileAt(3, 11), Map::BLOCK);

  setMapSize(21, 11);
  ASSERT_EQ(Map::getInstance()->staticTileAt(4, 0), Map::WALL);
  startRound();

  EXPECT_EQ(firstBrokenWall(), "");
}

TEST_F(RoundHygieneTest, GrowingTheMapWithBlocksPresentKeepsTheNewMapIntact)
{
  // Regression guard only: when both dimensions grow, an old interior block
  // lands on an interior tile of the bigger map, so this passes even without
  // the generation guard. The mixed resize above is the one that can fail.
  setMapSize(9, 7);
  startRound();
  spawnEntity(BLOCK, 0, 3, 3);
  spawnEntity(BLOCK, 0, 7, 5);
  spawnEntity(BLOCK, 0, 6, 1);

  setMapSize(17, 13);
  startRound();

  EXPECT_EQ(firstBrokenWall(), "");
}

TEST_F(RoundHygieneTest, BlocksStillClearTheirTileAfterAResize)
{
  // The guard must only skip *stale* writes: a block placed after the resize
  // still cleans up after itself.
  startRound();
  setMapSize(9, 7);
  startRound();

  spawnEntity(BLOCK, 0, 3, 3);
  ASSERT_EQ(Map::getInstance()->staticTileAt(3, 3), Map::BLOCK);

  EntityMessageQueue::getInstance()->postDestroyMessage(em().getDynamicMap()->entityMap[3][3].front());
  em().processMessageQueue();

  EXPECT_EQ(Map::getInstance()->staticTileAt(3, 3), Map::EMPTY);
  EXPECT_EQ(firstBrokenWall(), "");
}

TEST_F(RoundHygieneTest, SameSizeRoundsKeepClearingBlocksBetweenRounds)
{
  // Not a resize: the normal "play again" must still empty the old round's blocks.
  startRound();
  spawnEntity(BLOCK, 0, 3, 3);
  ASSERT_EQ(Map::getInstance()->staticTileAt(3, 3), Map::BLOCK);

  startRound();

  EXPECT_EQ(Map::getInstance()->staticTileAt(3, 3), Map::EMPTY);
}

// ---------------------------------------------------------------------------
// Smaller fixes on the same path
// ---------------------------------------------------------------------------

namespace
{
class BareEntity : public GameEntity
{
  public:
    void onCollision(EntityType, EntityGroupType, int) override {}
    void update() override {}
    int tileX() { return m_tileX; }
};
} // namespace

TEST_F(RoundHygieneTest, GameEntityScalarsAreInitialisedByTheConstructor)
{
  // Construct over memory full of 0xAB so that a member the constructor forgets
  // reads back as 0xABAB.... rather than whatever zeroed memory happens to give.
  alignas(BareEntity) unsigned char storage[sizeof(BareEntity)];
  std::memset(storage, 0xAB, sizeof(storage));
  BareEntity* e = new (storage) BareEntity; // no (): value-initialisation would zero the members first

  EXPECT_EQ(e->getHealth(), 0);
  EXPECT_EQ(e->getVelocity(), 0);
  EXPECT_EQ(e->getID(), 0);
  EXPECT_EQ(e->getPlayerNumber(), 0);
  EXPECT_EQ(e->getType(), BLOCK);
  EXPECT_EQ(e->getGroupType(), BLOCK_GROUP);

  e->~BareEntity();
}

TEST_F(RoundHygieneTest, CollisionsSkipAnIdThatIsOnTheTileButNotInTheEntityList)
{
  // Two entities share a tile, then one vanishes from the entity list while its
  // id is still on the tile (the stale-id state runCollisions must tolerate).
  // Looking it up used to dereference find(...)->second on end().
  configurePlayers(2);
  startRound();
  spawnEntity(ENPLUS_POWERUP, 0, 3, 3);
  spawnEntity(IQPLUS_POWERUP, 0, 3, 3);

  DynamicMap* dm = em().getDynamicMap();
  ASSERT_EQ(dm->entityMap[3][3].size(), 2u);
  dm->entityList.erase(dm->entityMap[3][3].front());

  em().runFrame(); // must not crash
  SUCCEED();
}
