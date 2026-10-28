// AI energy-seeking tests.
//
// A bot that is badly hurt cannot lay anything (CREATE_ENTITY is skipped below
// 15 health), so it cannot make its own energy and must collect existing
// pickups instead. These tests put one AI player on an otherwise empty board
// with its health forced low and a pickup placed somewhere, then check that it
// walks (or bombs) its way there. The game needs at least two players, so a
// second one is created and immediately removed; no hostiles are present and the
// spawning pool is never updated, so the only thing moving is the bot.
//
// The board is 9x9: walls on the border, indestructible pillars on every
// (even, even) tile, everything else open. Player 1 starts in the (1,1) corner.

#include "support/HeadlessGame.h"

#include <cstdlib>

#include "GameSettings.h"
#include "Map.h"

namespace bitriot_test
{
namespace
{

// Plenty for a bot to cross the board and bomb a block on the way; a bot that
// is going to get there does so in a small fraction of this.
const int FRAME_BUDGET = 3000;

class AISeekEnergyTest : public HeadlessGameTest
{
  protected:
    void SetUp() override
    {
      HeadlessGameTest::SetUp();
      configurePlayers(2); // the game's minimum; player 2 is removed below
      setMapSize(9, 9);
      startRound();
      destroyPlayer(2);
      clearBlocks();
      ASSERT_EQ(player(1)->getTileX(), 1);
      ASSERT_EQ(player(1)->getTileY(), 1);
    }

    // Entities of exactly this type, whoever owns them.
    int countOfType(EntityType type)
    {
      int count = 0;
      DynamicMap* map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        if (it->second->getType() == type)
        {
          count++;
        }
      }
      return count;
    }

    // Pumps frames until fewer than `remaining` entities of `type` are left,
    // returning the frames taken or -1 if the budget ran out first.
    int framesUntilFewerThan(EntityType type, int remaining, int budget = FRAME_BUDGET)
    {
      for (int frame = 1; frame <= budget; frame++)
      {
        runFrames(1);
        if (countOfType(type) < remaining)
        {
          return frame;
        }
      }
      return -1;
    }

    // Walls a tile in with blocks on every open side.
    void boxIn(int x, int y)
    {
      spawnEntity(BLOCK, 0, x + 1, y);
      spawnEntity(BLOCK, 0, x - 1, y);
      spawnEntity(BLOCK, 0, x, y + 1);
      spawnEntity(BLOCK, 0, x, y - 1);
    }
};

// -- the basic behaviour -----------------------------------------------------

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitWalksToAnEnergyDropAndCollectsIt)
{
  setPlayerHealth(1, 13);
  spawnEntity(ENERGY_DROP, 0, 4, 1);

  const int frames = framesUntilFewerThan(ENERGY_DROP, 1);

  ASSERT_GT(frames, 0) << "bot never collected an energy drop three tiles away";
  // 13 + ENERGY_DROP_BONUS (6). Anything else means the drop was destroyed
  // rather than picked up, or that something else changed the bot's health.
  EXPECT_EQ(player(1)->getHealth(), 19);
}

TEST_F(AISeekEnergyTest, BotCollectsAnEnergyPowerupToo)
{
  setPlayerHealth(1, 10);
  spawnEntity(ENPLUS_POWERUP, 0, 5, 1);

  const int frames = framesUntilFewerThan(ENPLUS_POWERUP, 1);

  ASSERT_GT(frames, 0) << "bot never collected the energy powerup";
  EXPECT_EQ(player(1)->getHealth(), 35); // 10 + ENERGY_POWERUP_BONUS (25)
}

TEST_F(AISeekEnergyTest, BotWithNoEnergyLooksBeyondItsUsualSearchRange)
{
  // (7,7) is twelve tiles away; healthy bots only notice powerups within four.
  setPlayerHealth(1, 10);
  spawnEntity(ENERGY_DROP, 0, 7, 7);

  const int frames = framesUntilFewerThan(ENERGY_DROP, 1);

  ASSERT_GT(frames, 0) << "bot too weak to lay eggs never went for a drop across the board";
  EXPECT_EQ(player(1)->getHealth(), 16);
}

TEST_F(AISeekEnergyTest, BotHeadsForTheNearerOfTwoDrops)
{
  setPlayerHealth(1, 13);
  spawnEntity(ENERGY_DROP, 0, 7, 1);
  spawnEntity(ENERGY_DROP, 0, 3, 1);

  ASSERT_GT(framesUntilFewerThan(ENERGY_DROP, 2), 0);

  // The near one went first, so the far one is still there.
  DynamicMap* map = em().getDynamicMap();
  int remainingX = -1;
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getType() == ENERGY_DROP)
    {
      remainingX = it->second->getTileX();
    }
  }
  EXPECT_EQ(remainingX, 7);
}

// -- pickups the bot cannot, or should not, go for ---------------------------

TEST_F(AISeekEnergyTest, BotThatCannotAffordToBombSkipsPickupWalledInByBlocksForOneItCanWalkTo)
{
  // Health 9 cannot survive a bomb's cost plus its blast, so the nearer drop
  // inside its block ring is out of reach. The bot must not sit there trying;
  // it should take the open one.
  setPlayerHealth(1, 9);
  boxIn(3, 1);
  spawnEntity(ENERGY_DROP, 0, 3, 1);  // nearer, walled in
  spawnEntity(ENERGY_DROP, 0, 7, 1);  // farther, open

  ASSERT_GT(framesUntilFewerThan(ENERGY_DROP, 2), 0)
      << "bot fixated on the drop it could not reach";

  DynamicMap* map = em().getDynamicMap();
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getType() == ENERGY_DROP)
    {
      EXPECT_EQ(it->second->getTileX(), 3) << "collected the wrong drop";
    }
  }
}

TEST_F(AISeekEnergyTest, AnUnreachablePickupDoesNotStopTheBotNoticingALaterOne)
{
  setPlayerHealth(1, 9);
  boxIn(3, 1);
  spawnEntity(ENERGY_DROP, 0, 3, 1);

  runFrames(600); // long enough for any "stuck on the unreachable drop" loop to set in
  ASSERT_EQ(player(1)->getHealth(), 9) << "nothing was reachable, health should not have moved";
  ASSERT_EQ(countOfType(ENERGY_DROP), 1);

  spawnEntity(ENERGY_DROP, 0, 5, 1);
  EXPECT_GT(framesUntilFewerThan(ENERGY_DROP, 2), 0)
      << "bot never reacted to a new, reachable drop";
}

TEST_F(AISeekEnergyTest, BotAvoidsAPickupSittingOnAnEnemyMine)
{
  setPlayerHealth(1, 13);
  spawnEntity(ENERGY_DROP, 0, 3, 1);
  spawnEntity(MINE, 0, 3, 1);  // owner 0: hostile to everyone
  spawnEntity(ENERGY_DROP, 0, 1, 5);

  ASSERT_GT(framesUntilFewerThan(ENERGY_DROP, 2), 0);
  EXPECT_EQ(player(1)->getHealth(), 19);

  DynamicMap* map = em().getDynamicMap();
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getType() == ENERGY_DROP)
    {
      EXPECT_EQ(it->second->getTileX(), 3) << "took the mined drop instead of the safe one";
    }
  }
}

// -- the deadlock itself -------------------------------------------------------

TEST_F(AISeekEnergyTest, BotWalledInByBlocksAndTooHurtToLayBombsItsWayOutToADrop)
{
  // The original hang: a bot below 15 health, boxed in by blocks, with an
  // energy drop in view. It kept deciding to go to the drop, but the route's
  // first step was a block - never bombed, because the path search skipped it,
  // and bombing was off the table below 15 health anyway. (The same path hole
  // froze a bot at any health: seed 199's four-player co-op hang was a bot on
  // 80 health. Not tested here with a healthy bot because a bot's opening goal
  // lays an egg on its own tile, and a bomb cannot be laid onto an egg.)
  setPlayerHealth(1, 13);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);
  spawnEntity(ENERGY_DROP, 0, 1, 6);

  const int frames = framesUntilFewerThan(ENERGY_DROP, 1);

  ASSERT_GT(frames, 0) << "bot stayed boxed in, health " << player(1)->getHealth();
  EXPECT_TRUE(player(1)->isAlive());
  EXPECT_GT(player(1)->getHealth(), 6) << "should have gained the drop's energy";
}

TEST_F(AISeekEnergyTest, BotTooHurtToSurviveItsOwnBombStaysPutRatherThanBlowingItselfUp)
{
  // A bomb costs 1 and its blast can take 8 more; at 11 that would leave 2.
  setPlayerHealth(1, 11);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);
  spawnEntity(ENERGY_DROP, 0, 1, 6);

  runFrames(1500);

  EXPECT_TRUE(player(1)->isAlive());
  EXPECT_EQ(countOfType(BOMB), 0);
  EXPECT_EQ(countOfType(ENERGY_DROP), 1);
  EXPECT_EQ(player(1)->getHealth(), 11);
}

// -- no regressions in ordinary behaviour -----------------------------------

TEST_F(AISeekEnergyTest, BotWithEnoughHealthStillLaysEnergyEggsWhenNothingIsAround)
{
  setPlayerHealth(1, 60);
  runFrames(1500);
  EXPECT_GT(player(1)->getNumberEggsPlaced(), 0)
      << "the normal recovery path (lay EGG1s) must still work at 15+ health";
}

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitWithNothingToCollectJustWaits)
{
  setPlayerHealth(1, 12);
  runFrames(1500);
  EXPECT_TRUE(player(1)->isAlive());
  EXPECT_EQ(player(1)->getNumberEggsPlaced(), 0);
  EXPECT_EQ(player(1)->getHealth(), 12);
}

// -- determinism ---------------------------------------------------------------

TEST_F(AISeekEnergyTest, SameSeedGivesTheSamePickupTiming)
{
  auto run = [this]() -> int
  {
    std::srand(1234);
    startRound();
    destroyPlayer(2);
    clearBlocks();
    setPlayerHealth(1, 13);
    spawnEntity(BLOCK, 0, 2, 1);
    spawnEntity(BLOCK, 0, 1, 2);
    spawnEntity(ENERGY_DROP, 0, 1, 6);
    return framesUntilFewerThan(ENERGY_DROP, 1);
  };

  const int first = run();
  const int second = run();
  ASSERT_GT(first, 0);
  EXPECT_EQ(first, second);
}

} // namespace
} // namespace bitriot_test
