// explosion entity, damages other entities in blast, regardless of team

#ifndef _EXPLOSIONENTITY
#define _EXPLOSIONENTITY

#include "GameEntity.h"

class ExplosionEntity : public GameEntity
{
public:
	ExplosionEntity(int uniqueID, int atX, int atY);
	~ExplosionEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:

};

#endif