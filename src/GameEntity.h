// GameEntity.h

// This is the base class from which all interactive game objects inherit

#ifndef _GAMEENTITYCLASS
#define _GAMEENTITYCLASS


#include "EntityAnim.h"
#include "EntityEnum.h"
#include "EntityMessageQueue.h"
#include <assert.h>
#include "GameSound.h"
#include "Fixed.h"
#include "Map.h"
#include "StateHash.h"

class GameEntity
{
  public:
    GameEntity();
    virtual ~GameEntity() {}

    // entity creation flags
    static const unsigned int UP_FLAG   = (1 << 1);
    static const unsigned int DOWN_FLAG   = (1 << 2);
    static const unsigned int LEFT_FLAG   = (1 << 3);
    static const unsigned int RIGHT_FLAG  = (1 << 4);

    // for robot types
    static const unsigned int WALK_THROUGH_BLOCKS = (1 << 5);
    static const unsigned int LAY_BLOCKS  = (1 << 6);
    static const unsigned int LAY_MINES   = (1 << 7);
    static const unsigned int FIRE_ROCKETS  = (1 << 8);
    static const unsigned int LAY_BOMBS   = (1 << 9);

    // for Player Character Bot AI
    static const unsigned int AI_CONTROLLED_BOT_FLAG = (1 << 10);

    // obvious accessor functions
    inline const int getTileX()
    {
      return m_tileX;
    }
    inline const int getTileY()
    {
      return m_tileY;
    }
    inline const fixed_t getOffsetX()
    {
      return m_offsetX;
    }
    inline const fixed_t getOffsetY()
    {
      return m_offsetY;
    }
    inline const fixed_t getVelocity()
    {
      return m_velocity;
    }
    inline const int getHealth()
    {
      return m_health;
    }
    inline const int getID()
    {
      return m_ID;
    }
    inline const int getPlayerNumber()
    {
      return m_playerNumber;
    }
    // Alliance team number (1-based; 0 = CPU/neutral), derived from this
    // entity's playerNumber via the menu's team assignment. Entities sharing a
    // team cooperate: rockets/spawns don't target or damage teammates.
    int getTeamNumber() const;
    // True if an entity belonging to player slot otherPlayerNumber is on this
    // entity's alliance team (so it must not be targeted/damaged). Used by all
    // targeting/collision checks in place of the old "same slot" comparison;
    // takes a playerNumber (as carried by EntityInfo / passed to onCollision)
    // and maps it to a team internally.
    bool isTeammatePlayer(int otherPlayerNumber) const;
    inline const bool getWalkThroughBlocks()
    {
      return m_walkThroughBlocks;
    }
    inline const bool getStaticFlag()
    {
      return m_staticFlag;
    }
    inline const bool isSolid()
    {
      return m_solidFlag;
    }
    inline const EntityType getType()
    {
      return m_type;
    }
    inline const EntityGroupType getGroupType()
    {
      return m_groupType;
    }
    // Which sprite-sheet row (EntityAnim::ANIM_STATE) and column the entity is
    // showing. The renderer reads these; only the simulation changes them.
    inline int animState() const
    {
      return m_currentAnimState;
    }
    inline int animFrame() const
    {
      return m_currentAnimFrame;
    }

    // Where the sprite's top-left corner is drawn on the play field, in pixels.
    // An offset of -1..+1 is half a tile either side of the tile's origin; the
    // fraction is truncated toward zero, as the old (int)(offset * n) cast did.
    // Rendering reads the sim through these and never writes back.
    inline int pixelX() const
    {
      return (m_tileX * Map::TILE_WIDTH) + fixedToPixels(m_offsetX, Map::TILE_WIDTH / 2);
    }
    inline int pixelY() const
    {
      return (m_tileY * Map::TILE_WIDTH) + fixedToPixels(m_offsetY, Map::TILE_WIDTH / 2);
    }

    inline void setTileX(int x)
    {
      m_tileX = x;
    }
    inline void setTileY(int y)
    {
      m_tileY = y;
    }
    inline void setOffsetX(fixed_t x)
    {
      m_offsetX = x;
    }
    inline void setOffsetY(fixed_t y)
    {
      m_offsetY = y;
    }

    // virtual functions to override
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber) = 0; //pure virtual function
    virtual void update() = 0; // called each frame for AI, animation, movement etc.

    // virtual functions that don't always need overriding
    virtual void moveInDirection(DIRECTION d); // returns false if can't move completely in direction

    // Advances the render-bookkeeping animation frame, wrapping at the number of
    // frames the type's sprite sheet has (EntityAnim::frameCount, a table: the
    // simulation needs no bitmaps). Drawing an entity is WorldRenderer's job.
    virtual void incAnimFrame();

    // Feeds this entity's simulation state into the state hash (see the "State
    // hash specification" in Simulation.h): the fields below that every entity
    // has, then whatever hashExtraState() adds for its own type. Never the
    // render-only members (m_currentAnimFrame, the anim counters) and never the
    // absolute id.
    void hashState(StateHasher& h) const;

  protected:
    // Each type's own simulation state (fuses, hatch and decay counters, AI
    // timers, ...), in a fixed order. A subclass that adds state to update()'s
    // behaviour must add it here too, calling its base class's first.
    virtual void hashExtraState(StateHasher& h) const;

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
    fixed_t m_offsetX, m_offsetY; // offset from tile cordinate with value -1 < offset < 1 (Q16.16, see Fixed.h)
    int m_health; // init to a starting value, lose health when hit or even when move
    fixed_t m_velocity; // offset units per tick (Q16.16; a tile is 2.0 wide); should be speed really, but i prefer this,
    // also should be a function of health for most entities
    int m_playerNumber; // 0 is monsters, 1 for player1, 2 for player2 etc...


    void explode(); // used by some children to explode :)


};



#endif
