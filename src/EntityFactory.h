// EntityFactory.h
// Singleton factory creates different Entity classes 
// based on supplied entity_type
#ifndef _ENTITYFACTORYCLASS
#define _ENTITYFACTORYCLASS

#include <assert.h>

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

	GameEntity * getEntity(EntityType t, int team, int atX, int atY, 
		unsigned int flags, float offsetX = 0, float offsetY = 0);

private:
	int m_idCount; // used to assign a unique ID to each entity

	EntityFactory();
	~EntityFactory();

};


#endif