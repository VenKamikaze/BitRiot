#include "RocketSpinEntity.h"

RocketSpinEntity::RocketSpinEntity(int uniqueID, int teamNumber, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = ROCKET_SPIN;
	m_groupType = WEAPON_GROUP;

	m_staticFlag = true;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = 1;
	m_velocity = 0;
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

	m_lifespan = DataReader::getInstance()->getIntFromFile("ROCKET_LIFESPAN", "data/entity.txt");
	//M2S SOUND DSound::getInstance()->playSound(DSound::ROCKET_SPIN);
	GameSound::playSound(GameSound::ROCKET_SPIN);
}

RocketSpinEntity::~RocketSpinEntity()
{
	// destructor
}

void RocketSpinEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	if (typeCollidedWith == EXPLOSION)
	{
		die();
		return;
	}	
}

void RocketSpinEntity::update()
{
	incAnimFrame();
	m_lifespan--;
	if (m_lifespan <= 0)
	{
		die();
	}

	// test for launching in a direction
	Map * staticMap = Map::getInstance();

	bool dirFinished[4] = { false, false, false, false };
	int distance = 0; 

	while ((!dirFinished[0]) || (!dirFinished[1])
			|| (!dirFinished[2]) || (!dirFinished[3]))
	{
		// try each direction in turn once
		// so that rocket targets nearest enemy
		distance++;
		for (int d = 0; d < 4; d++)
		{
			if (dirFinished[d])
				continue;

			DIRECTION dir;
			int xMultiplier, yMultiplier;
			// set these variables
			switch (d)
			{
			case 0:
				dir = UP; xMultiplier = 0; yMultiplier = -1;
				break;
			case 1:
				dir = DOWN; xMultiplier = 0; yMultiplier = 1;
				break;
			case 2:
				dir = LEFT; xMultiplier = -1; yMultiplier = 0;
				break;
			case 3:
				dir = RIGHT; xMultiplier = 1; yMultiplier = 0;
				break;
			}

			// get x and y co-ords to test
			int xTile, yTile;
            xTile = m_tileX + (distance * xMultiplier);
			yTile = m_tileY + (distance * yMultiplier);

			if (staticMap->staticTileAt(xTile, yTile) == Map::EMPTY)
			{
				// test this tile for enemies to launch at
				if (enemyInTile(xTile, yTile))
				{
					fireAt(dir, xTile, yTile);
					return;
				}
			} else {
				// reached a wall or block tile in this direction
				dirFinished[d] = true;
			}


		} // end for each direction

	} // end while loop

}

void RocketSpinEntity::die()
{
	EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
}

void RocketSpinEntity::fireAt(const DIRECTION d, const int destX, const int destY)
{
	int flags = 0;
	switch (d)
	{
	case UP:
		flags = UP_FLAG;
		break;
	case DOWN:
		flags = DOWN_FLAG;
		break;
	case LEFT:
		flags = LEFT_FLAG;
		break;
	case RIGHT:
		flags = RIGHT_FLAG;
		break;
	default:
		break;
	}
	EntityMessageQueue * emq = EntityMessageQueue::getInstance();

	emq->postDestroyMessage(m_ID);
	emq->postCreateMessage(ROCKET_FIRED, m_team, m_tileX, m_tileY, flags, (float)destX, (float)destY);
}

bool RocketSpinEntity::enemyInTile(const int tileX, const int tileY) const
{
	vector<EntityInfo> entities = AIGameView::getInstance()->getEntityInfoAtLocation(tileX, tileY);

	vector<EntityInfo>::iterator it;
	for (it = entities.begin(); it != entities.end(); it++)
	{
		if ( ((it->groupType == CHARACTER_GROUP) ||
			(it->groupType == MONSTER_GROUP) ||
			(it->groupType == PLAYER_SPAWN_GROUP) ||
			(it->groupType == ROBOT_GROUP)) && (it->team != m_team) )
			return true;
	}
	return false;
}
