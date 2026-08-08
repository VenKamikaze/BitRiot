// SpawningPool tests.
//
// The pool is the purple CPU "team": it drips hostile eggs onto the map on a
// random delay until its points budget runs out. canSpawnMore() is what tells
// the co-op win condition whether more enemies can still appear, so its two
// exhaustion paths (points spent, spawner toggled off) both matter.

#include "support/HeadlessGame.h"

#include "GameSettings.h"
#include "SpawningPool.h"

namespace bitriot_test
{
namespace
{

class SpawningPoolTest : public HeadlessGameTest
{
  protected:
    // The pool posts create messages rather than building entities directly, so
    // the queue has to be pumped for spawns to land on the map.
    void pumpPool(SpawningPool& pool, int ticks)
    {
      for (int i = 0; i < ticks; i++)
      {
        pool.update();
        em().processMessageQueue();
      }
    }
};

TEST_F(SpawningPoolTest, AFreshPoolCanSpawn)
{
  SpawningPool pool(400);
  EXPECT_TRUE(pool.canSpawnMore());
}

TEST_F(SpawningPoolTest, APoolWithNoPointsCannotSpawn)
{
  SpawningPool pool(0);
  EXPECT_FALSE(pool.canSpawnMore());
}

TEST_F(SpawningPoolTest, CanSpawnMoreReportsBudgetAndIgnoresTheSpawnerToggle)
{
  // canSpawnMore() answers "is there budget left", not "will an enemy appear".
  // The distinction matters: with the toggle off, update() suppresses enemy
  // eggs but returns before deducting points, so the budget never drains and
  // this stays true for the whole round. That is intentional - see the comment
  // on canSpawnMore() and RoundLifecycleTest, where folding the toggle in here
  // was shown to end a spawner-off co-op round at kickoff.
  SpawningPool pool(400);
  ASSERT_TRUE(pool.canSpawnMore());

  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
  EXPECT_TRUE(pool.canSpawnMore()) << "the toggle does not consume budget";

  std::GameSettings::getInstance()->setNpcSpawnerEnabled(true);
  EXPECT_TRUE(pool.canSpawnMore());
}

TEST_F(SpawningPoolTest, ADisabledPoolNeverDrainsItsBudget)
{
  configurePlayers(2);
  startRound();
  clearBlocks();

  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);

  SpawningPool pool(60); // would be spent well inside this many ticks if enabled
  pumpPool(pool, 5000);

  EXPECT_TRUE(pool.canSpawnMore())
      << "suppressed egg spawns must not cost points";
}

TEST_F(SpawningPoolTest, AnEnabledPoolEventuallyPutsHostilesOnTheBoard)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();
  clearBlocks(); // maximise the chance of finding an empty tile quickly

  ASSERT_EQ(countEntities(EGG_GROUP, 0), 0);

  SpawningPool pool(400);
  pumpPool(pool, 2000);

  EXPECT_GT(countEntities(EGG_GROUP, 0), 0) << "the pool should have dripped CPU eggs";
  EXPECT_TRUE(em().anyHostileNpcsAlive());
}

TEST_F(SpawningPoolTest, ADisabledPoolPutsNoHostilesOnTheBoard)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();
  clearBlocks();

  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);

  SpawningPool pool(400);
  pumpPool(pool, 2000);

  EXPECT_EQ(countEntities(EGG_GROUP, 0), 0) << "the toggle must suppress enemy eggs";
  EXPECT_FALSE(em().anyHostileNpcsAlive());
}

TEST_F(SpawningPoolTest, ADisabledPoolStillLetsPowerUpsThrough)
{
  // Documented behaviour of the toggle: it stops enemies, not pick-ups.
  configurePlayers(2);
  startRound();
  clearBlocks();

  std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);

  SpawningPool pool(400);
  pumpPool(pool, 4000);

  EXPECT_GT(countEntities(POWERUP_GROUP), 0)
      << "EN+/IQ+ pick-ups are exempt from the NPC spawner toggle";
}

TEST_F(SpawningPoolTest, PoolSpawnsExhaustItsBudget)
{
  configurePlayers(2);
  startRound();
  clearBlocks();

  SpawningPool pool(60); // deliberately small
  pumpPool(pool, 5000);

  EXPECT_FALSE(pool.canSpawnMore()) << "a small budget should be spent within 5000 ticks";
}

TEST_F(SpawningPoolTest, ExhaustedPoolStopsProducingHostiles)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();
  clearBlocks();

  SpawningPool pool(60);
  pumpPool(pool, 5000);
  ASSERT_FALSE(pool.canSpawnMore());

  const int hostilesAfterExhaustion = countEntities(EGG_GROUP, 0);
  pumpPool(pool, 2000);

  EXPECT_EQ(countEntities(EGG_GROUP, 0), hostilesAfterExhaustion)
      << "no new CPU eggs once the budget is gone";
}

} // namespace
} // namespace bitriot_test
