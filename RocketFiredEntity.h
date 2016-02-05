// rocket spin entity launches a rocket fired entity
#ifndef _ROCKETFIREDENTITY
#define _ROCKETFIREDENTITY

#include "GameEntity.h"

class RocketFiredEntity : public GameEntity
{
public:
	RocketFiredEntity(int uniqueID, int teamNumber, int atX, int atY, 
		unsigned int flags, float toX, float toY);
	virtual ~RocketFiredEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:
	DIRECTION m_dir;
	int m_toX, m_toY; // used to explode rocket in tile targeted by RocketSpinEntity creator



};

#endif