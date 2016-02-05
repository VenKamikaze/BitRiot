// a player spawned type that is just designed
// to get in everyone else's way and hurt things

#ifndef _DRONEENTITY
#define _DRONEENTITY

#include "SimpleMovingEntity.h"

class DroneEntity : public SimpleMovingEntity
{
public:
	DroneEntity(int uniqueID, int team, int atX, int atY);
	virtual ~DroneEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:


};

#endif