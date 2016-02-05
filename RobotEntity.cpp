#include "RobotEntity.h"

RobotEntity::RobotEntity(int uniqueID, int team, int atX, int atY, unsigned int flags)
: SimpleMovingEntity(uniqueID, team, atX, atY)
{
	// constructor
	m_type = ROBOT1;

	m_walkThroughBlocks = (flags & WALK_THROUGH_BLOCKS) ?  true : false;
	m_layBlocks = (flags & LAY_BLOCKS) ? true : false;
	m_layMines = (flags & LAY_MINES) ? true : false;
	m_fireRockets = (flags & FIRE_ROCKETS) ? true : false;
	m_layBombs = (flags & LAY_BOMBS) ? true : false;

	m_groupType = ROBOT_GROUP;

	m_initialAnimCount = DataReader::getInstance()->getIntFromFile("ROBOT_ANIM_COUNT", "data/entity.txt");
	m_animCounter = m_initialAnimCount;

	m_health = DataReader::getInstance()->getIntFromFile("ROBOT_INITIAL_HEALTH", "data/entity.txt");
	m_velocityModifier = DataReader::getInstance()->getIntFromFile("ROBOT_VELOCITY_MODIFIER", "data/entity.txt");
	m_velocity = ((float)m_health + 2)/m_velocityModifier;

	// set action counter thing
	m_minActionCount = DataReader::getInstance()->getIntFromFile("ROBOT_MIN_ACTION_COUNT", "data/entity.txt");
	m_maxActionCount = DataReader::getInstance()->getIntFromFile("ROBOT_MAX_ACTION_COUNT", "data/entity.txt");
	if (m_minActionCount > m_maxActionCount)
		m_minActionCount = m_maxActionCount;

	int range = m_maxActionCount - m_minActionCount;

	m_actionCounter = m_minActionCount + (rand()%range);
	//M2S SOUND DSound::getInstance()->playSound(DSound::ROBOT_CREATE);
	GameSound::playSound(GameSound::ROBOT_CREATE);

}

void RobotEntity::onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith)
{
	SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, teamCollidedWith);
}

void RobotEntity::update()
{
	SimpleMovingEntity::update();

	// test for action
	m_actionCounter--;
	if (m_actionCounter <= 0)
	{
		performAction();
		int range = m_maxActionCount - m_minActionCount;
        m_actionCounter = m_minActionCount + (rand()%range);
	}	
}

void RobotEntity::performAction()
{
	if (Map::getInstance()->staticTileAt(m_tileX, m_tileY) != Map::EMPTY)
		return; // this is not a statically empty block, try again later
	// check for static entities
	vector<EntityInfo> infoVec = AIGameView::getInstance()->getEntityInfoAtLocation(m_tileX, m_tileY);
	vector<EntityInfo>::iterator it;
	for (it = infoVec.begin(); it != infoVec.end(); it++)
	{
		// check for static entities
		if (it->staticFlag == true)
			return;
	}

	// check we're in a "crossroads" tile
	int x = (m_tileX - 1)%2;
	int y = (m_tileY - 1)%2;

	if ((x != 0) || (y != 0))
		return;



	EntityMessageQueue * emq = EntityMessageQueue::getInstance();
	// ok to do whatever it if
	if (m_layBlocks)
		emq->postCreateMessage(BLOCK, m_team, m_tileX, m_tileY, 0); 
	else if (m_layMines)
		emq->postCreateMessage(MINE, m_team, m_tileX, m_tileY, 0); 
	else if (m_fireRockets)
		emq->postCreateMessage(ROCKET_SPIN, m_team, m_tileX, m_tileY, 0);
	else if (m_layBombs)
		emq->postCreateMessage(BOMB, m_team, m_tileX, m_tileY, 0);
	else
		return; // no action flags

	if (m_team != 0)
	{
		// if not cpu team, it costs some health
		m_health -= DataReader::getInstance()->getIntFromFile("ROBOT_ACTION_COST", "data/entity.txt");
	}
	return;
}
