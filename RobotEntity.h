// The RobotEntity class can create
// several different combinations of
// robot appearances and behaviours

#ifndef _ROBOTENTITYCLASS
#define _ROBOTENTITYCLASS

#include "SimpleMovingEntity.h"
#include "AIGameView.h"

class RobotEntity : public SimpleMovingEntity
{
public:
	RobotEntity(int uniqueID, int team, int atX, int atY, unsigned int flags);
	virtual ~RobotEntity() {}

	// overridden virtual methods
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith);

	virtual void update();

private:
	int m_minActionCount, m_maxActionCount;
	int m_actionCounter;

	bool m_layBlocks, m_layMines, m_fireRockets, m_layBombs;
	void performAction(); // does one of the above


};

#endif

