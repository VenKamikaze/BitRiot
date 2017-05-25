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
