
#include "EggThiefEntity.h"

EggThiefEntity::EggThiefEntity(int uniqueID, int playerNumber, int atX, int atY)
  :SimpleMovingEntity(uniqueID, playerNumber, atX, atY)
{
  // constructor
  m_type = EGG_THIEF;
  m_groupType = PLAYER_SPAWN_GROUP;

  m_health = DataReader::getInstance()->getIntFromFile("EGG_THIEF_INITIAL_HEALTH", "data/entity.txt");

  GameSound::playSound(GameSound::THIEF_CREATE);
}

EggThiefEntity::~EggThiefEntity()
{}

void EggThiefEntity::onCollision(EntityType typeCollidedWith,
                                 EntityGroupType groupTypeCollidedWith,
                                 int collidedWithPlayerNumber)
{
  SimpleMovingEntity::onCollision(typeCollidedWith, groupTypeCollidedWith, collidedWithPlayerNumber);
}

void EggThiefEntity::update()
{
  SimpleMovingEntity::update();
}
