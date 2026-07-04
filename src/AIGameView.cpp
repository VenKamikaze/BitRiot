#include "AIGameView.h"
#include "MickLogger.h"
#include <string>

#ifndef NULL
#define NULL 0
#endif

AIGameView * AIGameView::getInstance()
{
  static AIGameView instance;
  return &instance;
}

void AIGameView::setMapSize(int width, int height)
{
  // This singleton outlives individual games, but DynamicMap (whose constructor
  // calls this once per game) is rebuilt from scratch each game without posting
  // DESTROY messages for still-alive entities. Any EntityInfo left over from a
  // previous game must be dropped here, or it becomes a permanent untargetable
  // "ghost" that AI lookups (e.g. RocketSpinEntity::enemyInTile) will keep
  // matching at its old tile forever.
  m_pEntityMap->clear();
  m_pEntityMap->resize(width);
  for (int x = 0; x < width; x++)
  {
    m_pEntityMap->at(x).resize(height);
  }
}

AIGameView::AIGameView()
{
  m_pEntityMap = new vector< vector< vector< EntityInfo > > >;
}

AIGameView::~AIGameView()
{
  if (m_pEntityMap)
  {
    delete m_pEntityMap;
  }
  m_pEntityMap = NULL;
}

const vector< EntityInfo > & AIGameView::getEntityInfoAtLocation(const int tileX, const int tileY) const
{
  return m_pEntityMap->at(tileX).at(tileY);
}

bool AIGameView::addNewEntity(const EntityInfo & r_info, const int tileX, const int tileY)
{
  m_pEntityMap->at(tileX).at(tileY).push_back(r_info);
  return true;
}

bool AIGameView::moveEntity(const int id, const int oldX, const int oldY, const int newX, const int newY)
{
  // check entity is at old map location
  vector<EntityInfo>::iterator locationIterator;
  for (locationIterator = m_pEntityMap->at(oldX).at(oldY).begin();
       locationIterator != m_pEntityMap->at(oldX).at(oldY).end();
       locationIterator++)
  {
    if (locationIterator->id == id)
    {
      // entity found here

      // get info
      EntityInfo info = (*locationIterator);
      // erase old position data
      m_pEntityMap->at(oldX).at(oldY).erase(locationIterator);
      // insert copied info to new position
      m_pEntityMap->at(newX).at(newY).push_back(info);

      return true;
    }
  }

  // not found - this entity's AIGameView entry is now stale/desynced from its real
  // position (assert() below is compiled out in release/NDEBUG builds, so this can
  // fail silently there - log unconditionally as it should never happen).
  std::string msg = std::string("AIGameView::moveEntity desync: id=") + std::to_string(id)
    + " not found at oldX,oldY=(" + std::to_string(oldX) + "," + std::to_string(oldY) + ")"
    + " attempting move to (" + std::to_string(newX) + "," + std::to_string(newY) + ")";
  MickLogger::getInstance()->warn(nullptr, msg);
  assert("AIGameView moveEntity() failed to find entity!" && 0);
  return false;

}

bool AIGameView::removeEntity(const int id, const int tileX, const int tileY)
{
  // check entity is at old map location
  vector<EntityInfo>::iterator locationIterator;
  for (locationIterator = m_pEntityMap->at(tileX).at(tileY).begin();
       locationIterator != m_pEntityMap->at(tileX).at(tileY).end();
       locationIterator++)
  {
    if (locationIterator->id == id)
    {
      // entity found here
      m_pEntityMap->at(tileX).at(tileY).erase(locationIterator);
      return true;
    }
  }

  // not found - this stale entry remains in the per-tile entity map forever, where it
  // will keep matching as a live "enemy" to lookups such as
  // RocketSpinEntity::enemyInTile - a candidate cause of rockets firing at nothing.
  std::string msg = std::string("AIGameView::removeEntity desync: id=") + std::to_string(id)
    + " not found at (" + std::to_string(tileX) + "," + std::to_string(tileY) + ")";
  MickLogger::getInstance()->warn(nullptr, msg);
  assert("AIGameView removeEntity() failed to find entity!" && 0);
  return false;
}

