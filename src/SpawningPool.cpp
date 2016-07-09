#include "SpawningPool.h"


SpawningPool::SpawningPool(int CPUpoints)
{
  m_pointsLeft = CPUpoints;

  m_minDelay = DataReader::getInstance()->getIntFromFile("CPU_SPAWN_MIN_DELAY", "data/entity.txt");
  m_maxDelay = DataReader::getInstance()->getIntFromFile("CPU_SPAWN_MAX_DELAY", "data/entity.txt");

  m_ENsLeft = DataReader::getInstance()->getIntFromFile("NUM_ENPLUS_PLAYER", "data/entity.txt");
  m_IQsLeft = 0; //disabled for now // DataReader::getInstance()->getIntFromFile("NUM_IQPLUS_PLAYER", "data/entity.txt");

  if (m_minDelay > m_maxDelay)
  {
    m_minDelay = m_maxDelay - 1;
  }

  m_delayCounter = m_minDelay + (rand()%(m_maxDelay - m_minDelay));
}

SpawningPool::~SpawningPool()
{

}

void SpawningPool::update()
{
  if (m_pointsLeft <= 0)
  {
    return;
  }

  m_delayCounter--;

  if (m_delayCounter <= 0)
  {
    // reset counter
    m_delayCounter = m_minDelay + (rand()%(m_maxDelay - m_minDelay));

    // try to spawn cpu entity

    int atX = 1;
    int atY = 1;
    bool spotFound = false;
    // try a few times to get an empty tile, else try again in a while
    for (int i = 0; i < 13; i++)
    {
      if (getEmptyTile(atX, atY) == true)
      {
        spotFound = true;
        break;
      }
    }
    if (spotFound == false)
    {
      return;
    }

    EntityMessageQueue * emq = EntityMessageQueue::getInstance();

    int pointsCost = 0;
    EntityType type;
    unsigned int flags = 0; // for special flags

    type = getRandomType();

    if (type == EGG5)
    {
      flags = getRobotFlags();
      if (flags & GameEntity::WALK_THROUGH_BLOCKS)
      {
        pointsCost += DataReader::getInstance()->getIntFromFile("WALK_THROUGH_BLOCKS_COST", "data/entity.txt");
      }
    }

    pointsCost += getTypeCost(type);

    // create type

    emq->postCreateMessage(type, 0, atX, atY, flags);

    m_pointsLeft -= pointsCost;
    if (m_pointsLeft < 0)
    {
      m_pointsLeft = 0;
    }

  }

}

int SpawningPool::getTypeCost(EntityType type)
{
  DataReader * data = DataReader::getInstance();
  switch (type)
  {
    case EGG1:
      return data->getIntFromFile("EGG1_COST", "data/player.txt");
    case EGG2:
      return data->getIntFromFile("EGG2_COST", "data/player.txt");
    case EGG3:
      return data->getIntFromFile("EGG3_COST", "data/player.txt");
    case EGG4:
      return data->getIntFromFile("EGG4_COST", "data/player.txt");
    case EGG5:
      return data->getIntFromFile("EGG5_COST", "data/player.txt");
    case ENPLUS_POWERUP:
    case IQPLUS_POWERUP:
      return 0;
    default:
      return 10;
  }

}

EntityType SpawningPool::getRandomType()
{
  int roll = rand()%100;

  int powerupChance = DataReader::getInstance()->getIntFromFile("SPAWN_POWERUP_CHANCE", "data/entity.txt");
  // first consider powerups
  if (m_ENsLeft > 0)
  {

    if (roll < powerupChance)
    {
      // spawn EN+ powerup
      m_ENsLeft--;
      return ENPLUS_POWERUP;
    }
  }

  if (m_IQsLeft > 0)
  {
    if (m_ENsLeft > 0)
    {
      powerupChance *= 2;
    }

    if (roll < powerupChance)
    {
      // spawn EN+ powerup
      m_IQsLeft--;
      return IQPLUS_POWERUP;
    }

  }

  roll = rand()%100;

  int eggChance[5];
  eggChance[0] = DataReader::getInstance()->getIntFromFile("SPAWN_EGG1_CHANCE", "data/entity.txt");
  eggChance[1] = DataReader::getInstance()->getIntFromFile("SPAWN_EGG2_CHANCE", "data/entity.txt");
  eggChance[2] = DataReader::getInstance()->getIntFromFile("SPAWN_EGG3_CHANCE", "data/entity.txt");
  eggChance[3] = DataReader::getInstance()->getIntFromFile("SPAWN_EGG4_CHANCE", "data/entity.txt");
  eggChance[4] = DataReader::getInstance()->getIntFromFile("SPAWN_EGG5_CHANCE", "data/entity.txt");

  if (roll < eggChance[0])
  {
    return EGG1;
  }

  roll -= eggChance[0];
  if (roll < eggChance[1])
  {
    return EGG2;
  }

  roll -= eggChance[1];
  if (roll <= eggChance[2])
  {
    return EGG3;
  }

  roll -= eggChance[2];
  if (roll <= eggChance[3])
  {
    return EGG4;
  }

  return EGG5;
}

bool SpawningPool::getEmptyTile(int & x, int & y)
{
  x = 1 + (rand()%(Map::MAP_WIDTH - 1));
  y = 1 + (rand()%(Map::MAP_HEIGHT - 1));

  if (Map::getInstance()->staticTileAt(x, y) == Map::EMPTY)
  {
    vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
    bool okSpot = true;
    vector<EntityInfo>::iterator it;
    for (it = info.begin(); it != info.end(); it++)
    {
      if (it->staticFlag == true)
      {
        okSpot = false;
        break;
      }
    }
    if (okSpot)
    {
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    return false;
  }
}

unsigned int SpawningPool::getRobotFlags()
{
  unsigned int flagsToSet = 0;
  DataReader * data = DataReader::getInstance();

  int roll1 = rand()%100;

  if (roll1 < data->getIntFromFile("WALK_THROUGH_BLOCKS_CHANCE", "data/entity.txt"))
  {
    flagsToSet = flagsToSet | GameEntity::WALK_THROUGH_BLOCKS;
  }

  int roll2 = rand()%100;

  if (roll2 < data->getIntFromFile("LAY_BLOCKS_CHANCE", "data/entity.txt"))
  {
    flagsToSet = flagsToSet | GameEntity::LAY_BLOCKS;
    return flagsToSet;
  }

  roll2 -= data->getIntFromFile("LAY_BLOCKS_CHANCE", "data/entity.txt");

  if (roll2 < data->getIntFromFile("LAY_MINES_CHANCE", "data/entity.txt"))
  {
    flagsToSet = flagsToSet | GameEntity::LAY_MINES;
    return flagsToSet;
  }

  roll2 -= data->getIntFromFile("LAY_MINES_CHANCE", "data/entity.txt");

  if (roll2 < data->getIntFromFile("LAY_BOMBS_CHANCE", "data/entity.txt"))
  {
    flagsToSet = flagsToSet | GameEntity::LAY_BOMBS;
    return flagsToSet;
  }

  // else
  flagsToSet = flagsToSet | GameEntity::FIRE_ROCKETS;
  return flagsToSet;
}