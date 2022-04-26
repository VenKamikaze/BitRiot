// implementation of EntityFactory.h

#include "EntityFactory.h"

shared_ptr<GameEntity> EntityFactory::getEntity(EntityType t, int team, int atX, int atY,
                                      unsigned int flags, float offsetX, float offsetY)
{
  // first get new ID
  int id = m_idCount;
  m_idCount++;

  // set up new pointer
  shared_ptr<GameEntity> entity;

  switch (t)
  {
    case BLOCK:
      // remember to expand out constructor argument stuff
      entity = std::make_shared<BlockEntity>(id, team, atX, atY);
      break;
    case EGG1:
      entity = std::make_shared<EggEntity>(id, team, EGG1, ENERGY_DROP, atX, atY, 0);
      break;
    case EGG2:
      entity = std::make_shared<EggEntity>(id, team, EGG2, EGG_THIEF, atX, atY, 0);
      break;
    case EGG3:
      entity = std::make_shared<EggEntity>(id, team, EGG3, WALL_EATER, atX, atY, 0);
      break;
    case EGG4:
      entity = std::make_shared<EggEntity>(id, team, EGG4, DRONE, atX, atY, 0);
      break;
    case EGG5:
      entity = std::make_shared<EggEntity>(id, team, EGG5, ROBOT1, atX, atY, flags);
      break;
    case ENPLUS_POWERUP:
      entity = std::make_shared<EnPlusEntity>(id, team, atX, atY);
      break;
    case IQPLUS_POWERUP:
      entity = std::make_shared<IQPowerupEntity>(id, team, atX, atY);
      break;
    case BOMB:
      entity = std::make_shared<BombEntity>(id, team, atX, atY);
      break;
    case MINE:
      entity = std::make_shared<MineEntity>(id, team, atX, atY);
      break;
    case ROCKET_SPIN:
      entity = std::make_shared<RocketSpinEntity>(id, team, atX, atY);
      break;
    case ROCKET_FIRED:
      entity = std::make_shared<RocketFiredEntity>(id, team, atX, atY, flags, offsetX, offsetY);
      break;
    case EXPLOSION:
      entity = std::make_shared<ExplosionEntity>(id, atX, atY);
      break;
    case PLAYER_CHARACTER:
      entity = std::make_shared<PlayerCharacterEntity>(id, team, atX, atY, flags);
      break;
    case ENERGY_DROP:
      entity = std::make_shared<EnergyDropEntity>(id, team, atX, atY);
      break;
    case EGG_THIEF:
      entity = std::make_shared<EggThiefEntity>(id, team, atX, atY);
      break;
    case WALL_EATER:
      entity = std::make_shared<WallEaterEntity>(id, team, atX, atY);
      break;
    case DRONE:
      entity = std::make_shared<DroneEntity>(id, team, atX, atY);
      break;
    case ROBOT1:
      entity = std::make_shared<RobotEntity>(id, team, atX, atY, flags);
      break;
    case MUNCH_EFFECT:
      entity = std::make_shared<EffectEntity>(id, MUNCH_EFFECT, atX, atY);
      break;
    case DAMAGE_EFFECT:
      entity = std::make_shared<EffectEntity>(id, DAMAGE_EFFECT, atX, atY, offsetX, offsetY);
      break;
    default:
      break;
  }
  assert(entity != 0 && "Unexpected Entity type requested from factory");
  return entity;
}

EntityFactory::EntityFactory()
{
  m_idCount = 1;
}

EntityFactory::~EntityFactory()
{

}

EntityFactory * EntityFactory::getInstance()
{
  static EntityFactory instance;

  return &instance;
}