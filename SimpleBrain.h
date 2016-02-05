// A "brain" object is contained in GameEntities
// that need some AI for movement and such

// A SimpleBrain is used for entities that move around
// in a fairly random way

#ifndef _SIMPLEBRAINCLASS
#define _SIMPLEBRAINCLASS

#include "Map.h"

class SimpleBrain
{
public:
	SimpleBrain(int & tileXref, int & tileYref, 
				float & offsetXref, float & offsetYref,
				int & animStateRef, bool & walkThroughBlocksRef,
				float & velocityRef);
	~SimpleBrain();
	
	enum ANIM_STATE 
	{ IDLE = 0, MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT };
	enum Heading { FORWARD, TURN_LEFT, TURN_RIGHT, GO_BACK };

	void setDirectionToMove(); // changes the entity's currentAnimState
							   // so move depending on this

protected:
	// data references
	const int & rTileX;
	const int & rTileY;
	const float & rOffsetX;
	const float & rOffsetY;
	int & rAnimState;
	const bool & rWalkThroughBlocks;
	const float & rVelocity;

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