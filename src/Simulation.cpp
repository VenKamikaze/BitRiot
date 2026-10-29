#include "Simulation.h"

#include "BuildId.h"
#include "DataHash.h"

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include "DynamicMap.h"

#include "AIGameView.h"
#include "EntityFactory.h"
#include "EntityMessageQueue.h"
#include "GameOverTimer.h"
#include "GameSettings.h"
#include "Map.h"
#include "SimRandom.h"

// The hash writes these enums as their C++ values (see the specification in
// Simulation.h). If one of these fails to compile, the numbering moved: bump
// Simulation::STATE_HASH_VERSION and update the list in Simulation.h.
static_assert(BLOCK == 0 && EGG1 == 1 && EGG5 == 5 && ENPLUS_POWERUP == 6 && IQPLUS_POWERUP == 7
              && BOMB == 8 && MINE == 9 && ROCKET_SPIN == 10 && ROCKET_FIRED == 11
              && ENERGY_DROP == 12 && EXPLOSION == 13 && PLAYER_CHARACTER == 14
              && EGG_THIEF == 15 && WALL_EATER == 16 && DRONE == 17 && ROBOT1 == 18
              && MUNCH_EFFECT == 19 && DAMAGE_EFFECT == 20 && NUM_ENTITY_TYPES == 21,
              "EntityType numbering is part of the state hash");
static_assert(BLOCK_GROUP == 0 && EGG_GROUP == 1 && POWERUP_GROUP == 2 && WEAPON_GROUP == 3
              && EXPLOSION_GROUP == 4 && CHARACTER_GROUP == 5 && PLAYER_SPAWN_GROUP == 6
              && MONSTER_GROUP == 7 && ROBOT_GROUP == 8 && EFFECT_GROUP == 9,
              "EntityGroupType numbering is part of the state hash");
static_assert(Map::WALL == 0 && Map::EMPTY == 1 && Map::BLOCK == 2,
              "Map::StaticTile numbering is part of the state hash");
static_assert(UP == 0 && DOWN == 1 && LEFT == 2 && RIGHT == 3,
              "DIRECTION numbering is part of the state hash");
static_assert(EntityAnim::IDLE == 0 && EntityAnim::MOVE_UP == 1 && EntityAnim::MOVE_DOWN == 2
              && EntityAnim::MOVE_LEFT == 3 && EntityAnim::MOVE_RIGHT == 4
              && SimpleBrain::IDLE == 0 && SimpleBrain::MOVE_RIGHT == 4,
              "the anim state numbering is part of the state hash");
static_assert(EntityMessage::DESTROY_ENTITY == 0 && EntityMessage::CREATE_ENTITY == 1
              && EntityMessage::MOVE_ENTITY == 2,
              "EntityMessage::MessageType numbering is part of the state hash");
static_assert(AIGoal::GOAL_INCREASE_ENERGY == 0 && AIGoal::GOAL_SECURE_AREA == 1
              && AIGoal::GOAL_CREATE_ENTITIES == 2 && AIGoal::GOAL_ATTACK_TEAM == 3
              && AIGoal::GOAL_ATTACK_ENTITY == 4 && AIGoal::GOAL_GET_POWERUP == 5,
              "AIGoal::GOAL_TYPES numbering is part of the state hash");
static_assert(AIInstruction::GO_TO == 0 && AIInstruction::MOVE_FROM_LOCATION == 1
              && AIInstruction::CREATE_ENTITY == 2 && AIInstruction::WAIT == 3,
              "AIInstruction::INSTRUCTION_TYPES numbering is part of the state hash");
static_assert(GameEntity::UP_FLAG == (1u << 1) && GameEntity::DOWN_FLAG == (1u << 2)
              && GameEntity::LEFT_FLAG == (1u << 3) && GameEntity::RIGHT_FLAG == (1u << 4)
              && GameEntity::WALK_THROUGH_BLOCKS == (1u << 5) && GameEntity::LAY_BLOCKS == (1u << 6)
              && GameEntity::LAY_MINES == (1u << 7) && GameEntity::FIRE_ROCKETS == (1u << 8)
              && GameEntity::LAY_BOMBS == (1u << 9) && GameEntity::AI_CONTROLLED_BOT_FLAG == (1u << 10),
              "the GameEntity creation flag bits are part of the state hash");

namespace
{
int s_instancesAlive = 0;

// EntityType names, for the schema trace only (indexed by EntityType value).
const char* const ENTITY_TYPE_NAMES[NUM_ENTITY_TYPES] = {
  "BLOCK", "EGG1", "EGG2", "EGG3", "EGG4", "EGG5", "ENPLUS_POWERUP", "IQPLUS_POWERUP",
  "BOMB", "MINE", "ROCKET_SPIN", "ROCKET_FIRED", "ENERGY_DROP", "EXPLOSION",
  "PLAYER_CHARACTER", "EGG_THIEF", "WALL_EATER", "DRONE", "ROBOT1", "MUNCH_EFFECT", "DAMAGE_EFFECT"
};

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
  // 0. Everything that can be refused is refused before anything is touched, so a
  // bad config leaves the running round and GameSettings exactly as they were.
  const std::string problem = config.runnableProblem();
  if (!problem.empty())
  {
    throw std::invalid_argument("Simulation::start: " + problem);
  }

  // 0b. The simulation's data files are read now, not on first use: a file that is
  // missing or does not parse fails here (std::runtime_error) instead of answering 1
  // for every constant. Also before anything is touched.
  DataHash::loadSimData();

  // 1. The previous round goes first, while the Map is still the size it was built
  // at: the blocks it placed clear their own tiles as they are destroyed.
  tearDown();
  m_tick = 0;
  m_roundOver = false;
  m_roundEndedAtTick = 0;
  m_slotInput = std::array<SlotInputState, std::GameSettings::MAX_PLAYERS>();
  m_hitThisTick.fill(false);

  // 2. The config becomes the settings the sim reads.
  std::GameSettings* settings = std::GameSettings::getInstance();
  config.applyTo(*settings);
  m_config = config;

  // 2b. The round brings its own map: the layout is rebuilt if the config's size
  // differs from the Map's (the walls and pillars only; no bitmaps are involved,
  // so nothing outside the simulation needs to know). The window, the HUD and the
  // tile pictures are the caller's, and follow the size separately.
  Map::getInstance()->resize(config.mapWidth, config.mapHeight);

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
  m_spawningPool.reset(new SpawningPool(config.numPlayers() * POOL_POINTS_PER_PLAYER));
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

void Simulation::step(const TickInputs& inputs)
{
  if (!started())
  {
    throw std::logic_error("Simulation::step: call start() first");
  }
  // Refuse a bad event before anything of this tick happens, so a refusal leaves the
  // simulation exactly as it was (and no event of the tick is half applied).
  validateControlEvents(inputs.control);

  m_tick++;

  // The hit flags are an output of one tick: forget last tick's, collect this tick's
  // at the end (hitThisTick).
  clearHits();

  // Who controls which slot changes first, at the very start of the tick, so the
  // bot (or the human) it now is acts on this tick. Then the human input: players
  // move and lay items here, before the win check and the AI, as the engine always
  // applied input before its tick.
  applyControlEvents(inputs.control);
  applyInputs(inputs);

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

  collectHits();
}

const Simulation::SlotInputState& Simulation::inputState(int slot) const
{
  if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
  {
    throw std::out_of_range("Simulation::inputState: slot " + std::to_string(slot) + " is outside 0.."
                            + std::to_string(std::GameSettings::MAX_PLAYERS - 1) + " (slot = playerNumber - 1)");
  }
  return m_slotInput[slot];
}

bool Simulation::hitThisTick(int slot) const
{
  if (slot < 0 || slot >= std::GameSettings::MAX_PLAYERS)
  {
    throw std::out_of_range("Simulation::hitThisTick: slot " + std::to_string(slot) + " is outside 0.."
                            + std::to_string(std::GameSettings::MAX_PLAYERS - 1) + " (slot = playerNumber - 1)");
  }
  return m_hitThisTick[slot];
}

EntityType Simulation::heldSelection(int slot) const
{
  const SlotInputState& st = inputState(slot);
  return (st.action1Down || st.action2Down) ? st.spawnSelection : PLAYER_CHARACTER;
}

std::shared_ptr<PlayerCharacterEntity> Simulation::playerForSlot(int slot) const
{
  // The roster is in creation order, which is slot order, and holds the dead too.
  for (const std::shared_ptr<PlayerCharacterEntity>& candidate : m_entityManager->getRoundPlayerRoster())
  {
    if (candidate && candidate->getPlayerNumber() == slot + 1)
    {
      return candidate;
    }
  }
  return nullptr;
}

void Simulation::clearHits()
{
  m_hitThisTick.fill(false);
  for (const std::shared_ptr<PlayerCharacterEntity>& player : m_entityManager->getRoundPlayerRoster())
  {
    if (player)
    {
      player->m_hitThisTick = false;
    }
  }
}

void Simulation::collectHits()
{
  for (const std::shared_ptr<PlayerCharacterEntity>& player : m_entityManager->getRoundPlayerRoster())
  {
    const int slot = player ? player->getPlayerNumber() - 1 : -1;
    if (slot >= 0 && slot < std::GameSettings::MAX_PLAYERS && player->m_hitThisTick)
    {
      m_hitThisTick[slot] = true;
    }
  }
}

void Simulation::validateControlEvents(const std::vector<ControlEvent>& events) const
{
  for (const ControlEvent& e : events)
  {
    if (e.slot < 0 || e.slot >= m_config.numPlayers())
    {
      throw std::out_of_range("Simulation::step: control event for slot " + std::to_string(e.slot)
                              + ", but the round has " + std::to_string(m_config.numPlayers())
                              + " players (slot = playerNumber - 1)");
    }
    if (e.kind != ControlEvent::HUMAN_TAKES_OVER && e.kind != ControlEvent::RETURN_TO_AI
        && e.kind != ControlEvent::LEAVE)
    {
      throw std::invalid_argument("Simulation::step: unknown control event kind "
                                  + std::to_string((int) e.kind));
    }
  }
}

// The rules are in the comment on ControlEvent (TickInputs.h).
void Simulation::applyControlEvents(const std::vector<ControlEvent>& events)
{
  for (const ControlEvent& e : events)
  {
    const std::shared_ptr<PlayerCharacterEntity> player = playerForSlot(e.slot);
    if (!player || !player->isAlive())
    {
      continue; // nobody to hand over: a dead player is neither driven nor bot-run
    }
    const bool toAI = e.kind != ControlEvent::HUMAN_TAKES_OVER; // RETURN_TO_AI and LEAVE
    if (player->applyControl(toAI))
    {
      // A fresh slot for the new controller: no hold, no selection carried over.
      m_slotInput[e.slot] = SlotInputState();
    }
  }
}

void Simulation::applyInputs(const TickInputs& inputs)
{
  const int slots = m_config.numPlayers() < std::GameSettings::MAX_PLAYERS ? m_config.numPlayers()
                                                                          : std::GameSettings::MAX_PLAYERS;
  for (int slot = 0; slot < slots; slot++)
  {
    applySlotInput(slot, inputs.slot[slot]);
  }
}

// The rules table is at the top of TickInputs.h; this is that table.
void Simulation::applySlotInput(int slot, PlayerInput in)
{
  const std::shared_ptr<PlayerCharacterEntity> player = playerForSlot(slot);
  if (!player || !player->isAlive() || player->isControlledByAI())
  {
    return; // nobody here to drive: a bot, a dead player, or a slot with no player
  }

  SlotInputState& st = m_slotInput[slot];
  DynamicMap* dynamicMap = m_entityManager->getDynamicMap();

  // Ends the hold; lays the selection first if the tile is free.
  const auto place = [&]()
  {
    if (dynamicMap->tileHasStaticEntity(player->getTileX(), player->getTileY()) == false)
    {
      player->createEntity(st.spawnSelection);
    }
    st.action1Down = false;
    st.action2Down = false;
    st.spawnSelection = BLOCK;
  };

  if (!st.action1Down && !st.action2Down)
  {
    // Idle: movement, then the action that begins a hold.
    if (in.held(PlayerInput::UP))
    {
      player->moveInDirection(UP);
      player->incAnimFrame();
    }
    else if (in.held(PlayerInput::DOWN))
    {
      player->moveInDirection(DOWN);
      player->incAnimFrame();
    }
    else if (in.held(PlayerInput::LEFT))
    {
      player->moveInDirection(LEFT);
      player->incAnimFrame();
    }
    else if (in.held(PlayerInput::RIGHT))
    {
      player->moveInDirection(RIGHT);
      player->incAnimFrame();
    }

    if (in.held(PlayerInput::ACTION1) || in.released(PlayerInput::ACTION1))
    {
      st.action1Down = true;
      st.spawnSelection = BLOCK;
      if (in.released(PlayerInput::ACTION1))
      {
        place(); // the tap rule: pressed and released inside this tick
      }
    }
    else if (in.held(PlayerInput::ACTION2) || in.released(PlayerInput::ACTION2))
    {
      st.action2Down = true;
      st.spawnSelection = EGG1;
      if (in.released(PlayerInput::ACTION2))
      {
        place(); // the tap rule
      }
    }
  }
  else if (st.action1Down)
  {
    if (in.held(PlayerInput::UP))
    {
      st.spawnSelection = BOMB;
    }
    else if (in.held(PlayerInput::DOWN))
    {
      st.spawnSelection = BLOCK;
    }
    else if (in.held(PlayerInput::LEFT))
    {
      st.spawnSelection = MINE;
    }
    else if (in.held(PlayerInput::RIGHT))
    {
      st.spawnSelection = ROCKET_SPIN;
    }

    if (in.released(PlayerInput::ACTION1))
    {
      place();
    }
  }
  else
  {
    // action2Down
    if (in.held(PlayerInput::UP))
    {
      st.spawnSelection = EGG3;
    }
    else if (in.held(PlayerInput::DOWN))
    {
      st.spawnSelection = EGG5;
    }
    else if (in.held(PlayerInput::LEFT))
    {
      st.spawnSelection = EGG2;
    }
    else if (in.held(PlayerInput::RIGHT))
    {
      st.spawnSelection = EGG4;
    }

    if (in.released(PlayerInput::ACTION2))
    {
      place();
    }
  }
}

std::shared_ptr<PlayerCharacterEntity> Simulation::currentWinner()
{
  return m_entityManager->getWinningPlayer(m_spawningPool->canSpawnMore());
}

uint64_t Simulation::dataHash()
{
  return DataHash::hash();
}

uint64_t Simulation::buildId()
{
  return bitriotBuildId();
}

uint64_t Simulation::stateHash() const
{
  StateHasher h;
  hashInto(h);
  return h.value();
}

std::vector<std::string> Simulation::stateHashFields() const
{
  StateHasher h;
  std::vector<std::string> trace;
  h.setTrace(&trace);
  hashInto(h);
  return trace;
}

void Simulation::hashInto(StateHasher& h) const
{
  if (!started())
  {
    throw std::logic_error("Simulation::stateHash: call start() first");
  }

  std::GameSettings* settings = std::GameSettings::getInstance();
  DynamicMap* dynamicMap = m_entityManager->getDynamicMap();
  const int width = settings->getMapWidth();
  const int height = settings->getMapHeight();

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
  h.beginSection("[header]");
  h.i32(STATE_HASH_VERSION, "version");
  h.u64(m_tick, "tick");
  h.flag(m_roundOver, "roundOver");

  // 2: the settings the sim reads
  h.beginSection("[settings]");
  h.i32(width, "settings.mapWidth");
  h.i32(height, "settings.mapHeight");
  h.i32(settings->getNumberOfPlayers(), "settings.numPlayers");
  for (int slot = 1; slot <= settings->getNumberOfPlayers(); slot++)
  {
    h.i32(settings->teamNumberForPlayer(slot), "settings.team");
  }
  h.flag(settings->isNpcSpawnerEnabled(), "settings.npcSpawnerEnabled");

  // 3: the sim RNG
  h.beginSection("[rng]");
  h.u64(SimRandom::sim().stateValue(), "rng.state");
  h.u64(SimRandom::sim().incValue(), "rng.inc");

  // 4: the game-over timer
  h.beginSection("[game over timer]");
  std::GameTimer* timer = std::GameOverTimer::getInstance();
  h.flag(timer->isTimerTriggered(), "timer.triggered");
  h.u64(timer->getTriggeredAtTick(), "timer.triggeredAtTick");

  // 5: the spawning pool
  h.beginSection("[spawning pool]");
  m_spawningPool->hashState(h);

  // 6: messages still waiting
  h.beginSection("[pending messages]");
  EntityMessageQueue::getInstance()->hashState(h);

  // 6b: the per-slot input state (which action each human holds, and what it has
  // selected). Slots past the player count are always the default and left out.
  for (int slot = 0; slot < settings->getNumberOfPlayers() && slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    h.beginSection("[input state, slot " + std::to_string(slot) + "]");
    h.flag(m_slotInput[slot].action1Down, "input.action1Down");
    h.flag(m_slotInput[slot].action2Down, "input.action2Down");
    h.i32(m_slotInput[slot].spawnSelection, "input.spawnSelection");
  }

  // 7: the static map (the dump prints it as a picture)
  h.quiet(true);
  Map* map = Map::getInstance();
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      h.u8((uint8_t) map->staticTileAt(x, y), "map.tile");
    }
  }

  // 8: the entities
  h.quiet(false);
  h.beginSection("[entities]");
  h.count(dynamicMap->entityList.size(), "entities.count");
  int walkIndex = 0;
  for (std::map<int, std::shared_ptr<GameEntity>>::const_iterator it = dynamicMap->entityList.begin();
       it != dynamicMap->entityList.end(); ++it)
  {
    h.beginSection("entity " + std::to_string(walkIndex++) + " " + ENTITY_TYPE_NAMES[it->second->getType()]
                   + " (id " + std::to_string(it->first) + ")");
    h.setTracePrefix(ENTITY_TYPE_NAMES[it->second->getType()]);
    it->second->hashState(h);
  }
  h.setTracePrefix("");

  // 9: the per-tile entity lists (the dump prints the non-empty ones itself)
  h.quiet(true);
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::vector<int>& tile = dynamicMap->entityMap[x][y];
      h.count(tile.size(), "entityMap.count");
      for (size_t i = 0; i < tile.size(); i++)
      {
        h.ref(tile[i], "entityMap.entity");
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
      h.count(tile.size(), "aiView.count");
      for (size_t i = 0; i < tile.size(); i++)
      {
        h.ref(tile[i].id, "aiView.entity");
        h.i32(tile[i].playerNumber, "aiView.owner");
        h.i32(tile[i].type, "aiView.type");
        h.i32(tile[i].groupType, "aiView.groupType");
        h.flag(tile[i].staticFlag, "aiView.static");
      }
    }
  }
}

void Simulation::dumpState(std::ostream& out) const
{
  StateHasher h;
  std::ostringstream body;
  h.setDump(&body);
  hashInto(h); // throws if not started

  std::GameSettings* settings = std::GameSettings::getInstance();
  DynamicMap* dynamicMap = m_entityManager->getDynamicMap();
  const int width = settings->getMapWidth();
  const int height = settings->getMapHeight();

  out << "state hash 0x";
  {
    static const char DIGITS[] = "0123456789abcdef";
    const uint64_t v = h.value();
    for (int shift = 60; shift >= 0; shift -= 4)
    {
      out << DIGITS[(v >> shift) & 0xf];
    }
  }
  out << "\n";
  out << "tick " << m_tick << ", round " << (m_roundOver ? "over" : "running") << "\n";
  out << "config: seed " << m_config.seed << ", map " << width << "x" << height << ", blocks "
      << m_config.blockPercentage << "%, spawner " << (m_config.npcSpawnerEnabled ? "on" : "off") << ", players";
  for (int i = 0; i < m_config.numPlayers(); i++)
  {
    out << " " << (i + 1) << ":team" << m_config.slots[i].team << (m_config.slots[i].ai ? "/ai" : "/human");
  }
  out << "\n\n";

  out << body.str();

  out << "\n[map] (# wall, . empty, + block)\n";
  Map* map = Map::getInstance();
  for (int y = 0; y < height; y++)
  {
    out << "  ";
    for (int x = 0; x < width; x++)
    {
      const int tile = map->staticTileAt(x, y);
      out << (tile == Map::WALL ? '#' : (tile == Map::BLOCK ? '+' : '.'));
    }
    out << "\n";
  }

  // The entity-id to walk-index mapping, so a ref in the listing can be followed.
  std::map<int, int> walkOf;
  int index = 0;
  for (std::map<int, std::shared_ptr<GameEntity>>::const_iterator it = dynamicMap->entityList.begin();
       it != dynamicMap->entityList.end(); ++it)
  {
    walkOf[it->first] = index++;
  }
  out << "\n[tile entity lists] (walk indexes, in list order; empty tiles omitted)\n";
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::vector<int>& tile = dynamicMap->entityMap[x][y];
      if (!tile.empty())
      {
        out << "  (" << x << "," << y << "):";
        for (size_t i = 0; i < tile.size(); i++)
        {
          const std::map<int, int>::const_iterator w = walkOf.find(tile[i]);
          out << " " << (w == walkOf.end() ? -1 : w->second);
        }
        out << "\n";
      }
    }
  }
  AIGameView* view = AIGameView::getInstance();
  out << "\n[AI view lists] (walk index/owner/type/group/static; empty tiles omitted)\n";
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      const std::vector<EntityInfo>& tile = view->getEntityInfoAtLocation(x, y);
      if (!tile.empty())
      {
        out << "  (" << x << "," << y << "):";
        for (size_t i = 0; i < tile.size(); i++)
        {
          const std::map<int, int>::const_iterator w = walkOf.find(tile[i].id);
          out << " " << (w == walkOf.end() ? -1 : w->second) << "/" << tile[i].playerNumber << "/" << tile[i].type << "/"
              << tile[i].groupType << "/" << (tile[i].staticFlag ? 1 : 0);
        }
        out << "\n";
      }
    }
  }
}
