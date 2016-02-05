// implementation of EntityFactory.h

#include "EntityFactory.h"

GameEntity * EntityFactory::getEntity(EntityType t, int team, int atX, int atY, 
									  unsigned int flags, float offsetX, float offsetY)
{
	// first get new ID
	int id = m_idCount;
	m_idCount++;

	// set up new pointer
	GameEntity * entity = 0;

	switch (t)
	{
	case BLOCK:
		// remember to expand out constructor argument stuff
		entity = new BlockEntity(id, team, atX, atY);
		break;
	case EGG1:
		entity = new EggEntity(id, team, EGG1, ENERGY_DROP, atX, atY, 0);
		break;
	case EGG2:
		entity = new EggEntity(id, team, EGG2, EGG_THIEF, atX, atY, 0);
		break;
	case EGG3:
		entity = new EggEntity(id, team, EGG3, WALL_EATER, atX, atY, 0);
		break;
	case EGG4:
		entity = new EggEntity(id, team, EGG4, DRONE, atX, atY, 0);
		break;
	case EGG5:
		entity = new EggEntity(id, team, EGG5, ROBOT1, atX, atY, flags);
		break;
	case ENPLUS_POWERUP:
		entity = new EnPlusEntity(id, team, atX, atY);
		break;
	case IQPLUS_POWERUP:
		entity = new IQPowerupEntity(id, team, atX, atY);
		break;
	case BOMB:
		entity = new BombEntity(id, team, atX, atY);
		break;
	case MINE:
		entity = new MineEntity(id, team, atX, atY);
		break;
	case ROCKET_SPIN:
		entity = new RocketSpinEntity(id, team, atX, atY);
		break;
	case ROCKET_FIRED:
		entity = new RocketFiredEntity(id, team, atX, atY, flags, offsetX, offsetY);
		break;
	case EXPLOSION:
		entity = new ExplosionEntity(id, atX, atY);
		break;
	case PLAYER_CHARACTER:
		entity = new PlayerCharacterEntity(id, team, atX, atY, flags);
		break;
	case ENERGY_DROP:
		entity = new EnergyDropEntity(id, team, atX, atY);
		break;
	case EGG_THIEF:
		entity = new EggThiefEntity(id, team, atX, atY);
		break;
	case WALL_EATER:
		entity = new WallEaterEntity(id, team, atX, atY);
		break;
	case DRONE:
		entity = new DroneEntity(id, team, atX, atY);
		break;
	case ROBOT1:
		entity = new RobotEntity(id, team, atX, atY, flags);
		break;
	case MUNCH_EFFECT:
		entity = new EffectEntity(id, MUNCH_EFFECT, atX, atY);
		break;
	case DAMAGE_EFFECT:
		entity = new EffectEntity(id, DAMAGE_EFFECT, atX, atY, offsetX, offsetY);
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