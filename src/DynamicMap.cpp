
#include "DynamicMap.h"

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
  for (entityIterator = entityList.begin(); entityIterator != entityList.end(); entityIterator++)
  {
    GameEntity * e = entityIterator->second;
    delete e;
    entityIterator->second = NULL;
  }
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