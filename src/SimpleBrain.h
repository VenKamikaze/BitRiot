// A "brain" object is contained in GameEntities
// that need some AI for movement and such

// A SimpleBrain is used for entities that move around
// in a fairly random way

#ifndef _SIMPLEBRAINCLASS
#define _SIMPLEBRAINCLASS

#include "Map.h"
#include "Fixed.h"
#include "StateHash.h"

class SimpleBrain
{
  public:
    SimpleBrain(int & tileXref, int & tileYref,
                fixed_t & offsetXref, fixed_t & offsetYref,
                int & animStateRef, bool & walkThroughBlocksRef,
                fixed_t & velocityRef);
    ~SimpleBrain();

    enum ANIM_STATE
    { IDLE = 0, MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT };
    enum Heading { FORWARD, TURN_LEFT, TURN_RIGHT, GO_BACK };

    void setDirectionToMove(); // changes the entity's currentAnimState
    // so move depending on this

    // The brain's own timers (the rest of its state is the owning entity's
    // tile, offset, velocity and anim state, hashed with the entity).
    void hashState(StateHasher& h) const;

  protected:
    // data references
    const int & rTileX;
    const int & rTileY;
    const fixed_t & rOffsetX;
    const fixed_t & rOffsetY;
    int & rAnimState;
    const bool & rWalkThroughBlocks;
    const fixed_t & rVelocity;

    // private functions
    bool nearTileCenter();
    bool turnAvailable(); // checks if possible to turn left or right from current position

    int m_lastMovingState; // used in below function
    DIRECTION getDirectionFromHeading(Heading h, int moveState); // used to determine direction to go
    // in conjunction with direction probability weightings

    int m_minMoveTimer; // used to force movement so entity doesnt try to go different directions at a junction each frame
    static const int INITIAL_MIN_MOVE_TIMER = 5; // frames to head in forced direction

};

#endif