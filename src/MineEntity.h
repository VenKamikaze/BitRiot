 // a mine is another weapon
// similar to bomb but only explodes on contact with enemy
// and lasts longer

#ifndef _MINEENTITY
#define _MINEENTITY

#include "GameEntity.h"

class MineEntity : public GameEntity
{
public:
	MineEntity(int id,int team, int atX, int atY);
	~MineEntity();

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();	
	static const int ANIM_DELAY_MAX = 15;


private:
	int m_fuse, m_animDelay;

};

#endif