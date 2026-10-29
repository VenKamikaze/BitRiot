// class to represent the player's character ingame

#ifndef _PLAYERCHARACTER
#define _PLAYERCHARACTER

#include "GameEntity.h"

#include "AIGoal.h"
#include "AIInstruction.h"
#include "AIGameView.h"

#include "Map.h"
#include "GameSettings.h"

using namespace std;
#include <list>
#include <string>
#include <utility>
#include <vector>
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

  protected:
    virtual void hashExtraState(StateHasher& h) const;

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
    // true if a bomb here would set off an egg, mine or bomb next to it, whose
    // blast would hit this tile a second time
    bool explosivesAdjacent(int tileX, int tileY);
    // true if a bomb laid here to clear a block would be allowed
    bool bombSiteAcceptable(int tileX, int tileY);
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
      int bombFromX = -1; // tile the first of them is bombed from (-1 if none)
      int bombFromY = -1;
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
    // A* searches run so far, across all bots. For tests.
    static long pathSearchesRun();
    // The AI's tuning constants that are not in a data file (named constants in
    // PlayerCharacterEntity.cpp), as ("PlayerCharacterEntity.NAME", value) pairs,
    // for the data hash (DataHash.cpp).
    static void appendSimConstants(std::vector<std::pair<std::string, int> >& out);

  protected:
    void clearGoalsAndInstructions(); // wipes m_insList and m_goalList

  private:
    // The Simulation applies control events (TickInputs.h) and reads the hit flag;
    // nothing else changes who controls a player or sees the flag.
    friend class Simulation;

    // Brief 04b: whether a bot or a human controls this player. Written by the
    // constructor and by applyControl(), which only Simulation::step() calls, when it
    // applies a ControlEvent. (Which device drives a human is not here: it is the
    // input layer's, and does not exist in bitriot_sim.)
    bool m_controlledByAI = false;

    // Hands the player to a human (toAI false) or back to the AI (toAI true) and
    // clears the AI's plans: its goal and instruction lists go on both transitions, so
    // a bot never resumes a plan made before it was a human, and a taken-over bot does
    // not keep a stale one. Does nothing, and returns false, if it would not change
    // who controls the player.
    bool applyControl(bool toAI);

    // Set when onCollision damages this player; the Simulation collects it after each
    // step (hitThisTick) and clears it before the next. Not hashed: it is an output of
    // the tick, derived from health, which is.
    bool m_hitThisTick = false;

    bool alive;
};

#endif
