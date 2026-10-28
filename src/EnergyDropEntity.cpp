#include "EnergyDropEntity.h"

EnergyDropEntity::EnergyDropEntity(int uniqueID, int playerNumber, int atX, int atY)
{
  // constructor
  m_ID = uniqueID;
  m_playerNumber = playerNumber;

  m_type = ENERGY_DROP;
  m_groupType = WEAPON_GROUP;

  m_staticFlag = true;

  m_tileX = atX;
  m_tileY = atY;
  m_offsetX = 0;
  m_offsetY = 0;
  m_health = 1;
  m_velocity = 0;
  m_currentAnimState = EntityAnim::IDLE;
  m_currentAnimFrame = 0;

  GameSound::playSound(GameSound::ENERGY_DROP);

}

EnergyDropEntity::~EnergyDropEntity()
{

}

void EnergyDropEntity::onCollision(EntityType typeCollidedWith,
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

void EnergyDropEntity::update()
{
  incAnimFrame();
}
