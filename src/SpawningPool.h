/**
 * This class is used to
 * semi randomly spawn CPU team
 * entities in each level
 */
#ifndef _SPAWNINGPOOLCLASS
#define _SPAWNINGPOOLCLASS

#include "EntityMessageQueue.h"
#include "DataReader.h"
#include "GameEntity.h"
#include "GameSettings.h"
#include "AIGameView.h"


class SpawningPool
{
  public:
    SpawningPool(int CPUpoints);
    ~SpawningPool();

    void update();

    // True while the pool still has budget to spawn hostile NPCs. Once points
    // are exhausted no more enemies will ever appear, which lets a co-op round
    // end once the board is cleared.
    //
    // Deliberately does NOT consider the NPC spawner toggle. update() suppresses
    // enemy eggs when the toggle is off but returns before deducting points, so
    // with the spawner off this stays true for the whole round - which is what
    // we want: a co-op round with no NPC spawner is an enemy-free sandbox that
    // should keep running, not end on frame one. Folding the toggle in here
    // makes oneOrZeroPlayersRemain() fire at kickoff, because the board is
    // legitimately clear of hostiles before anyone has moved. See
    // RoundLifecycleTest.CoopWithTheSpawnerDisabledDoesNotEndAtKickoff.
    inline bool canSpawnMore() const
    {
      return m_pointsLeft > 0;
    }

  private:
    int m_pointsLeft;
    int m_IQsLeft, m_ENsLeft;

    int m_minDelay, m_maxDelay, m_delayCounter;

    bool getEmptyTile(int & x, int & y);
    EntityType getRandomType();
    unsigned int getRobotFlags();
    int getTypeCost(EntityType type);


};

#endif
