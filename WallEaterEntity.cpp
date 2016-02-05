#include "WallEaterEntity.h"

WallEaterEntity::WallEaterEntity(int uniqueID, int teamNumber, int atX, int atY)
:SimpleMovingEntity(uniqueID, teamNumber, atX, atY)
{
	// constructor
	m_type = WALL_EATER;
	m_walkThroughBlocks = true;
	m_groupType = PLAYER_SPAWN_GROUP;

	m_health = DataReader::getInstance()->getIntFromFile("WALL_EATER_INITIAL_HEALTH", "data/entity.txt");
	m_velocityModifier = DataReader::getInstance()->getIntFromFile("WALL_EATER_VELOCITY_MODIFIER", "data/entity.txt");
	m_velocity = ((float)m_health + 2)/m_velocityModifier;

	//M2S SOUND DSound::getInstance()->playSound(DSound::EATER_CREATE);
	GameSound::playSound(GameSound::EATER_CREATE);
}

WallEaterEntity::~WallEaterEntity()
{}

void WallEaterEntity::onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith)
{
	SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, teamCollidedWith);

	// also
	if (typeCollidedWith == BLOCK)
	{
		m_health -= DataReader::getInstance()->getIntFromFile("WALL_EATER_BLOCK_COST", "data/entity.txt"); // eating a block takes some health
		return;
	}

}

void WallEaterEntity::update()
{
	SimpleMovingEntity::update();
}
