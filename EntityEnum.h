// Simple header file just for the Entity type enumerations

#ifndef _ENTITYENUM
#define _ENTITYENUM

#ifndef NULL
#define NULL 0
#endif

// definitions for entity types and groups
static const int NUM_ENTITY_GROUPS = 10;
enum EntityGroupType
{
	BLOCK_GROUP,
	EGG_GROUP,
	POWERUP_GROUP,
	WEAPON_GROUP,
	EXPLOSION_GROUP,
	CHARACTER_GROUP,
	PLAYER_SPAWN_GROUP,
	MONSTER_GROUP,
	ROBOT_GROUP,
	EFFECT_GROUP
};
static const int NUM_ENTITY_TYPES = 21;
enum EntityType
{
	BLOCK, // BLOCK_GROUP
	EGG1, EGG2, EGG3, EGG4, EGG5, // EGG_GROUP
	ENPLUS_POWERUP, IQPLUS_POWERUP, // POWERUP_GROUP
	BOMB, MINE, ROCKET_SPIN, ROCKET_FIRED, ENERGY_DROP, // WEAPON_GROUP
	EXPLOSION, // EXPLOSION_GROUP
	PLAYER_CHARACTER, // CHARACTER_GROUP
	EGG_THIEF, WALL_EATER, DRONE, // PLAYER_SPAWN_GROUP
		// MONSTER GROUP currently not implemented
    ROBOT1, // ROBOT_GROUP
	MUNCH_EFFECT, DAMAGE_EFFECT // EFFECT GROUP
};

#endif
