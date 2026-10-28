#include "Simulation.h"

#include <stdexcept>
#include <string>

#include "AIGameView.h"
#include "EntityFactory.h"
#include "EntityMessageQueue.h"
#include "GameOverTimer.h"
#include "GameSettings.h"
#include "Map.h"
#include "SimRandom.h"

namespace
{
int s_instancesAlive = 0;

// Where player `playerNumber` (1-based) starts. Fixed corners and edges.
void startingTileFor(int playerNumber, int mapWidth, int mapHeight, int& atX, int& atY)
{
  atX = 0;
  atY = 0;
  switch (playerNumber)
  {
    case 1:
    case 4:
      atX = 1;
      break;
    case 2:
    case 3:
      atX = mapWidth - 2;
      break;
    case 5:
    case 6:
      atX = mapWidth / 2;
      break;
  }
  switch (playerNumber)
  {
    case 1:
    case 3:
    case 5:
      atY = 1;
      break;
    case 2:
    case 4:
    case 6:
      atY = mapHeight - 2;
      break;
  }
}
} // namespace

Simulation::Simulation()
{
  if (s_instancesAlive != 0)
  {
    throw std::logic_error("Simulation: only one Simulation may exist per process "
                           "(it runs on process-lifetime singletons)");
  }
  s_instancesAlive++;
}

Simulation::~Simulation()
{
  tearDown();
  s_instancesAlive--;
}

int Simulation::instancesAlive()
{
  return s_instancesAlive;
}

void Simulation::tearDown()
{
  // Entities first, then the pool: the order the engine always used.
  m_winner.reset();
  m_entityManager.reset();
  m_spawningPool.reset();
}

void Simulation::start(const MatchConfig& config)
{
  const std::string problem = config.problem();
  if (!problem.empty())
  {
    throw std::invalid_argument("Simulation::start: " + problem);
  }

  // 1. The previous round goes first. (A map resize may already have happened:
  // BlockEntity's destructor skips its Map write when the map was rebuilt.)
  tearDown();
  m_tick = 0;
  m_roundOver = false;
  m_roundEndedAtTick = 0;

  // 2. The config becomes the settings the sim reads.
  std::GameSettings* settings = std::GameSettings::getInstance();
  config.applyTo(*settings);
  m_config = config;
  if (Map::MAP_WIDTH != config.mapWidth || Map::MAP_HEIGHT != config.mapHeight)
  {
    throw std::runtime_error("Simulation::start: the Map is " + std::to_string(Map::MAP_WIDTH) + "x"
                             + std::to_string(Map::MAP_HEIGHT) + " but the config wants "
                             + std::to_string(config.mapWidth) + "x" + std::to_string(config.mapHeight)
                             + "; resize the Map before starting");
  }

  // 3. Seed. This must stay ahead of everything below that constructs something
  // which draws (the SpawningPool, the players, block seeding). The settings keep
  // the seed as "the seed this round used" and any pin is spent.
  SimRandom::sim().seed(config.seed);
  settings->setRoundSeed(config.seed);
  settings->clearRoundSeedPin();

  // 4. EntityManager's constructor resets the message queue, the GameOverTimer,
  // the entity ids and (through DynamicMap) AIGameView.
  m_entityManager.reset(new EntityManager());

  // 5. The Map's blocks, then proof that every singleton is fresh.
  Map::getInstance()->clearBlocks();
  checkSingletonsAreFresh();

  // 6. The world.
  m_spawningPool.reset(new SpawningPool(config.numPlayers() * 100));
  spawnPlayers();
  seedBlocks();
}

void Simulation::checkSingletonsAreFresh() const
{
  if (!EntityMessageQueue::getInstance()->empty())
  {
    throw std::logic_error("Simulation::start: the EntityMessageQueue still holds the last round's messages");
  }
  if (std::GameOverTimer::getInstance()->isTimerTriggered())
  {
    throw std::logic_error("Simulation::start: the GameOverTimer is still latched from the last round");
  }
  if (EntityFactory::getInstance()->nextId() != 1)
  {
    throw std::logic_error("Simulation::start: entity ids were not restarted (next id is "
                           + std::to_string(EntityFactory::getInstance()->nextId()) + ")");
  }
  AIGameView* view = AIGameView::getInstance();
  Map* map = Map::getInstance();
  for (int x = 0; x < m_config.mapWidth; x++)
  {
    for (int y = 0; y < m_config.mapHeight; y++)
    {
      if (!view->getEntityInfoAtLocation(x, y).empty())
      {
        throw std::logic_error("Simulation::start: AIGameView still holds an entity at ("
                               + std::to_string(x) + "," + std::to_string(y) + ") from the last round");
      }
      if (map->staticTileAt(x, y) == Map::BLOCK)
      {
        throw std::logic_error("Simulation::start: the Map still has a block at ("
                               + std::to_string(x) + "," + std::to_string(y) + ")");
      }
    }
  }
}

void Simulation::spawnPlayers()
{
  EntityMessageQueue* emq = EntityMessageQueue::getInstance();

  for (int i = 1; i <= m_config.numPlayers() && i <= std::GameSettings::MAX_PLAYERS; i++)
  {
    int atX = 0;
    int atY = 0;
    startingTileFor(i, m_config.mapWidth, m_config.mapHeight, atX, atY);

    unsigned int flags = 0;
    if (m_config.slots[i - 1].ai)
    {
      flags |= GameEntity::AI_CONTROLLED_BOT_FLAG;
    }

    emq->postCreateMessage(PLAYER_CHARACTER, i, atX, atY, flags);
    m_entityManager->processMessageQueue();
  }
}

void Simulation::seedBlocks()
{
  // Places CPU-team blocks over most of the map: blockPercentage is the chance
  // for each tile that is not a player's start corner or a wall pillar. (The
  // Map's blocks were cleared in start().)
  int blockPercentage = m_config.blockPercentage;
  if (blockPercentage < 0)
  {
    blockPercentage = 0;
  }
  if (blockPercentage > 100)
  {
    blockPercentage = 100;
  }

  const int mapWidth = m_config.mapWidth;
  const int mapHeight = m_config.mapHeight;
  Map* staticMap = Map::getInstance();
  EntityMessageQueue* emq = EntityMessageQueue::getInstance();

  for (int y = 1; y < mapHeight - 1; y++)
  {
    for (int x = 1; x < mapWidth - 1; x++)
    {
      bool badSpot = false;
      // check if in player spawn corners
      if ((x < 4) && (y < 4))
      {
        badSpot = true;
      }
      if ((x < 4) && (y > mapHeight - 5))
      {
        badSpot = true;
      }
      if ((x > mapWidth - 5) && (y < 4))
      {
        badSpot = true;
      }
      if ((x > mapWidth - 5) && (y > mapHeight - 5))
      {
        badSpot = true;
      }

      // now ok to place blocks in non-wall tiles
      // check for static map wall piece
      if (staticMap->staticTileAt(x, y) == Map::WALL)
      {
        badSpot = true;
      }

      int team = 0;
      if (x < 6 && y < 5)
      {
        team = 1;
      }
      if (x >= mapWidth - 6 && y >= mapHeight - 5)
      {
        team = 2;
      }
      if (x >= mapWidth - 6 && y < 5)
      {
        team = 3;
      }
      if (x < 6 && y >= mapHeight - 5)
      {
        team = 4;
      }

      if (!badSpot)
      {
        int roll = SimRandom::sim().nextInt(100);
        if (roll < blockPercentage)
        {
          emq->postCreateMessage(BLOCK, team, x, y, 0);
        }
      }
    } // end for x
  }   // end for y

  // deal with block creation messages
  m_entityManager->processMessageQueue();
}

void Simulation::step()
{
  if (!started())
  {
    throw std::logic_error("Simulation::step: call start() first");
  }

  m_tick++;

  // Top of the tick: the win check and the game-over countdown. During the
  // countdown the sim keeps running (and, in the real game, an AI player can
  // still be taken over). The pool's canSpawnMore() comes from the same pool that
  // update()s below.
  const bool endConditionHolds = m_entityManager->oneOrZeroPlayersRemain(m_spawningPool->canSpawnMore());
  const bool over = std::GameOverTimer::getInstance()->advance(m_tick, endConditionHolds);
  if (over && !m_roundOver)
  {
    // Fixed now, from the state at the top of this tick.
    m_winner = m_entityManager->getWinningPlayer(m_spawningPool->canSpawnMore());
  }

  // The rest of the step runs on the tick the round ends, too.
  m_spawningPool->update();
  m_entityManager->runFrame();

  if (over && !m_roundOver)
  {
    m_roundOver = true;
    m_roundEndedAtTick = m_tick;
  }
}

std::shared_ptr<PlayerCharacterEntity> Simulation::currentWinner()
{
  return m_entityManager->getWinningPlayer(m_spawningPool->canSpawnMore());
}

uint64_t Simulation::stateHash() const
{
  if (!started())
  {
    throw std::logic_error("Simulation::stateHash: call start() first");
  }

  std::GameSettings* settings = std::GameSettings::getInstance();
  DynamicMap* dynamicMap = m_entityManager->getDynamicMap();
  const int width = settings->getMapWidth();
  const int height = settings->getMapHeight();

  StateHasher h;

  // Ids of the live entities in walk order, for every ref() below.
  std::vector<int> ids;
  ids.reserve(dynamicMap->entityList.size());
  for (std::map<int, std::shared_ptr<GameEntity>>::const_iterator it = dynamicMap->entityList.begin();
       it != dynamicMap->entityList.end(); ++it)
  {
    ids.push_back(it->first);
  }
  h.setEntityIds(ids);

  // 0, 1
  h.i32(STATE_HASH_VERSION);
  h.u64(m_tick);
  h.flag(m_roundOver);

  // 2: the settings the sim reads
  h.i32(width);
  h.i32(height);
  h.i32(settings->getNumberOfPlayers());
  for (int slot = 1; slot <= settings->getNumberOfPlayers(); slot++)
  {
    h.i32(settings->teamNumberForPlayer(slot));
  }
  h.flag(settings->isNpcSpawnerEnabled());

  // 3: the sim RNG
  h.u64(SimRandom::sim().stateValue());
  h.u64(SimRandom::sim().incValue());

  // 4: the game-over timer
  std::GameTimer* timer = std::GameOverTimer::getInstance();
  h.flag(timer->isTimerTriggered());
  h.u64(timer->getTriggeredAtTick());

  // 5: the spawning pool
  m_spawningPool->hashState(h);

  // 6: messages still waiting
  EntityMessageQueue::getInstance()->hashState(h);

  // 7: the static map
  Map* map = Map::getInstance();
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      h.u8((uint8_t) map->staticTileAt(x, y));
    }
  }

  // 8: the entities
  h.count(dynamicMap->entityList.size());
  for (std::map<int, std::shared_ptr<GameEntity>>::const_iterator it = dynamicMap->entityList.begin();
       it != dynamicMap->entityList.end(); ++it)
  {
    it->second->hashState(h);
  }

  // 9: the per-tile entity lists
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::vector<int>& tile = dynamicMap->entityMap[x][y];
      h.count(tile.size());
      for (size_t i = 0; i < tile.size(); i++)
      {
        h.ref(tile[i]);
      }
    }
  }

  // 10: AIGameView's per-tile lists
  AIGameView* view = AIGameView::getInstance();
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::vector<EntityInfo>& tile = view->getEntityInfoAtLocation(x, y);
      h.count(tile.size());
      for (size_t i = 0; i < tile.size(); i++)
      {
        h.ref(tile[i].id);
        h.i32(tile[i].playerNumber);
        h.i32(tile[i].type);
        h.i32(tile[i].groupType);
        h.flag(tile[i].staticFlag);
      }
    }
  }

  return h.value();
}
