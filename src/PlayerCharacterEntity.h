// class to represent the player's character ingame

#ifndef _PLAYERCHARACTER
#define _PLAYERCHARACTER

#include "GameEntity.h"
#include "MickSDLUtil.h"

#include "AIGoal.h"
#include "AIInstruction.h"
#include "AIGameView.h"

#include "Map.h"
#include "GameSettings.h"

using namespace std;
#include <list>
#include <set>
#include <map>

typedef pair<int, int> coord;

class PlayerCharacterEntity : public GameEntity
{
  public:
    PlayerCharacterEntity(int uniqueID, int playerNumber, int atX, int atY, unsigned int flags);
    virtual ~PlayerCharacterEntity();

    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber); //pure virtual function
    virtual void update(); // called each frame for AI, animation, movement etc.

    virtual void incAnimFrame();

    bool isControlledByAI()
    {
      return m_controlledByAI;
    }

    bool createEntity(EntityType type);

    void incNumberBombsPlaced()
    {
      m_numberBombsPlaced++;
    }
    int getNumberBombsPlaced()
    {
      return m_numberBombsPlaced;
    }
    void incNumberEggsPlaced()
    {
      m_numberEggsPlaced++;
    }
    int getNumberEggsPlaced()
    {
      return m_numberEggsPlaced;
    }
    bool isAlive()
    {
      return alive;
    }
    void setAlive(bool alive)
    {
      this->alive = alive;
    }

    int attachedController=-1;
    bool m_controlledByAI;

  protected:
    int m_animCounter;
    int m_animDelay;
    int m_velocityModifier;
    int m_numberBombsPlaced = 0; // used for scoring
    int m_numberEggsPlaced = 0;  // used for scoring

    int m_IQ; // used for egg intelligence chance

    void incHealth(int amount);
    void incIQ(int amount)
    {
      m_IQ += amount;
    }

    void die();

    // Bot-AI-only data
    list<AIGoal *> m_goalList;
    list<AIInstruction *> m_insList;

    // Bot-AI-only functions
    void determineGoals();
    bool seekPickup(); // queues GOAL_GET_POWERUP for the best reachable pickup, if any
    bool pickupIsHazardous(int tileX, int tileY);
    bool canBombBlocks(); // true if bombing our way through a block is affordable and survivable
    // true if anything that would explode in a chain is within `radius` tiles
    bool explosivesNear(int tileX, int tileY, int radius);
    void determineInstructionsFromGoal();
    void processNextInstruction();

    bool staticEntityAtLocation(int tileX, int tileY);
    bool goToNearestEmptyTile(int fromX, int fromY); // returns false on failure
    bool goNumTilesAway(int numToMove);
    bool nearTileCenter(); // checks if entity is near center of current tile
    // Returns the first tile of an A* route to the target, or the current tile
    // when there is none (or we are already there). With allowBlockBombing the
    // route may go through blocks and the instructions to bomb the first one
    // are queued as a side effect.
    coord getNextTileFromPathfinding(int targetTileX, int targetTileY,
                                     bool allowBlockBombing = false);

    // What it takes to get from our tile to every other tile: one flood fill,
    // so every candidate target can be classified at once instead of running an
    // A* search per target. Indexed [y * mapWidth + x].
    struct RouteInfo
    {
      bool reachable = false;
      int steps = 0;  // tiles walked
      int blocks = 0; // blocks that have to be bombed on the way
    };
    void floodRoutes(bool allowBlockBombing, vector<RouteInfo> & routes);
    bool safeLocationToBomb(int tileX, int tileY); // returns false if a bomb here would be stupid
    // i.e. blowing up our own eggs
    void checkEnvironment(); // checks surrounding environment for anything we should react to
    int m_checkTimer; // timer variable for the above function

    // Pickups we gave up on after repeatedly failing to make progress towards
    // them, so a pickup we cannot actually reach does not trap the AI forever.
    // Maps pickup id to the m_aiFrame we gave up on it; the entry expires after a
    // while, because whatever was in the way (an egg on the tile we need to bomb
    // from, say) is usually temporary.
    map<int, int> m_abandonedPickups;
    struct PickupAttempt
    {
      // Best route to it we have set out on: fewest blocks left to bomb, then
      // fewest tiles to walk.
      int bestBlocks = 1 << 30;
      int bestSteps = 1 << 30;
      int failures = 0; // times in a row we set out without improving on that
    };
    map<int, PickupAttempt> m_pickupAttempts; // keyed by pickup entity id
    int m_aiFrame = 0; // frames this bot's AI has run; the clock for the above

  public:
    // Path search nodes currently allocated, across all bots. Zero between
    // searches; anything else is a leak. For tests.
    static long livePathNodes();

  protected:
    void clearGoalsAndInstructions(); // wipes m_insList and m_goalList

  private:
    bool alive;
};

#endif
