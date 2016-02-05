
#include "SimpleBrain.h"

SimpleBrain::SimpleBrain(int &rTileXref, int &rTileYref, 
						 float &rOffsetXref, float &rOffsetYref,
						 int &animStateRef, bool & rWalkThroughBlocksRef,
						 float & rVelocityRef)
: 
rTileX(rTileXref),
rTileY(rTileYref),
rOffsetX(rOffsetXref),
rOffsetY(rOffsetYref),
rAnimState(animStateRef),
rWalkThroughBlocks(rWalkThroughBlocksRef),
rVelocity(rVelocityRef),
m_lastMovingState((rand()%4) + 1),
m_minMoveTimer(INITIAL_MIN_MOVE_TIMER)
{}

SimpleBrain::~SimpleBrain()
{

}

void SimpleBrain::setDirectionToMove()
{
	Map * staticMap = Map::getInstance();

	if (rAnimState == IDLE)
	{
		// pick random direction to move in
		bool directions[4] = { true, true, true, true }; // corresponds to which directions are valid
		
		Map::StaticTile directionTile[4]; // holds static tile info i.e. WALL, EMPTY or BLOCK
		directionTile[UP] = staticMap->staticTileAt(rTileX, rTileY - 1);
		directionTile[DOWN] = staticMap->staticTileAt(rTileX, rTileY + 1);
		directionTile[LEFT] = staticMap->staticTileAt(rTileX - 1, rTileY);
		directionTile[RIGHT] = staticMap->staticTileAt(rTileX + 1, rTileY);

		bool allDirectionsAreBad = true; // flag to see if it should stay still for now

        for (int i = 0; i < 4; i++)
		{
			if (directionTile[i] == Map::WALL || // if it's a wall
				(directionTile[i] == Map::BLOCK && !rWalkThroughBlocks)) // or a block and I can't move through blocks
			{
				directions[i] = false;
			} else {
				allDirectionsAreBad = false;
			}
		}

		// if all directions are false, stay on idle
		if (allDirectionsAreBad)
		{
			rAnimState = IDLE;
		} else {
            // else pick random direction
			bool directionFound = false;

			while (directionFound == false)
			{
				// weights to adjust directions
				Heading heading;
				
				int random = rand()%100;
				if (random < 100)
					heading = TURN_LEFT; // 0.3p
				if (random < 70)
					heading = TURN_RIGHT; // 0.3p
				if (random < 40)
					heading = FORWARD; // 0.3p
				if (random < 10)
					heading = GO_BACK; // 0.1p

				DIRECTION randDirection = getDirectionFromHeading(heading, m_lastMovingState);

				if (directions[randDirection] == true)
				{
					// ok to go this way
					directionFound = true;
					rAnimState = randDirection + 1;
					m_minMoveTimer = INITIAL_MIN_MOVE_TIMER;
				}
			}

		} // end if all directions not bad

		// end if animState == IDLE
	} else {
		// state is not idle
		if (m_minMoveTimer > 0)
		{
			// have to move in current direction
			m_minMoveTimer--;
		} else {
			// check if in middle of tile
			if (nearTileCenter() == false)
			{
				// keep going
				
			} else {
				// look for turns left or right
				if (turnAvailable())
				{
					// turns available
					// go to be being idle, then call recursively
					rAnimState = IDLE;
					setDirectionToMove();
					return;

				} else {
					// turns not available

					// check for dead end
					switch (static_cast<DIRECTION>(rAnimState - 1))
					{
					case UP:
						if (staticMap->staticTileAt(rTileX, rTileY - 1) != Map::EMPTY
							|| (staticMap->staticTileAt(rTileX, rTileY - 1) == Map::BLOCK && !rWalkThroughBlocks))
						{
							rAnimState = IDLE;
							setDirectionToMove();
							return;
						}
						break;
					case DOWN:
						if (staticMap->staticTileAt(rTileX, rTileY + 1) != Map::EMPTY
							|| (staticMap->staticTileAt(rTileX, rTileY + 1) == Map::BLOCK && !rWalkThroughBlocks))
						{
							rAnimState = IDLE;
							setDirectionToMove();
							return;
						}
						break;
					case LEFT:
						if (staticMap->staticTileAt(rTileX - 1, rTileY) != Map::EMPTY
							|| (staticMap->staticTileAt(rTileX - 1, rTileY) == Map::BLOCK && !rWalkThroughBlocks))
						{
							rAnimState = IDLE;
							setDirectionToMove();
							return;
						}
						break;
					case RIGHT:
						if (staticMap->staticTileAt(rTileX + 1, rTileY) != Map::EMPTY
							|| (staticMap->staticTileAt(rTileX + 1, rTileY) == Map::BLOCK && !rWalkThroughBlocks))
						{
							rAnimState = IDLE;
							setDirectionToMove();
							return;
						}
						break;
					}
				
				}

			} // end if near tile center
		} // end if minMoveTimer expired

	} // end if state != idle

	if (rAnimState != IDLE)
		m_lastMovingState = rAnimState;

}


bool SimpleBrain::nearTileCenter()
{
	float absOffsetX, absOffsetY;
	absOffsetX = (rOffsetX < 0) ? -rOffsetX : rOffsetX;
	absOffsetY = (rOffsetY < 0) ? -rOffsetY : rOffsetY;

	if ((absOffsetX <= rVelocity) && (absOffsetY <= rVelocity))
		return true;
	else
		return false;
}

bool SimpleBrain::turnAvailable()
{
	// should only be called while moving
	if (rAnimState == 0)
		return false;

	DIRECTION dir = static_cast<DIRECTION>(rAnimState - 1);
	Map * staticMap = Map::getInstance();

	switch (dir)
	{
	case UP:
	case DOWN:
		// try left
		if (staticMap->staticTileAt(rTileX - 1, rTileY) == Map::EMPTY
			|| (staticMap->staticTileAt(rTileX - 1, rTileY) == Map::BLOCK && rWalkThroughBlocks))
			return true;
		// try right
		if (staticMap->staticTileAt(rTileX + 1, rTileY) == Map::EMPTY
			|| (staticMap->staticTileAt(rTileX + 1, rTileY) == Map::BLOCK && rWalkThroughBlocks))
			return true;
		// neither is ok
		return false;

	case LEFT:
	case RIGHT:
		// try up
		if (staticMap->staticTileAt(rTileX, rTileY - 1) == Map::EMPTY
			|| (staticMap->staticTileAt(rTileX, rTileY - 1) == Map::BLOCK && rWalkThroughBlocks))
			return true;
		// try down
		if (staticMap->staticTileAt(rTileX, rTileY + 1) == Map::EMPTY
			|| (staticMap->staticTileAt(rTileX, rTileY + 1) == Map::BLOCK && rWalkThroughBlocks))
			return true;
		// neither is ok
		return false;
	default:
		return false;
	}

}

DIRECTION SimpleBrain::getDirectionFromHeading(Heading h, int moveState)
{
	switch (moveState)
	{
	case MOVE_UP:
		switch (h)
		{
		case FORWARD:
			return UP;
		case TURN_LEFT:
			return LEFT;
		case TURN_RIGHT:
			return RIGHT;
		case GO_BACK:
			return DOWN;
		}
		break;
	case MOVE_DOWN:
		switch (h)
		{
		case FORWARD:
			return DOWN;
		case TURN_LEFT:
			return RIGHT;
		case TURN_RIGHT:
			return LEFT;
		case GO_BACK:
			return UP;
		}
		break;
	case MOVE_LEFT:
		switch (h)
		{
		case FORWARD:
			return LEFT;
		case TURN_LEFT:
			return DOWN;
		case TURN_RIGHT:
			return UP;
		case GO_BACK:
			return RIGHT;
		}
		break;
	case MOVE_RIGHT:
		switch (h)
		{
		case FORWARD:
			return RIGHT;
		case TURN_LEFT:
			return UP;
		case TURN_RIGHT:
			return DOWN;
		case GO_BACK:
			return LEFT;
		}
		break;
	}

	return UP; // shouldn't get called

}
