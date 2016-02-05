// EggTheifEntity
// a moving entity that converts eggs to it's team (this is handled in egg entity collision function)

#ifndef _EGGTHIEFENTITY
#define _EGGTHIEFENTITY

#include "SimpleMovingEntity.h"


class EggThiefEntity : public SimpleMovingEntity
{
public:
	EggThiefEntity(int uniqueID, int team, int atX, int atY);
	virtual ~EggThiefEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:	

};

#endif