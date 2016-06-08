 
#include "MineEntity.h"

MineEntity::MineEntity(int uniqueID, int teamNumber, int atX, int atY)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = MINE;
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

	m_fuse = DataReader::getInstance()->getIntFromFile("MINE_FUSE", "data/entity.txt");
	m_animDelay = m_fuse/10;

	// play creation sound
	////M2S SOUND DSound::getInstance()->playSound(DSound::MINE_DROP); // M2S SOUND
	GameSound::playSound(GameSound::MINE_DROP);
}

MineEntity::~MineEntity()
{
	// destructor
}

void MineEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	if (typeCollidedWith == EXPLOSION)
	{
		explode();
		return;
	}

	if (teamCollidedWith != m_team)
	{
		explode();
		return;
	}

}

void MineEntity::update()
{
	m_animDelay--;
	if (m_animDelay <= 0)
	{
		incAnimFrame();
		if (m_fuse > 10)
			m_animDelay = m_fuse/10;
		else
			m_animDelay = 1;
	}

	if (m_team != 0) // cpu team doesn't decay
	{
		m_fuse--;
		if (m_fuse <= 0)
		{
			explode();
		}
	}
}
