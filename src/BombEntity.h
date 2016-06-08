// Bomb entity, a player weapon that explodes after a set period of time

#ifndef _BOMBENTITY
#define _BOMBENTITY

#include "GameEntity.h"

class BombEntity : public GameEntity
{
public:
	BombEntity(int uniqueID, int teamNumber, int atX, int atY);
	virtual ~BombEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();


private:
	int m_fuse;

};

#endif