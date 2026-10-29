#include "EnPlusEntity.h"

EnPlusEntity::EnPlusEntity(int uniqueID, int playerNumber, int atX, int atY)
{
  //GameEntity(); // apparently this is called for us automatically M2S check this
  // constructor
  m_ID = uniqueID;
  m_playerNumber = playerNumber;

  m_type = ENPLUS_POWERUP;
  m_groupType = POWERUP_GROUP;

  m_staticFlag = true;

  m_tileX = atX;
  m_tileY = atY;
  m_offsetX = 0;
  m_offsetY = 0;
  m_health = DataReader::getInstance()->getIntFromFile("POWERUP_INITIAL_HEALTH", "data/entity.txt");
  m_velocity = 0;
  m_currentAnimState = EntityAnim::IDLE;
  m_currentAnimFrame = 0;

  GameSound::playSound(GameSound::ENERGY_DROP);

  m_initialDecayCount = DataReader::getInstance()->getIntFromFile("POWERUP_DECAY_COUNTER", "data/entity.txt");
  m_decayCounter = m_initialDecayCount;

}


void EnPlusEntity::onCollision(EntityType typeCollidedWith,
                               EntityGroupType groupTypeCollidedWith,
                               int collidedWithPlayerNumber)
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

void EnPlusEntity::update()
{
  incAnimFrame();

  m_decayCounter--;
  if (m_decayCounter <= 0)
  {
    m_decayCounter = m_initialDecayCount;
    m_health--;
  }

  if (m_health <= 0)
  {
    EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
  }
}

void EnPlusEntity::hashExtraState(StateHasher& h) const
{
  h.i32(m_decayCounter, "enplus.decayCounter");
}
