// BlockEntity.h

#ifndef _BLOCKENTITYCLASS
#define _BLOCKENTITYCLASS

#include "GameEntity.h"

class BlockEntity : public GameEntity
{
public:
	BlockEntity(int uniqueID, int teamNumber, int atX, int atY);
	~BlockEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

protected:

};


#endif
