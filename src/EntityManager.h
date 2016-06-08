// EntityManager class
// creates, destroys, updates and renders all
// entities in the game

#ifndef _ENTITYMANAGER
#define _ENTITYMANAGER

#include "DynamicMap.h"
#include "PlayerCharacterEntity.h"
#include "EntityFactory.h"

using namespace std;


class EntityManager
{
public:
	EntityManager();
	~EntityManager();

	// game loop functions
    void runFrame(); // performs all frame-specific processing, see private functions
	void renderEntities(SDL_Surface*  destination); // renders frame to supplied surface

	void processMessageQueue(); // processes all messages in EntityMessageQueue (create, move and destroy entities)
	PlayerCharacterEntity * getPlayerPointer(int team);
	bool getPlayerDead(int team) { return m_playerDead[team - 1]; }

	DynamicMap * getDynamicMap() { return m_pDynamicMap; }

private:
	// private game loop functions called from runFrame()
	void runAI(); // runs AI and movement for every entity
	void runCollisions(); // sends collision data between colliding entities	
	
	bool m_playerDead[4];

	// entity manager data structures
	DynamicMap * m_pDynamicMap;

};

#endif 
