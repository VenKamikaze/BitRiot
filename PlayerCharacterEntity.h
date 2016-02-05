// class to represent the player's character ingame

#ifndef _PLAYERCHARACTER
#define _PLAYERCHARACTER

#include "GameEntity.h"

#include "AIGoal.h"
#include "AIInstruction.h"
#include "AIGameView.h"

#include "Map.h"

using namespace std;
#include <list>
#include <set>
#include <map>

typedef pair<int, int> coord;

class PlayerCharacterEntity : public GameEntity
{
public:
	PlayerCharacterEntity(int uniqueID, int teamNumber, int atX, int atY, unsigned int flags);
	virtual ~PlayerCharacterEntity();

	virtual void onCollision(EntityType typeCollidedWith, 
							 EntityGroupType groupTypeCollidedWith,
							 int teamCollidedWith); //pure virtual function
	virtual void update(); // called each frame for AI, animation, movement etc.

	virtual void incAnimFrame();

	bool isControlledByAI() { return m_controlledByAI; }

	bool createEntity(EntityType type);


protected:
	int m_animCounter;
	int m_animDelay;

	int m_velocityModifier;

	bool m_controlledByAI;

	int m_IQ; // used for egg intelligence chance

	void incHealth(int amount);
	void incIQ(int amount) { m_IQ += amount; }

	void die();

	// Bot-AI-only data
	list<AIGoal *> m_goalList;
	list<AIInstruction *> m_insList;

	// Bot-AI-only functions
	void determineGoals();
	void determineInstructionsFromGoal();
	void processNextInstruction();

	bool staticEntityAtLocation(int tileX, int tileY);
	bool goToNearestEmptyTile(int fromX, int fromY); // returns false on failure
	bool goNumTilesAway(int numToMove);
	bool nearTileCenter(); // checks if entity is near center of current tile
	coord getNextTileFromPathfinding(int targetTileX, int targetTileY,
		 bool allowBlockBombing = false);
	bool safeLocationToBomb(int tileX, int tileY); // returns false if a bomb here would be stupid
												   // i.e. blowing up our own eggs
	void checkEnvironment(); // checks surrounding environment for anything we should react to
	int m_checkTimer; // timer variable for the above function
	void clearGoalsAndInstructions(); // wipes m_insList and m_goalList
	

};

#endif