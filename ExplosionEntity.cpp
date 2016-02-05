
#include "ExplosionEntity.h"

ExplosionEntity::ExplosionEntity(int uniqueID, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = 0;

	m_type = EXPLOSION;
	m_groupType = EXPLOSION_GROUP;

	m_staticFlag = true;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = DataReader::getInstance()->getIntFromFile("EXPLOSION_INITIAL_HEALTH", "data/entity.txt");
	m_velocity = 0;
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

}

ExplosionEntity::~ExplosionEntity()
{
	// destructor
}

void ExplosionEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	
}

void ExplosionEntity::update()
{
	m_health--;
	if (m_health <= 0)
		EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
	incAnimFrame();

	// count down to explosion
}