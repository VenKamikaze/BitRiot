 
#include "BombEntity.h"

BombEntity::BombEntity(int uniqueID, int teamNumber, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = BOMB;
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

	m_fuse = DataReader::getInstance()->getIntFromFile("BOMB_FUSE", "data/entity.txt");
	////M2S SOUND DSound::getInstance()->playSound(DSound::BOMB_FUSE); // M2S SOUND
	//MickBaseSound::getInstance()->playSound("audio/newbombfuse1.wav");
	GameSound::playSound(GameSound::BOMB_FUSE);
}

BombEntity::~BombEntity()
{
	// destructor
}

void BombEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	if (typeCollidedWith == EXPLOSION)
	{
		explode();
		return;
	}
	
}

void BombEntity::update()
{
	m_fuse--;
	if (m_fuse <= 0)
	{
		explode();
	}
	incAnimFrame();

	// count down to explosion
}

