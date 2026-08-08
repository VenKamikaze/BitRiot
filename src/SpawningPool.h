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

    // True while the pool can still produce hostile NPCs, which lets a co-op
    // round end once the board is cleared. Two ways it can dry up:
    //  - points exhausted: nothing left to buy enemies with;
    //  - the NPC spawner toggle is off: update() suppresses enemy eggs and
    //    returns *before* deducting points, so the budget never drains and
    //    m_pointsLeft alone would stay positive forever.
    inline bool canSpawnMore() const
    {
      return m_pointsLeft > 0 && GameSettings::getInstance()->isNpcSpawnerEnabled();
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
