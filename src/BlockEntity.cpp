// implementation of BlockEntity class

#include "BlockEntity.h"

BlockEntity::BlockEntity(int uniqueID, int teamNumber, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = BLOCK;
	m_groupType = BLOCK_GROUP;
	m_solidFlag = true;
	m_staticFlag = true;

	m_tileX = atX;
	m_tileY = atY;
	Map::getInstance()->setBlockAt(atX, atY, true);

	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = 1;
	m_velocity = 0;
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

	// play creation sound
	////M2S SOUND DSound::getInstance()->playSound(DSound::BLOCK_DROP); M2S SOUND
	GameSound::playSound(GameSound::BLOCK_DROP);
}

BlockEntity::~BlockEntity()
{
	// destructor
	Map::getInstance()->setBlockAt(m_tileX, m_tileY, false);
}

void BlockEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{

	if (typeCollidedWith == EXPLOSION)
	{
		EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
		return;
	}
	if (typeCollidedWith == WALL_EATER)
	{
		// wall eater destroys any blocks in contact
		// munch effect!		
		EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
		////M2S SOUND DSound::getInstance()->playSound(DSound::MUNCHY); // M2S SOUND
		GameSound::playSound(GameSound::MUNCHY);
		EntityMessageQueue::getInstance()->postCreateMessage(MUNCH_EFFECT, m_team, m_tileX, m_tileY, 0);
		return;
	}

}

void BlockEntity::update()
{
	return;
}
