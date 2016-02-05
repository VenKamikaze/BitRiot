// Wall Eater Entity
// one of the entity types a player can spawn
// can move through blocks and destorys them in the process, hence "wall eater"

#ifndef _WALLEATERENTITY
#define _WALLEATERENTITY

#include "SimpleMovingEntity.h"

class WallEaterEntity : public SimpleMovingEntity
{
public:
	WallEaterEntity(int uniqueID, int team, int atX, int atY);
	virtual ~WallEaterEntity();
	
	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:

};

#endif
