
#include "EggEntity.h"

EggEntity::EggEntity(int uniqueID, int teamNumber, EntityType eggType, 
					 EntityType hatchType, int atX, int atY, unsigned int flags)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = eggType;
	m_hatchling = hatchType;
	m_hatchFlags = flags;

	switch (m_hatchling)
	{
	case ENERGY_DROP:
		m_hatchCounter = DataReader::getInstance()->getIntFromFile("ENERGY_DROP_HATCH_TIME", "data/entity.txt");
		break;
	case EGG_THIEF:
		m_hatchCounter = DataReader::getInstance()->getIntFromFile("EGG_THIEF_HATCH_TIME", "data/entity.txt");
		break;
	case WALL_EATER:
		m_hatchCounter = DataReader::getInstance()->getIntFromFile("WALL_EATER_HATCH_TIME", "data/entity.txt");
		break;
	case DRONE:
		m_hatchCounter = DataReader::getInstance()->getIntFromFile("DRONE_HATCH_TIME", "data/entity.txt");
		break;
	case ROBOT1:
		m_hatchCounter = DataReader::getInstance()->getIntFromFile("ROBOT_HATCH_TIME", "data/entity.txt");
		break;
	default:
		assert(false && "Unexpected hatchType passed to EggEntity constructor!");
		break;
	}

	m_animCounter = (m_hatchCounter + 10)/10;

	m_groupType = EGG_GROUP;
	m_solidFlag = false;
	m_staticFlag = true;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = 1;
	m_velocity = 0;
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

	// play creation sound
	// //M2S SOUND DSound::getInstance()->playSound(DSound::EGG_DROP); // M2S SOUND
	//MickBaseSound::getInstance()->playSound("audio/beepdrop.wav");
	GameSound::playSound(GameSound::EGG_DROP);
}

EggEntity::~EggEntity()
{
	// destructor
}

void EggEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int teamCollidedWith)
{
	if (teamCollidedWith == m_team)
	{
		// collided with own team
		// do nothing for most entity types
		switch (groupTypeCollidedWith)
		{
		case EXPLOSION_GROUP:
			explode();
			break;
		default:
			break;
		}
	}
	else
	{
		// collided with an entity from another team
	        
		// switch by group first
		switch (typeCollidedWith)
		{
		case EXPLOSION:
			explode();
			break;
		case EGG_THIEF:
			// oh no an egg thief!
			// forced to change teams
			m_team = teamCollidedWith;
			////M2S SOUND DSound::getInstance()->playSound(DSound::EGG_STOLEN); // M2S SOUND
			GameSound::playSound(GameSound::EGG_STOLEN);
			break;
		default:
			break;
		} // end switch by group
	} // end if

}

void EggEntity::update()
{
	m_animCounter--;
	if (m_animCounter <= 0)
	{
		incAnimFrame();
		m_animCounter = (m_hatchCounter + 10)/10;
	}

	m_hatchCounter--;
	if (m_hatchCounter <= 0)
	{
		hatch();
	}

	return;
}

void EggEntity::hatch()
{
	EntityMessageQueue * emq = EntityMessageQueue::getInstance();
	emq->postCreateMessage(m_hatchling, m_team, m_tileX, m_tileY, m_hatchFlags);
	emq->postDestroyMessage(m_ID);
}
