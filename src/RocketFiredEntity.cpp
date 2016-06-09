#include "RocketFiredEntity.h"
#include <cmath>

RocketFiredEntity::RocketFiredEntity(int uniqueID, int teamNumber, int atX, int atY, 
									 unsigned int flags, float fToX, float fToY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = ROCKET_FIRED;
	m_groupType = WEAPON_GROUP;

	m_staticFlag = false;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = 10;

	m_velocity = (float)m_health/(float)DataReader::getInstance()->getIntFromFile("ROCKET_VELOCITY", "data/entity.txt");

	m_dir = UP;
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

	// set direction from flags
	if (flags & GameEntity::UP_FLAG)
	{
		m_dir = UP;
		m_currentAnimState = EntityRenderer::MOVE_UP;
	} 
	else if (flags & GameEntity::DOWN_FLAG)
	{
		m_dir = DOWN;
		m_currentAnimState = EntityRenderer::MOVE_DOWN;
	}
	else if (flags & GameEntity::LEFT_FLAG)
	{
		m_dir = LEFT;
		m_currentAnimState = EntityRenderer::MOVE_LEFT;
	}
	else if (flags & GameEntity::RIGHT_FLAG)
	{
		m_dir = RIGHT;
		m_currentAnimState = EntityRenderer::MOVE_RIGHT;
	}
	// set target tile
	m_toX = (int)fToX; // although a cast from an int to a float back to an int isn't ideal,
	m_toY = (int)fToY; // it saves us from introducing rarely used parameters in EntityFactory

	//M2S SOUND DSound::getInstance()->playSound(DSound::ROCKET_FIRED);
	GameSound::playSound(GameSound::ROCKET_FIRED);
}

RocketFiredEntity::~RocketFiredEntity() 
{}

void RocketFiredEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	if ((teamCollidedWith != m_team && (
		(groupTypeCollidedWith == CHARACTER_GROUP) ||
		(groupTypeCollidedWith == MONSTER_GROUP) ||
		(groupTypeCollidedWith == PLAYER_SPAWN_GROUP) ||
		(groupTypeCollidedWith == ROBOT_GROUP)))
		|| (typeCollidedWith == EXPLOSION))
	{
		explode();
	}	
}

void RocketFiredEntity::update()
{
	// move in direction
	moveInDirection(m_dir);

	// if in destination, explode
	if ((m_tileX == m_toX) && (m_tileY == m_toY) && (abs(m_offsetX) <= m_velocity) && (abs(m_offsetY) <= m_velocity))
		explode();

	// if in center of tile, and next tile is a block, explode
	if ((abs(m_offsetX) <= m_velocity) && (abs(m_offsetY) <= m_velocity))
	{
		// get next tile
		Map * map = Map::getInstance();
		Map::StaticTile nextTile = Map::EMPTY;
		switch (m_dir)
		{
		case UP:
			nextTile = map->staticTileAt(m_tileX, m_tileY - 1);
			break;
		case DOWN:
			nextTile = map->staticTileAt(m_tileX, m_tileY + 1);
			break;
		case LEFT:
			nextTile = map->staticTileAt(m_tileX - 1, m_tileY);
			break;
		case RIGHT:
			nextTile = map->staticTileAt(m_tileX + 1, m_tileY);
			break;
		}
		if ((nextTile == Map::WALL) || (nextTile == Map::BLOCK))
			explode();
	}
}
