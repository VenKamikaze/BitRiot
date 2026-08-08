
#include "EntityManager.h"
#include "GameEntity.h"
#include "MickLogger.h"
#include "PlayerCharacterEntity.h"
#include <memory>
#include <set>
#include <string>

EntityManager::EntityManager()
{
  // set dynamicMap vector sizes to map width and height
  m_pDynamicMap = new DynamicMap(GameSettings::getInstance()->getMapWidth(), GameSettings::getInstance()->getMapHeight());

  //m_playerDead = new std::vector<bool>(GameSettings::getInstance()->getNumberOfPlayers(), true);
}

EntityManager::~EntityManager()
{
  delete m_pDynamicMap;
  //delete m_playerDead;
  //m_playerDead = NULL;
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
    {
      m_pDynamicMap->entityIterator->second->render(destination);
    }
  }
  for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
       m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
       m_pDynamicMap->entityIterator++)
  {
    if (m_pDynamicMap->entityIterator->second->getStaticFlag() == false)
    {
      m_pDynamicMap->entityIterator->second->render(destination);
    }
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
          shared_ptr<GameEntity> entity = ef->getEntity(typeToCreate, message->getTeamToCreate(),
                                              x, y, message->getFlags(), message->getOffsetX(), message->getOffsetY());

          int id = entity->getID();

          // add to entity list
          pair<int, shared_ptr<GameEntity>> value(id, entity);
          m_pDynamicMap->entityList.insert(value);
          // add entity to dynamic map
          m_pDynamicMap->entityMap[x][y].push_back(id);

          // check for player character creation for setting dead flag
          //if (typeToCreate == PLAYER_CHARACTER)
          //{
          //  m_playerDead->at(message->getTeamToCreate() - 1) = false;
          //}

          // also add to AIGameView
          EntityInfo info;
          info.id = id;
          info.playerNumber = entity->getPlayerNumber();
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
          {
            break;
          }

          int atX = m_pDynamicMap->entityIterator->second->getTileX();
          int atY = m_pDynamicMap->entityIterator->second->getTileY();

          // check for setting player dead flag
          if (m_pDynamicMap->entityIterator->second->getType() == PLAYER_CHARACTER)
          {
            //m_playerDead->at(m_pDynamicMap->entityIterator->second->getPlayerNumber() - 1) = true;
            static_pointer_cast<PlayerCharacterEntity>(m_pDynamicMap->entityIterator->second)->setAlive(false);
          }

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
            m_pDynamicMap->entityIterator->second = 0;
            m_pDynamicMap->entityList.erase(m_pDynamicMap->entityIterator);

            // also remove from AI Game View
            AIGameView::getInstance()->removeEntity(id, atX, atY);
          }
          else
          {
            // entity was still live in entityList (not a duplicate destroy) but was not
            // found at its own reported (atX,atY) in entityMap - so it is NOT removed from
            // entityList or AIGameView here. It keeps updating and stays visible to
            // AIGameView lookups (e.g. RocketSpinEntity::enemyInTile) at whatever stale
            // location it was last registered at - a candidate cause of "ghost" targets.
            std::string msg = std::string("EntityManager DESTROY_ENTITY desync: id=") + std::to_string(id)
              + " not found in entityMap at its own (atX,atY)=(" + std::to_string(atX) + "," + std::to_string(atY) + ")"
              + " - entity NOT removed from entityList/AIGameView";
            MickLogger::getInstance()->warn(nullptr, msg);
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

unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> EntityManager::getAllPlayerCharacters()
{
  unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> players = make_unique<vector<shared_ptr<PlayerCharacterEntity>>>();
  int numPlayers = 0;
  // set up player character entity pointers
  for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
       m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
       m_pDynamicMap->entityIterator++)
  {
    if (m_pDynamicMap->entityIterator->second->getType() == PLAYER_CHARACTER)
    {
      players->resize(++numPlayers, std::static_pointer_cast<PlayerCharacterEntity>(m_pDynamicMap->entityIterator->second));
    }
  }

  return players;
}

shared_ptr<PlayerCharacterEntity> EntityManager::getPlayerPointer(int playerNumber)
{

  // set up player character entity pointers
  for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
       m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
       m_pDynamicMap->entityIterator++)
  {
    if (m_pDynamicMap->entityIterator->second->getType() == PLAYER_CHARACTER)
    {
      if (m_pDynamicMap->entityIterator->second->getPlayerNumber() == playerNumber)
      {
        return std::static_pointer_cast<PlayerCharacterEntity>(m_pDynamicMap->entityIterator->second);
        //return (shared_ptr<PlayerCharacterEntity>) m_pDynamicMap->entityIterator->second;
      }
    }
  }

  return nullptr;
}

shared_ptr<PlayerCharacterEntity> EntityManager::getWinningPlayer(bool moreHostilesPossible)
{
  if(oneOrZeroPlayersRemain(moreHostilesPossible))
  {
    unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> playerCharacters = getAllPlayerCharacters();
    for(const auto& player : *playerCharacters)
    {
      if(player->isAlive())
      {
        MickLogger::getInstance()->debug(this, string("Player " + to_string(player->getPlayerNumber()) + " is the winning player").c_str());
        return player;
      }
    }
  }
  return nullptr;
}

bool EntityManager::oneOrZeroPlayersRemain(bool moreHostilesPossible)
{
  // A single-team (co-op) round is all players allied vs the NPC spawner: it
  // must keep running until the whole team is wiped out, not end at kickoff
  // when only one team exists. So there the round ends only when either the
  // team is wiped out (loss), OR the board is cleared of hostile NPCs and the
  // spawner can produce no more (win). The latter avoids the degenerate case
  // where the spawner exhausts, leaving living humans with nothing to kill
  // them and the round never terminating.
  if(GameSettings::getInstance()->numConfiguredTeams() <= 1)
  {
    return allPlayersDead() || (!moreHostilesPossible && !anyHostileNpcsAlive());
  }
  // PvP: the round ends when one (or zero) alliance teams remain, not when one
  // player head remains. In the default free-for-all each player is their own
  // team, so this is identical to numPlayersAlive() <= 1.
  return numAliveTeams() <= 1;
}

int EntityManager::numAliveTeams()
{
  std::set<int> aliveTeams;
  unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> playerCharacters = getAllPlayerCharacters();
  for(const auto& player : *playerCharacters)
  {
    if(player->isAlive())
    {
      aliveTeams.insert(player->getTeamNumber());
    }
  }
  return (int) aliveTeams.size();
}

bool EntityManager::allPlayersDead()
{
  return numPlayersAlive() == 0;
}

int EntityManager::numPlayersAlive()
{
  unsigned int remainingPlayers = 0;
  unique_ptr<vector<shared_ptr<PlayerCharacterEntity>>> playerCharacters = getAllPlayerCharacters();
  for(const auto& player : *playerCharacters)
  {
    if(player->isAlive())
    {
      remainingPlayers++;
    }
  }
  return remainingPlayers;
}

bool EntityManager::anyHostileNpcsAlive()
{
  // Hostile NPCs are the CPU spawning pool's creatures: eggs about to hatch and
  // the monsters they become. All carry playerNumber 0. Player-laid eggs and
  // their hatchlings carry the owning player's number (a teammate in co-op) and
  // must be excluded. Powerups/blocks are also playerNumber 0 but fall outside
  // these groups. NPCs are removed from entityList on death, so presence here
  // means alive.
  for (m_pDynamicMap->entityIterator = m_pDynamicMap->entityList.begin();
       m_pDynamicMap->entityIterator != m_pDynamicMap->entityList.end();
       m_pDynamicMap->entityIterator++)
  {
    const shared_ptr<GameEntity>& entity = m_pDynamicMap->entityIterator->second;
    if (entity->getPlayerNumber() != 0)
    {
      continue; // player-owned egg/monster, not a hostile
    }
    EntityGroupType group = entity->getGroupType();
    if (group == EGG_GROUP || group == ROBOT_GROUP || group == PLAYER_SPAWN_GROUP)
    {
      return true;
    }
  }
  return false;
}

void EntityManager::runCollisions()
{
  // sends collision data between colliding entities
  // for each tile, run collisions between entities
  for (int y = 1; y < GameSettings::getInstance()->getMapHeight() - 1; y++)
  {
    for (int x = 1; x < GameSettings::getInstance()->getMapWidth() - 1; x++)
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
          shared_ptr<GameEntity> currentEntity = m_pDynamicMap->entityList.find(currentID)->second;

          for (mapLocationIterator2 = m_pDynamicMap->entityMap[x][y].begin();
               mapLocationIterator2 != m_pDynamicMap->entityMap[x][y].end();
               mapLocationIterator2++)
          {
            int otherID = (*mapLocationIterator2);
            if (otherID != currentID)
            {
              // entities are different
              // run collision on other entity
              shared_ptr<GameEntity> otherEntity = m_pDynamicMap->entityList.find(otherID)->second;
              otherEntity->onCollision(currentEntity->getType(),
                                       currentEntity->getGroupType(),
                                       currentEntity->getPlayerNumber());

            }
          }
        }
      }

    }
  }
}
