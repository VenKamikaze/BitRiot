#include "DroneEntity.h"

DroneEntity::DroneEntity(int uniqueID, int playerNumber, int atX, int atY)
  :SimpleMovingEntity(uniqueID, playerNumber, atX, atY)
{
  // constructor
  m_type = DRONE;
  m_groupType = PLAYER_SPAWN_GROUP;

  m_initialAnimCount = DataReader::getInstance()->getIntFromFile("DRONE_ANIM_COUNT", "data/entity.txt");
  m_animCounter = m_initialAnimCount;

  m_health = DataReader::getInstance()->getIntFromFile("DRONE_INITIAL_HEALTH", "data/entity.txt");
  m_velocityModifier = DataReader::getInstance()->getIntFromFile("DRONE_VELOCITY_MODIFIER", "data/entity.txt");
  m_velocity = fixedFromRatio(m_health + 2, m_velocityModifier);

  GameSound::playSound(GameSound::DRONE_CREATE);
}

DroneEntity::~DroneEntity() {}


void DroneEntity::onCollision(EntityType typeCollidedWith,
                              EntityGroupType groupTypeCollidedWith,
                              int collidedWithPlayerNumber)
{
  SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, collidedWithPlayerNumber);
}

void DroneEntity::update()
{
  SimpleMovingEntity::update();
}

