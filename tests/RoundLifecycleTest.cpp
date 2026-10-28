// Round-lifecycle tests: the EntityManager/SpawningPool seam.
//
// WinConditionTest passes literal true/false for `moreHostilesPossible` and
// SpawningPoolTest checks canSpawnMore() in isolation. Neither composes the
// two, but GameEngine (stepTick(), via Simulation::step()) does:
//
//     m_pEntityManager->oneOrZeroPlayersRemain(m_pSpawningPool->canSpawnMore())
//
// and it does so at the *top* of the GAME_RUNNING branch, before the pool has
// updated. Bugs in this area live at that seam, so these tests wire the real
// pool to the real win condition exactly as the engine does.

#include "support/HeadlessGame.h"

#include "GameSettings.h"
#include "SpawningPool.h"

namespace bitriot_test
{
namespace
{

class RoundLifecycleTest : public HeadlessGameTest
{
  protected:
    // Exactly what GameEngine asks each tick.
    bool roundIsOver(SpawningPool& pool)
    {
      return em().oneOrZeroPlayersRemain(pool.canSpawnMore());
    }

    void pumpPool(SpawningPool& pool, int ticks)
    {
      for (int i = 0; i < ticks; i++)
      {
        pool.update();
        em().processMessageQueue();
      }
    }

    // A pool sized the way GameEngine sizes it: numPlayers * 100.
    int poolPointsFor(int numPlayers) { return numPlayers * 100; }
};

TEST_F(RoundLifecycleTest, CoopDoesNotEndAtKickoff)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  SpawningPool pool(poolPointsFor(2));

  EXPECT_FALSE(roundIsOver(pool))
      << "a co-op round must not be over on its first tick";
}

TEST_F(RoundLifecycleTest, CoopWithTheSpawnerDisabledDoesNotEndAtKickoff)
{
  // Reachable from the menus: Options -> NPC Spawner off, then put every
  // player on one team. There are no hostiles and there never will be, but the
  // round must still *start* - ending it immediately would mean the scoreboard
  // appears before anyone has moved.
  configurePlayers(2);
  setTeams({0, 0});
  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
  startRound();

  SpawningPool pool(poolPointsFor(2));

  ASSERT_FALSE(em().anyHostileNpcsAlive()) << "precondition: an empty board";
  EXPECT_FALSE(roundIsOver(pool))
      << "spawner off is an enemy-free sandbox, not an instant win";
}

TEST_F(RoundLifecycleTest, CoopSurvivesManyFramesWithTheSpawnerDisabled)
{
  configurePlayers(2);
  setTeams({0, 0});
  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
  startRound();

  SpawningPool pool(poolPointsFor(2));

  for (int i = 0; i < 200; i++)
  {
    pool.update();
    em().runFrame();
    ASSERT_FALSE(roundIsOver(pool)) << "round ended unexpectedly at frame " << i;
  }
}

TEST_F(RoundLifecycleTest, CoopEndsOnceThePoolIsSpentAndTheBoardIsClear)
{
  // The "co-op never ends" fix, exercised through the real pool rather than a
  // literal flag: run a small pool dry, clear whatever it left behind, and the
  // round should conclude.
  configurePlayers(2);
  setTeams({0, 0});
  startRound();
  clearBlocks();

  SpawningPool pool(40); // small enough to exhaust quickly
  pumpPool(pool, 5000);
  ASSERT_FALSE(pool.canSpawnMore()) << "precondition: pool exhausted";

  // Remove any hostiles the pool managed to place, simulating the team having
  // killed them.
  std::vector<int> hostileIds;
  DynamicMap* map = em().getDynamicMap();
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    const EntityGroupType group = it->second->getGroupType();
    if (it->second->getPlayerNumber() == 0
        && (group == EGG_GROUP || group == ROBOT_GROUP || group == PLAYER_SPAWN_GROUP))
    {
      hostileIds.push_back(it->first);
    }
  }
  for (int id : hostileIds)
  {
    EntityMessageQueue::getInstance()->postDestroyMessage(id);
  }
  em().processMessageQueue();

  ASSERT_FALSE(em().anyHostileNpcsAlive());
  EXPECT_GT(em().numPlayersAlive(), 0) << "the team survived";
  EXPECT_TRUE(roundIsOver(pool)) << "board cleared and pool spent: the team has won";
}

TEST_F(RoundLifecycleTest, CoopKeepsRunningWhileThePoolStillHasBudget)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();
  clearBlocks();

  SpawningPool pool(poolPointsFor(2));
  pumpPool(pool, 50);

  ASSERT_TRUE(pool.canSpawnMore());
  EXPECT_FALSE(roundIsOver(pool));
}

TEST_F(RoundLifecycleTest, PvPRoundOutcomeIgnoresThePoolEntirely)
{
  // The pool only gates the co-op branch. A PvP round must end on team count
  // alone, whatever the pool's state.
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  SpawningPool fullPool(poolPointsFor(4));
  SpawningPool emptyPool(0);

  EXPECT_FALSE(roundIsOver(fullPool));
  EXPECT_FALSE(roundIsOver(emptyPool));

  killPlayer(3);
  killPlayer(4);

  EXPECT_TRUE(roundIsOver(fullPool));
  EXPECT_TRUE(roundIsOver(emptyPool));
}

TEST_F(RoundLifecycleTest, CoopEndsWhenTheTeamIsWipedOutEvenWithBudgetLeft)
{
  configurePlayers(3);
  setTeams({0, 0, 0});
  startRound();

  SpawningPool pool(poolPointsFor(3));
  ASSERT_TRUE(pool.canSpawnMore());

  killPlayer(1);
  killPlayer(2);
  EXPECT_FALSE(roundIsOver(pool));

  killPlayer(3);
  EXPECT_TRUE(roundIsOver(pool)) << "the whole team is dead: a loss";
}

} // namespace
} // namespace bitriot_test
