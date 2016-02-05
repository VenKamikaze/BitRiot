#include "EnergyDropEntity.h"

EnergyDropEntity::EnergyDropEntity(int uniqueID, int teamNumber, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = ENERGY_DROP;
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

	// //M2S SOUND DSound::getInstance()->playSound(DSound::ENERGY_DROP); // M2S SOUND
	GameSound::playSound(GameSound::ENERGY_DROP);

}

EnergyDropEntity::~EnergyDropEntity()
{

}

void EnergyDropEntity::onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith, 
							 int teamCollidedWith)
{
	if (groupTypeCollidedWith == EXPLOSION_GROUP)
	{
		explode();
		return;
	}

	if (typeCollidedWith == PLAYER_CHARACTER)
	{
		EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
		return;
	}

}

void EnergyDropEntity::update()
{
	incAnimFrame();
}
