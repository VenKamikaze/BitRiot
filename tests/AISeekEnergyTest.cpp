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

#include <chrono>
#include <cstdlib>

#include "GameSettings.h"
#include "Map.h"
#include "PlayerCharacterEntity.h"

namespace bitriot_test
{
namespace
{

// Plenty for a bot to cross the board and bomb a block on the way; a bot that
// is going to get there does so in a small fraction of this.
const int FRAME_BUDGET = 3000;

// A bot that is not part of the round, so a test can ask it to make a decision
// and look at the answer without it also moving, laying eggs or being hurt.
// It sees the real map and the real AIGameView, which is all a decision needs.
class AIProbe : public PlayerCharacterEntity
{
  public:
    typedef PlayerCharacterEntity::RouteInfo Route;

    AIProbe(int tileX, int tileY, int health)
      : PlayerCharacterEntity(99999, 1, tileX, tileY, GameEntity::AI_CONTROLLED_BOT_FLAG)
    {
      clearGoalsAndInstructions(); // drop the opening "lay some eggs" goal
      m_health = health;
    }

    // Makes one goal decision from scratch. Returns the tile of the pickup it
    // went for, or (-1,-1) if it chose something else.
    coord decide()
    {
      clearGoalsAndInstructions();
      determineGoals();
      lastRoute = -1;
      coord result(-1, -1);
      for (std::list<AIGoal*>::iterator it = m_goalList.begin(); it != m_goalList.end(); ++it)
      {
        if ((*it)->m_goalType == AIGoal::GOAL_GET_POWERUP)
        {
          result = coord((*it)->m_tileX, (*it)->m_tileY);
          lastRoute = (*it)->m_targetAmount;
        }
      }
      clearGoalsAndInstructions();
      return result;
    }

    // Makes one decision and works out the instructions for it, as the bot
    // would over its next few frames. Returns how many A* searches that took.
    long searchesNeededToDecide()
    {
      const long before = PlayerCharacterEntity::pathSearchesRun();
      clearGoalsAndInstructions();
      determineGoals();
      determineInstructionsFromGoal();
      clearGoalsAndInstructions();
      return PlayerCharacterEntity::pathSearchesRun() - before;
    }

    void advance(int frames) { m_aiFrame += frames; }

    // First step of the A* route, (own tile) if none. Queued bomb instructions
    // are discarded.
    coord astarStep(int x, int y, bool throughBlocks)
    {
      coord step = getNextTileFromPathfinding(x, y, throughBlocks);
      clearGoalsAndInstructions();
      return step;
    }

    std::vector<Route> flood(bool throughBlocks)
    {
      std::vector<Route> routes;
      floodRoutes(throughBlocks, routes);
      return routes;
    }

    int lastRoute = -1; // 0 = walk, 1 = through blocks, of the last decide()
};

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

    // Walls a tile in with blocks on every open side (the border and pillars
    // are walls already; a block must not be put on top of one).
    void boxIn(int x, int y)
    {
      const int sides[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
      for (const auto& side : sides)
      {
        if (Map::getInstance()->staticTileAt(x + side[0], y + side[1]) == Map::EMPTY)
        {
          spawnEntity(BLOCK, 0, x + side[0], y + side[1]);
        }
      }
    }

    // Destroys the BLOCK entity at a tile, the way an explosion would.
    void removeBlock(int x, int y)
    {
      DynamicMap* map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        if (it->second->getType() == BLOCK && it->second->getTileX() == x
            && it->second->getTileY() == y)
        {
          EntityMessageQueue::getInstance()->postDestroyMessage(it->second->getID());
          break;
        }
      }
      em().processMessageQueue();
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
  // The far drop is scanned first (smaller x), so a bot that just took the first
  // pickup it found would go for it.
  setPlayerHealth(1, 13);
  spawnEntity(ENERGY_DROP, 0, 1, 7);
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
  EXPECT_EQ(remainingX, 1);
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


// -- giving up on a pickup, and coming back to it ------------------------------

TEST_F(AISeekEnergyTest, BotThatKeepsGettingNowhereGivesUpOnAPickupThenForgivesIt)
{
  AIProbe bot(1, 1, 60);
  spawnEntity(ENERGY_DROP, 0, 3, 1);  // nearer
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  // The probe never moves, which is what a bot whose GO_TO keeps failing looks
  // like to the next decision: same pickup, no closer.
  EXPECT_EQ(bot.decide(), coord(3, 1));
  EXPECT_EQ(bot.decide(), coord(3, 1));
  EXPECT_EQ(bot.decide(), coord(3, 1));
  EXPECT_EQ(bot.decide(), coord(1, 4)) << "should have given up on (3,1) and taken the other";

  // The give-up is not for ever: whatever was wrong is usually temporary.
  bot.advance(2000);
  EXPECT_EQ(bot.decide(), coord(3, 1)) << "should have forgiven (3,1) by now";
}

TEST_F(AISeekEnergyTest, GettingFewerBlocksAwayCountsAsProgressTowardsAPickup)
{
  // Bot caged by blocks with a drop behind a chain of them. Each bomb that
  // removes a block is progress even though the straight-line distance to the
  // drop has not changed, so the bot must not give up mid-dig.
  AIProbe bot(1, 1, 60);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);
  spawnEntity(BLOCK, 0, 1, 3);
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  EXPECT_EQ(bot.decide(), coord(1, 4));
  EXPECT_EQ(bot.decide(), coord(1, 4));
  EXPECT_EQ(bot.decide(), coord(1, 4));

  removeBlock(1, 2); // one block fewer to get through

  EXPECT_EQ(bot.decide(), coord(1, 4)) << "a block fewer is progress";
  EXPECT_EQ(bot.decide(), coord(1, 4));
  EXPECT_EQ(bot.decide(), coord(1, 4));
}

TEST_F(AISeekEnergyTest, HealthyBotWithAnEggUnderItselfStillGetsOutToAPickupInTime)
{
  // The bot's opening move is to lay an egg where it stands, and a bomb cannot
  // be laid on top of an egg. Boxed in by blocks, it can do nothing about the
  // pickup until the egg hatches - and must not have written the pickup off for
  // good in the meantime.
  setPlayerHealth(1, 60);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  EXPECT_GT(framesUntilFewerThan(ENERGY_DROP, 1, 4000), 0)
      << "bot never got out, stuck at (" << player(1)->getTileX() << ","
      << player(1)->getTileY() << ")";
}

// -- ordinary (15+ health) pickup choice --------------------------------------

TEST_F(AISeekEnergyTest, HealthyBotHeadsForTheNearestPickupNotTheFirstScanned)
{
  AIProbe bot(1, 1, 60);
  spawnEntity(ENERGY_DROP, 0, 1, 4);  // scanned first, three tiles away
  spawnEntity(ENERGY_DROP, 0, 3, 1);  // two tiles away
  EXPECT_EQ(bot.decide(), coord(3, 1));
}

TEST_F(AISeekEnergyTest, HealthyBotPrefersAPickupItCanWalkToOverOneBehindBlocks)
{
  AIProbe bot(1, 1, 60);
  boxIn(3, 1);
  spawnEntity(ENERGY_DROP, 0, 3, 1);  // nearer, walled in
  spawnEntity(ENERGY_DROP, 0, 1, 4);  // farther, open
  EXPECT_EQ(bot.decide(), coord(1, 4));
  EXPECT_EQ(bot.lastRoute, 0);
}

TEST_F(AISeekEnergyTest, HealthyBotWillBombThroughBlocksWhenThatIsTheOnlyWay)
{
  AIProbe bot(1, 1, 60);
  boxIn(3, 1);
  spawnEntity(ENERGY_DROP, 0, 3, 1);
  EXPECT_EQ(bot.decide(), coord(3, 1));
  EXPECT_NE(bot.lastRoute, 0) << "route should be flagged as going through blocks";
}

TEST_F(AISeekEnergyTest, HealthyBotSkipsAHazardousPickupAndOnlyLooksFourTilesOut)
{
  AIProbe bot(1, 1, 60);
  spawnEntity(ENERGY_DROP, 0, 3, 1);
  spawnEntity(MINE, 0, 3, 1);
  spawnEntity(ENERGY_DROP, 0, 7, 7);  // across the board
  EXPECT_EQ(bot.decide(), coord(-1, -1)) << "mined pickup and out-of-range pickup both ignored";

  AIProbe weak(1, 1, 10);  // the same board, but this one has nothing else to live on
  EXPECT_EQ(weak.decide(), coord(7, 7));
}

// -- bombing out on low health is not worth dying for ------------------------

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitSurvivesBombingItselfOutAcrossTheRange)
{
  // Health 12 is the least that is allowed to bomb out. Whatever it takes off
  // the bot (the bomb, the blast) it must live to collect the drop.
  for (int health = 12; health <= 14; health++)
  {
    startRound();
    destroyPlayer(2);
    clearBlocks();
    setPlayerHealth(1, health);
    spawnEntity(BLOCK, 0, 2, 1);
    spawnEntity(BLOCK, 0, 1, 2);
    spawnEntity(ENERGY_DROP, 0, 1, 6);

    EXPECT_GT(framesUntilFewerThan(ENERGY_DROP, 1), 0) << "health " << health << " never got out";
    ASSERT_NE(player(1), nullptr) << "health " << health << " died getting out";
    EXPECT_GT(player(1)->getHealth(), 0);
  }
}

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitBombsOutToAPickupTwoTilesAwayThatTheBlastDoesNotReach)
{
  // The bomb's blast reaches only the four tiles around it, so a drop two
  // tiles off is neither hurt by it nor a chain-blast risk. (Used to be refused
  // for being "near" the bomb, and the bot never got out.)
  for (int health = 12; health <= 14; health++)
  {
    startRound();
    destroyPlayer(2);
    clearBlocks();
    setPlayerHealth(1, health);
    spawnEntity(BLOCK, 0, 2, 1);
    spawnEntity(BLOCK, 0, 1, 2);
    spawnEntity(ENERGY_DROP, 0, 3, 1);

    EXPECT_GT(framesUntilFewerThan(ENERGY_DROP, 1, 1500), 0)
        << "health " << health << " never bombed out to the drop";
    EXPECT_NE(player(1), nullptr) << "health " << health << " died getting out";
  }
}

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitBombsDownACorridorToADropBehindTheBlock)
{
  // The bomb has to be laid from (1,2), a tile away from where the bot starts,
  // with the drop two tiles from it.
  for (int health = 12; health <= 14; health++)
  {
    startRound();
    destroyPlayer(2);
    clearBlocks();
    setPlayerHealth(1, health);
    spawnEntity(BLOCK, 0, 2, 1);
    spawnEntity(BLOCK, 0, 1, 3);
    spawnEntity(ENERGY_DROP, 0, 1, 4);

    EXPECT_GT(framesUntilFewerThan(ENERGY_DROP, 1, 1500), 0)
        << "health " << health << " never bombed through the corridor block";
    EXPECT_NE(player(1), nullptr) << "health " << health << " died getting out";
  }
}

TEST_F(AISeekEnergyTest, BotBelowTheLayLimitDoesNotBombWithAnEggNextToTheBombTile)
{
  // Same corridor, but an enemy egg sits on (1,1), right beside the bomb tile
  // (1,2). The blast would set it off, and its blast would hit the bot again:
  // 9 + 8 is more than this bot has.
  setPlayerHealth(1, 13);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 3);
  spawnEntity(EGG3, 0, 1, 1); // takes 280 frames to hatch
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  for (int frame = 0; frame < 200; frame++)
  {
    runFrames(1);
    ASSERT_EQ(countOfType(BOMB), 0) << "bot bombed beside an egg, frame " << frame;
  }
  EXPECT_EQ(player(1)->getHealth(), 13);
}

TEST_F(AISeekEnergyTest, BotAtTheSameCorridorBombsWhenNothingIsBesideTheBombTile)
{
  // The control for the test above: same board, no egg, and the bot bombs.
  setPlayerHealth(1, 13);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 3);
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  int bombedAt = -1;
  for (int frame = 0; frame < 400 && bombedAt < 0; frame++)
  {
    runFrames(1);
    if (countOfType(BOMB) > 0)
    {
      bombedAt = frame;
    }
  }
  EXPECT_GE(bombedAt, 0);
}

TEST_F(AISeekEnergyTest, PlannerDoesNotSetOutOnARouteWhoseFirstBombWouldBeRefused)
{
  // The first block on the route is bombed from (1,2). With an enemy egg beside
  // that tile a weak bot would refuse to lay the bomb, so it should not choose
  // the route at all (and so waste attempts on it until it gives up).
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 3);
  spawnEntity(ENERGY_DROP, 0, 1, 4);

  AIProbe weak(1, 1, 13);
  EXPECT_EQ(weak.decide(), coord(1, 4)) << "control: nothing beside the bomb tile";

  spawnEntity(EGG3, 0, 1, 1);
  EXPECT_EQ(weak.decide(), coord(-1, -1));

  AIProbe healthy(1, 1, 60); // 15+ health does not chain-blast check
  EXPECT_EQ(healthy.decide(), coord(1, 4));
}

// -- route finding -------------------------------------------------------------

TEST_F(AISeekEnergyTest, FloodFillAgreesWithThePathSearchAboutWhatIsReachable)
{
  // seekPickup classifies pickups with one flood fill and the bot then walks
  // them with A*; the two must agree or the bot would chase unreachable tiles.
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);
  boxIn(5, 3);
  boxIn(7, 7);
  spawnEntity(BLOCK, 0, 3, 5);
  spawnEntity(BLOCK, 0, 3, 3);

  AIProbe bot(1, 1, 60);
  const int width = GameSettings::getInstance()->getMapWidth();
  const int height = GameSettings::getInstance()->getMapHeight();

  for (int throughBlocks = 0; throughBlocks <= 1; throughBlocks++)
  {
    std::vector<AIProbe::Route> routes = bot.flood(throughBlocks != 0);
    for (int x = 0; x < width; x++)
    {
      for (int y = 0; y < height; y++)
      {
        if ((x == 1 && y == 1) || Map::getInstance()->staticTileAt(x, y) == Map::WALL)
        {
          continue;
        }
        const bool astarFindsIt = bot.astarStep(x, y, throughBlocks != 0) != coord(1, 1);
        EXPECT_EQ(routes[y * width + x].reachable, astarFindsIt)
            << "tile (" << x << "," << y << ") throughBlocks=" << throughBlocks;
      }
    }
  }
}

TEST_F(AISeekEnergyTest, PathSearchFreesEveryNodeItAllocates)
{
  // Reachable and unreachable targets, with and without block bombing: each
  // search must leave no path nodes alive (they used to leak on every call).
  boxIn(5, 3);
  spawnEntity(BLOCK, 0, 2, 1);
  spawnEntity(BLOCK, 0, 1, 2);

  AIProbe bot(1, 1, 60);
  ASSERT_EQ(PlayerCharacterEntity::livePathNodes(), 0);

  const int targets[][2] = { {7, 7}, {5, 3}, {3, 1}, {1, 1}, {7, 1} };
  for (const auto& t : targets)
  {
    bot.astarStep(t[0], t[1], false);
    EXPECT_EQ(PlayerCharacterEntity::livePathNodes(), 0) << "walking to (" << t[0] << "," << t[1] << ")";
    bot.astarStep(t[0], t[1], true);
    EXPECT_EQ(PlayerCharacterEntity::livePathNodes(), 0) << "bombing to (" << t[0] << "," << t[1] << ")";
  }
}

TEST_F(AISeekEnergyTest, BotRanksPickupsByTheRouteToThemNotByStraightLineDistance)
{
  // From (1,3): the drop at (1,1) is two tiles off in a straight line but the
  // block at (1,2) sends the bot round the long way (six tiles); the one at
  // (1,6) is three tiles off and straight down the corridor.
  spawnEntity(BLOCK, 0, 1, 2);
  spawnEntity(ENERGY_DROP, 0, 1, 1);
  spawnEntity(ENERGY_DROP, 0, 1, 6);

  AIProbe bot(1, 3, 60);
  EXPECT_EQ(bot.decide(), coord(1, 6));
  EXPECT_EQ(bot.lastRoute, 0);
}

TEST_F(AISeekEnergyTest, ManyUnreachablePickupsOnABigMapNeedNoPathSearchToRuleOut)
{
  // 20 pickups, each walled in, a bot too weak to bomb, on a map the size of the
  // game's largest. Used to take minutes per decision: a full-map A* search per
  // pickup, each flooding the board with duplicate nodes. Now one flood fill
  // classifies them all, and A* only ever runs for the pickup chosen.
  setMapSize(33, 25);
  startRound();
  destroyPlayer(2);
  clearBlocks();

  int placed = 0;
  for (int x = 5; x <= 29 && placed < 20; x += 4)
  {
    for (int y = 5; y <= 21 && placed < 20; y += 4)
    {
      boxIn(x, y);
      spawnEntity(ENERGY_DROP, 0, x, y);
      placed++;
    }
  }

  AIProbe bot(1, 1, 11);
  const auto started = std::chrono::steady_clock::now();
  EXPECT_EQ(bot.searchesNeededToDecide(), 0) << "nothing is reachable, so nothing to search for";
  const auto elapsed = std::chrono::steady_clock::now() - started;
  EXPECT_EQ(bot.decide(), coord(-1, -1));
  EXPECT_EQ(PlayerCharacterEntity::livePathNodes(), 0);
  // Generous: this takes well under a millisecond. The counter is the real check.
  EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 5);

  setPlayerHealth(1, 11);
  runFrames(300);
  EXPECT_EQ(PlayerCharacterEntity::livePathNodes(), 0);
}

TEST_F(AISeekEnergyTest, ChoosingAReachablePickupRunsExactlyOnePathSearch)
{
  // Twenty candidates, one of them reachable: one search, for the chosen one.
  setMapSize(33, 25);
  startRound();
  destroyPlayer(2);
  clearBlocks();

  int placed = 0;
  for (int x = 5; x <= 29 && placed < 19; x += 4)
  {
    for (int y = 5; y <= 21 && placed < 19; y += 4)
    {
      boxIn(x, y);
      spawnEntity(ENERGY_DROP, 0, x, y);
      placed++;
    }
  }
  spawnEntity(ENERGY_DROP, 0, 1, 9); // open ground

  AIProbe bot(1, 1, 11);
  EXPECT_EQ(bot.searchesNeededToDecide(), 1);
}

// -- determinism ---------------------------------------------------------------

TEST_F(AISeekEnergyTest, SameSeedGivesTheSamePickupTiming)
{
  const unsigned int resumeSeed = (unsigned int) std::rand(); // put the harness's RNG back after
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
  std::srand(resumeSeed);
  ASSERT_GT(first, 0);
  EXPECT_EQ(first, second);
}

} // namespace
} // namespace bitriot_test
