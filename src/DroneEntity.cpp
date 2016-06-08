#include "DroneEntity.h"

DroneEntity::DroneEntity(int uniqueID, int team, int atX, int atY)
:SimpleMovingEntity(uniqueID, team, atX, atY)
{
	// constructor
	m_type = DRONE;
	m_groupType = PLAYER_SPAWN_GROUP;

	m_initialAnimCount = DataReader::getInstance()->getIntFromFile("DRONE_ANIM_COUNT", "data/entity.txt");
	m_animCounter = m_initialAnimCount;

	m_health = DataReader::getInstance()->getIntFromFile("DRONE_INITIAL_HEALTH", "data/entity.txt");
	m_velocityModifier = DataReader::getInstance()->getIntFromFile("DRONE_VELOCITY_MODIFIER", "data/entity.txt");
	m_velocity = ((float)m_health + 2)/m_velocityModifier;

	////M2S SOUND DSound::getInstance()->playSound(DSound::DRONE_CREATE); // M2S SOUND
	GameSound::playSound(GameSound::DRONE_CREATE);
}

DroneEntity::~DroneEntity() {}


void DroneEntity::onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith)
{
	SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, teamCollidedWith);
}

void DroneEntity::update()
{
	SimpleMovingEntity::update();
}

