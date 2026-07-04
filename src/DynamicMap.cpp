
#include "DynamicMap.h"
#include "MickLogger.h"
#include <string>

DynamicMap::DynamicMap(int width, int height)
{
  entityMap.resize(width);
  for (int x = 0; x < width; x++)
  {
    entityMap[x].resize(height);
  }
  // set size of AI game view map
  AIGameView::getInstance()->setMapSize(width, height);
}

DynamicMap::~DynamicMap()
{
  // delete any entities in entityList map
  /*for (entityIterator = entityList.begin(); entityIterator != entityList.end(); entityIterator++)
  {
    GameEntity * e = entityIterator->second;
    delete e;
    entityIterator->second = NULL;
  }*/
}

bool DynamicMap::moveEntityOnMap(int id, int oldX, int oldY, int newX, int newY)
{
  // moves entity on dynamic map

  // check entity is at old map location
  for (mapLocationIterator = entityMap[oldX][oldY].begin();
       mapLocationIterator != entityMap[oldX][oldY].end();
       mapLocationIterator++)
  {
    if ((*mapLocationIterator) == id)
    {
      entityMap[oldX][oldY].erase(mapLocationIterator);
      entityMap[newX][newY].push_back(id);
      // also more on AIGameView
      return AIGameView::getInstance()->moveEntity(id, oldX, oldY, newX, newY);
    }
  }

  // desync: entity id was expected at (oldX,oldY) but wasn't found there.
  // Its AIGameView entry (and this entityMap bucket) will now be stale/out of
  // sync with the entity's real position - a candidate cause of "ghost" targets.
  std::string msg = std::string("DynamicMap::moveEntityOnMap desync: id=") + std::to_string(id)
    + " not found at oldX,oldY=(" + std::to_string(oldX) + "," + std::to_string(oldY) + ")"
    + " attempting move to (" + std::to_string(newX) + "," + std::to_string(newY) + ")";
  MickLogger::getInstance()->warn(nullptr, msg);

  return false; // entity not found at oldX, oldY
}

bool DynamicMap::tileHasStaticEntity(int tileX, int tileY)
{
  for (mapLocationIterator = entityMap[tileX][tileY].begin();
       mapLocationIterator != entityMap[tileX][tileY].end();
       mapLocationIterator++)
  {
    int id = (*mapLocationIterator);
    if (entityList.find(id)->second->getStaticFlag() == true)
    {
      return true;
    }
  }

  return false;
}