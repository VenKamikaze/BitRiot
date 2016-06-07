// GameEntity.h

// This is the base class from which all interactive game objects inherit

#ifndef _GAMEENTITYCLASS
#define _GAMEENTITYCLASS


#include "EntityRendererFactory.h"
#include "EntityMessageQueue.h"
#include <assert.h>
#include "GameSound.h"

class GameEntity
{
public:
	GameEntity();
	virtual ~GameEntity() {}

	// entity creation flags
	static const unsigned int UP_FLAG		= (1 << 1);
	static const unsigned int DOWN_FLAG		= (1 << 2);
	static const unsigned int LEFT_FLAG		= (1 << 3);
	static const unsigned int RIGHT_FLAG	= (1 << 4);

	// for robot types
	static const unsigned int WALK_THROUGH_BLOCKS	= (1 << 5);
	static const unsigned int LAY_BLOCKS	= (1 << 6);
	static const unsigned int LAY_MINES		= (1 << 7);
	static const unsigned int FIRE_ROCKETS	= (1 << 8);
	static const unsigned int LAY_BOMBS		= (1 << 9);

	// for Player Character Bot AI
	static const unsigned int AI_CONTROLLED_BOT_FLAG = (1 << 10);

	// obvious accessor functions
	inline const int getTileX() { return m_tileX; }
	inline const int getTileY() { return m_tileY; }
	inline const float getOffsetX() { return m_offsetX; }
	inline const float getOffsetY() { return m_offsetY; }
	inline const float getVelocity() { return m_velocity; }
	inline const int getHealth() { return m_health; }
	inline const int getID() { return m_ID; }
	inline const int getTeam() { return m_team; }
	inline const bool getWalkThroughBlocks() { return m_walkThroughBlocks; }
	inline const bool getStaticFlag() { return m_staticFlag; }	
	inline const bool isSolid() { return m_solidFlag; }
	inline const EntityType getType() { return m_type; }
	inline const EntityGroupType getGroupType() { return m_groupType; }

	inline void setTileX(int x) { m_tileX = x; }
	inline void setTileY(int y) { m_tileY = y; }
	inline void setOffsetX(float x) { m_offsetX = x; }
	inline void setOffsetY(float y) { m_offsetY = y; }

	// virtual functions to override
	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith,
							 int teamCollidedWith) = 0; //pure virtual function
	virtual void update() = 0; // called each frame for AI, animation, movement etc.
	
	// virtual functions that don't always need overriding
	virtual void moveInDirection(DIRECTION d); // returns false if can't move completely in direction
	
	EntityRenderer * getEntityRenderer(); // gets animation surface pointer from factory
	virtual void incAnimFrame();
	bool render(SDL_Surface* dest); // renders entity to destination surface
	
protected:
													
	int m_currentAnimState, m_currentAnimFrame; // used to reference specific tile for rendering

	// const variables for collision detection
	EntityType m_type;
	EntityGroupType m_groupType; 	
	bool m_staticFlag; // static entities never move, can only have one per tile
	bool m_solidFlag; // solid entitites are things like blocks and wall pieces	
	bool m_walkThroughBlocks; // can walk through blocks and other solid entities

	// object data
	int m_ID; // each entity has a unique id
	int m_tileX, m_tileY; // which absolute tile the entity is closest to
	float m_offsetX, m_offsetY; // offset from tile cordinate with value -1 < offset < 1
	int m_health; // init to a starting value, lose health when hit or even when move
	float m_velocity; // should be speed really, but i prefer this, 
					// also should be a function of health for most entities
	int m_team; // 0 is monsters, 1 for player1, 2 for player2 etc...

	
	void explode(); // used by some children to explode :)
	

};

	

#endif
