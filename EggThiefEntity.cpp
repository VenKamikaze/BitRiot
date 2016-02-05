
#include "EggThiefEntity.h"

EggThiefEntity::EggThiefEntity(int uniqueID, int teamNumber, int atX, int atY)
:SimpleMovingEntity(uniqueID, teamNumber, atX, atY)
{
	// constructor
	m_type = EGG_THIEF;
	m_groupType = PLAYER_SPAWN_GROUP;

	m_health = DataReader::getInstance()->getIntFromFile("EGG_THIEF_INITIAL_HEALTH", "data/entity.txt");

	// //M2S SOUND DSound::getInstance()->playSound(DSound::THIEF_CREATE); // M2S SOUND
	GameSound::playSound(GameSound::THIEF_CREATE);
}

EggThiefEntity::~EggThiefEntity()
{}

void EggThiefEntity::onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith)
{
	SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, teamCollidedWith);
}

void EggThiefEntity::update()
{
	SimpleMovingEntity::update();
}
