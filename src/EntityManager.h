// EntityManager class
// creates, destroys and updates all
// entities in the game

#ifndef _ENTITYMANAGER
#define _ENTITYMANAGER

#include "DynamicMap.h"
#include "PlayerCharacterEntity.h"
#include "EntityFactory.h"
#include "GameSettings.h"
#include "MickLogger.h"
#include <memory>

using namespace std;


class EntityManager
{
  public:
    EntityManager();
    ~EntityManager();

    // game loop functions
    void runFrame(); // performs all frame-specific processing, see private functions

    void processMessageQueue(); // processes all messages in EntityMessageQueue (create, move and destroy entities)
    // The *live* player characters only: a dead player is erased from the entity
    // list, so it drops out of this set. Win-condition, AI and getPlayerPointer
    // logic all rely on that - use getRoundPlayerRoster() for "everyone who played".
    unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> getAllPlayerCharacters();
    // Every player character created since this EntityManager was constructed
    // (i.e. this round), in creation order, including those since destroyed.
    // Read-only history for the end-of-round scoreboard; entries are never
    // simulated, rendered or collided. Reset by constructing a new EntityManager.
    const vector<shared_ptr<PlayerCharacterEntity>>& getRoundPlayerRoster() const
    {
      return m_roundPlayerRoster;
    }
    shared_ptr<PlayerCharacterEntity> getPlayerPointer(int playerNumber);
    //bool getPlayerDead(int playerIndex)
    //{
    //  return m_playerDead->at(playerIndex);
    //}
    // moreHostilesPossible: whether the CPU spawning pool can still produce
    // enemies (only affects the co-op branch; ignored in PvP). Passed in by
    // GameEngine, which owns the SpawningPool.
    shared_ptr<PlayerCharacterEntity> getWinningPlayer(bool moreHostilesPossible); // returns winning player or nullptr if a draw.
    bool oneOrZeroPlayersRemain(bool moreHostilesPossible);
    bool allPlayersDead();
    int numPlayersAlive();
    // Number of distinct alliance teams with at least one player still alive.
    // Team play ends when this drops to <= 1 (last team standing wins).
    int numAliveTeams();
    // True if any CPU-owned (playerNumber 0) hostile NPC — a hatching egg or a
    // monster — is still on the board. Player-laid eggs/monsters carry their
    // owner's playerNumber and are excluded. Used to decide when a co-op round
    // has been cleared.
    bool anyHostileNpcsAlive();

    DynamicMap * getDynamicMap()
    {
      return m_pDynamicMap;
    }

  private:
    // private game loop functions called from runFrame()
    void runAI(); // runs AI and movement for every entity
    void runCollisions(); // sends collision data between colliding entities

    // Removes id from the dynamic map's tile lists. Looks at (tileX, tileY) first,
    // then everywhere; on success tileX/tileY are where it actually was.
    bool removeFromEntityMap(int id, int & tileX, int & tileY);

    //std::vector<bool>* m_playerDead = NULL;
    //bool m_playerDead[4];

    // entity manager data structures
    DynamicMap * m_pDynamicMap;

    // Every player character spawned this round, kept alive past DESTROY_ENTITY
    // so the scoreboard can still read their stats. Populated in CREATE_ENTITY.
    vector<shared_ptr<PlayerCharacterEntity>> m_roundPlayerRoster;

};

#endif
