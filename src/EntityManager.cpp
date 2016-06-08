
#include "EntityManager.h"

EntityManager::EntityManager()
{
	// set dynamicMap vector sizes to map width and height
	m_pDynamicMap = new DynamicMap(Map::MAP_WIDTH, Map::MAP_HEIGHT);	

	for (int i = 0; i < 4; i++)
		m_playerDead[i] = true;
}

EntityManager::~EntityManager()
{
	delete m_pDynamicMap;
}


void EntityManager::runFrame()
{
	// performs all frame-specific processing, see private functions

	runAI(); // runs AI and movement for every entity
	
	processMessageQueue(); // processes all messages in EntityMessageQueue (create, move and destroy entities)
	
	runCollisions(); // sends collision data between colliding entities
	
}

void EntityManager::renderEntities(SDL_Surface*  destination)
{
	// renders frame to supplied surface

	// first render static map, then dynamic map
	Map * staticMap = Map::getInstance();
	staticMap->render(destination, 0, 0);


	// just iterate through entity list
	// first render static entities, then non-static ones
	for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
		m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
		m_pDynamicMap->entityIterator++)
	{
		if (m_pDynamicMap->entityIterator->second->getStaticFlag() == true)
			m_pDynamicMap->entityIterator->second->render(destination);
	}
	for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
		m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
		m_pDynamicMap->entityIterator++)
	{
		if (m_pDynamicMap->entityIterator->second->getStaticFlag() == false)
			m_pDynamicMap->entityIterator->second->render(destination);
	}

}


void EntityManager::runAI()
{
	// runs AI and movement for every entity
	for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
		m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
		m_pDynamicMap->entityIterator++)
	{
		m_pDynamicMap->entityIterator->second->update();
	}


}

void EntityManager::processMessageQueue()
{
	// processes all messages in EntityMessageQueue (create, move and destroy entities)
	EntityMessageQueue * emq = EntityMessageQueue::getInstance();

	while (!(emq->empty()))
	{
		EntityMessage * message = emq->getMessageAtFront();

		// switch on message type for action to perform
		switch (message->getType())
		{
		case EntityMessage::CREATE_ENTITY:
			{
				EntityFactory * ef = EntityFactory::getInstance();

				// create new entity				
				int x, y;
				x = message->getToX();
				y = message->getToY();

				EntityType typeToCreate = (EntityType)message->getTypeToCreate();
				GameEntity * entity = ef->getEntity(typeToCreate, message->getTeamToCreate(), 
					x, y, message->getFlags(), message->getOffsetX(), message->getOffsetY());

				int id = entity->getID();

				// add to entity list
				pair<int, GameEntity *> value(id, entity);
				m_pDynamicMap->entityList.insert(value);
				// add entity to dynamic map
				m_pDynamicMap->entityMap[x][y].push_back(id);
				
				// check for player character creation for setting dead flag
				if (typeToCreate == PLAYER_CHARACTER)
				{
					m_playerDead[message->getTeamToCreate() - 1] = false;
				}

				// also add to AIGameView
				EntityInfo info;
				info.id = id;
				info.team = entity->getTeam();
				info.type = entity->getType();
				info.groupType = entity->getGroupType();
				info.staticFlag = entity->getStaticFlag();
				AIGameView::getInstance()->addNewEntity(info, x, y);
			}
			break;
		case EntityMessage::DESTROY_ENTITY:
			{
				int id = message->getSender();
				// find entity in list
				m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.find(id);

				// check has not already been destroyed/can't be found
				if (m_pDynamicMap->entityIterator == m_pDynamicMap->entityList.end())
					break;

				int atX = m_pDynamicMap->entityIterator->second->getTileX();
				int atY = m_pDynamicMap->entityIterator->second->getTileY();

				// check for setting player dead flag
				if (m_pDynamicMap->entityIterator->second->getType() == PLAYER_CHARACTER)
					m_playerDead[m_pDynamicMap->entityIterator->second->getTeam() - 1] = true;

				// remove from map
				bool foundOnMap = false;
				for (m_pDynamicMap->mapLocationIterator = m_pDynamicMap->entityMap[atX][atY].begin();
					m_pDynamicMap->mapLocationIterator != m_pDynamicMap->entityMap[atX][atY].end();
					m_pDynamicMap->mapLocationIterator++)
				{
					if ((*m_pDynamicMap->mapLocationIterator) == id)
					{
						m_pDynamicMap->entityMap[atX][atY].erase(m_pDynamicMap->mapLocationIterator);
						foundOnMap = true;
						break; // from for loop
					}		
				}
				if (foundOnMap) // may not be found on map due to multiple destroy messages
				{
					// delete and remove from list
					delete m_pDynamicMap->entityIterator->second;
					m_pDynamicMap->entityIterator->second = 0;
					m_pDynamicMap->entityList.erase(m_pDynamicMap->entityIterator);	

					// also remove from AI Game View
					AIGameView::getInstance()->removeEntity(id, atX, atY);
				}
			}

			break;
		case EntityMessage::MOVE_ENTITY:
			{
				m_pDynamicMap->moveEntityOnMap(message->getSender(), 
					message->getFromX(), message->getFromY(),
					message->getToX(), message->getToY());
			}
			break;
		}

		emq->deleteMessageAtFront();
	}
}

PlayerCharacterEntity * EntityManager::getPlayerPointer(int team)
{
	
    // set up player character entity pointers
	for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin(); 
		m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end(); 
		m_pDynamicMap->entityIterator++)
	{
		if (m_pDynamicMap->entityIterator->second->getType() == PLAYER_CHARACTER)
		{
			if (m_pDynamicMap->entityIterator->second->getTeam() == team)
				return (PlayerCharacterEntity*)m_pDynamicMap->entityIterator->second;
		}
	}

	return NULL;
}

void EntityManager::runCollisions()
{
	// sends collision data between colliding entities
	// for each tile, run collisions between entities
	for (int y = 1; y < Map::MAP_HEIGHT - 1; y++)
	{
		for (int x = 1; x < Map::MAP_WIDTH - 1; x++)
		{
			if (m_pDynamicMap->entityMap[x][y].size() >= 2)
			{
				vector<int>::iterator mapLocationIterator2;
				for (m_pDynamicMap->mapLocationIterator = m_pDynamicMap->entityMap[x][y].begin();
					m_pDynamicMap->mapLocationIterator != m_pDynamicMap->entityMap[x][y].end();
					m_pDynamicMap->mapLocationIterator++)
				{
					// for each entity in map location
					int currentID = (*m_pDynamicMap->mapLocationIterator);
					GameEntity * currentEntity = m_pDynamicMap->entityList.find(currentID)->second;

					for (mapLocationIterator2 = m_pDynamicMap->entityMap[x][y].begin();
						mapLocationIterator2 != m_pDynamicMap->entityMap[x][y].end();
						mapLocationIterator2++) 
					{
						int otherID = (*mapLocationIterator2);
						if (otherID != currentID)
						{
							// entities are different
							// run collision on other entity
							GameEntity * otherEntity = m_pDynamicMap->entityList.find(otherID)->second;
							otherEntity->onCollision(currentEntity->getType(),
								currentEntity->getGroupType(),
								currentEntity->getTeam());

						}
					}
				}
			}

		}
	}
}
