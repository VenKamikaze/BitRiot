// EntityFactory.h
// Singleton factory creates different Entity classes
// based on supplied entity_type
#ifndef _ENTITYFACTORYCLASS
#define _ENTITYFACTORYCLASS

#include <assert.h>
#include <memory>

// include all entity classes
#include "BlockEntity.h"
#include "PlayerCharacterEntity.h"

#include "EggEntity.h"

#include "EggThiefEntity.h"
#include "WallEaterEntity.h"
#include "DroneEntity.h"

#include "RobotEntity.h"

#include "BombEntity.h"
#include "ExplosionEntity.h"
#include "MineEntity.h"
#include "RocketSpinEntity.h"
#include "RocketFiredEntity.h"
#include "EnergyDropEntity.h"

#include "EnPlusEntity.h"
#include "IQPowerupEntity.h"

#include "EffectEntity.h"

class EntityFactory
{
  public:
    static EntityFactory * getInstance();

    // Restart entity ids from 1. Called when a new round's EntityManager is built,
    // so every round numbers its entities identically (ids are only compared for
    // equality, and std::map iteration follows id order, so this changes nothing
    // about behaviour - it just makes rounds comparable).
    void resetIds();

    shared_ptr<GameEntity> getEntity(EntityType t, int playerNumber, int atX, int atY,
                           unsigned int flags, fixed_t offsetX = 0, fixed_t offsetY = 0,
                           int targetX = 0, int targetY = 0);

  private:
    int m_idCount; // used to assign a unique ID to each entity

    EntityFactory();
    ~EntityFactory();

};


#endif