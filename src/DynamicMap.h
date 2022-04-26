#ifndef _DYNAMICMAPCLASS
#define _DYNAMICMAPCLASS

#include <map>
#include <memory>
using namespace std;

#include "GameEntity.h"
#include "AIGameView.h"

class DynamicMap
{
  public:
    DynamicMap(int width, int height);
    ~DynamicMap();

    bool moveEntityOnMap(int id, int oldX, int oldY, int newX, int newY); // moves entity on dynamic map
    bool tileHasStaticEntity(int tileX, int tileY); // returns true if static entity at x,y

    map<int, shared_ptr<GameEntity>> entityList; // maps unique ids to entities
    map<int, shared_ptr<GameEntity>>::iterator entityIterator; // iterator for above
    vector< vector< vector< int > > > entityMap; // map of entity id locations, at x, y position
    vector< vector< vector< int > > >::iterator mapIterator;
    vector<int>::iterator mapLocationIterator;

};

#endif