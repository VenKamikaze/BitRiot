#include "EffectEntity.h"

EffectEntity::EffectEntity(int uniqueID, EntityType effectType, int atX, int atY, float offX, float offY)
{
	// constructor
	m_ID = uniqueID;
	m_team = 0; // team does not matter, set to cpu

	m_type = effectType;
	m_origAnimCounter = DataReader::getInstance()->getIntFromFile("BASE_EFFECT_ANIM_COUNT", "data/entity.txt");

	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

	switch (m_type)
	{
	case MUNCH_EFFECT:
		// expires after this time
		m_health = DataReader::getInstance()->getIntFromFile("MUNCH_EFFECT_HEALTH", "data/entity.txt");
		break;
	case DAMAGE_EFFECT:
		m_health = DataReader::getInstance()->getIntFromFile("DAMAGE_EFFECT_HEALTH", "data/entity.txt");;
		m_currentAnimFrame = rand()%5; // pick random frame
		break;
	default:
		break;
	}

	m_animCounter = m_origAnimCounter;

	m_groupType = EFFECT_GROUP;
	m_solidFlag = false;
	m_staticFlag = false;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = offX;
	m_offsetY = offY;
	m_velocity = 0;
	
}

void EffectEntity::update()
{	
	m_animCounter--;
	if (m_animCounter <= 0)
	{
		m_animCounter = m_origAnimCounter;
		incAnimFrame();
	}

	m_health--;
	if (m_health <= 0)
	{
		EntityMessageQueue * emq = EntityMessageQueue::getInstance();
		emq->postDestroyMessage(m_ID);
	}
}
