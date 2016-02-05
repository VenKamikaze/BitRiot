// shared base class for concrete class entities that share 
// a lot of functionality

#ifndef _SIMPLEMOVINGENTITY
#define _SIMPLEMOVINGENTITY

#include "GameEntity.h"
#include "SimpleBrain.h"

class SimpleMovingEntity : public GameEntity
{
public:
	SimpleMovingEntity(int uniqueID, int teamNumber, int atX, int atY, 
		float offsetX = 0.0f, float offsetY = 0.0f);
	virtual ~SimpleMovingEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

protected:
	SimpleBrain * m_pBrain;

	int m_velocityModifier;

	int m_animCounter;
	int m_initialAnimCount;

	int m_decayCounter;
	int m_initialDecayCount;

};

#endif