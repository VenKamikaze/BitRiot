#include "HeadlessGame.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "AIGameView.h"
#include "EntityMessageQueue.h"
#include "EntityRendererFactory.h"
#include "GameSettings.h"
#include "Map.h"

namespace bitriot_test
{

namespace
{
SDL_Surface* g_surface = nullptr;
NullSound* g_sound = nullptr;

// Mirrors GameEngine::initPlayerCharacters(): players 1..6 start in fixed
// corners/edges. Kept in sync by hand - if the game's layout changes, the
// StartingPositionsMatchGameEngine test in EntitySimTest.cpp will not catch it,
// but every positional test here will start failing in an obvious way.
void startingTileFor(int playerNumber, int mapWidth, int mapHeight, int& atX, int& atY)
{
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
    default: // 5, 6
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
    default: // 2, 4, 6
      atY = mapHeight - 2;
      break;
  }
}
} // namespace

// ---------------------------------------------------------------------------
// NullSound
// ---------------------------------------------------------------------------

NullSound::NullSound()
{
  setBaseInstance(this);
}

void NullSound::playSound(std::string fileName)
{
  m_soundLog.push_back(fileName);
}

// ---------------------------------------------------------------------------
// HeadlessEnvironment
// ---------------------------------------------------------------------------

void HeadlessEnvironment::SetUp()
{
  // SDL_Init(0) initialises no subsystem at all: surfaces are pure CPU memory
  // and need neither video nor a window. This is what makes the harness usable
  // on a headless CI box.
  if (SDL_Init(0) != 0)
  {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  // Dimensions are irrelevant - this surface is only ever used as a *format*
  // reference for SDL_MapRGB and blit conversion, never rendered to.
  g_surface = SDL_CreateRGBSurface(0, 640, 480, 32, 0, 0, 0, 0);
  if (g_surface == nullptr)
  {
    throw std::runtime_error(std::string("SDL_CreateRGBSurface failed: ") + SDL_GetError());
  }

  // Must exist before any entity is constructed: entity ctors call
  // GameSound::playSound(), which dereferences MickBaseSound::getInstance().
  g_sound = new NullSound();

  Map::getInstance()->init(g_surface);

  // Loads every entity bitmap. Throws if an asset is missing, which is the
  // clearest possible signal that the working directory is wrong.
  EntityRendererFactory::getInstance()->initSurfaces(g_surface);
}

void HeadlessEnvironment::TearDown()
{
  // Deliberately leaks g_sound: the game's singletons (EntityFactory, GameSound)
  // may still hold references and are themselves never torn down. Freeing the
  // surface is likewise skipped - Map and every EntityRenderer hold surfaces
  // derived from it and outlive this call.
  SDL_Quit();
}

SDL_Surface* HeadlessEnvironment::surface()
{
  return g_surface;
}

NullSound* HeadlessEnvironment::sound()
{
  return g_sound;
}

// ---------------------------------------------------------------------------
// HeadlessGameTest
// ---------------------------------------------------------------------------

namespace
{
uint64_t g_baseSeed = 1;

// FNV-1a over the bytes, then a splitmix64 finaliser.
uint64_t mixSeed(uint64_t base, const std::string& name, uint64_t round)
{
  uint64_t h = 1469598103934665603ULL;
  auto feed = [&h](uint8_t byte) { h ^= byte; h *= 1099511628211ULL; };
  for (int i = 0; i < 8; i++) feed((uint8_t) (base >> (8 * i)));
  for (char c : name) feed((uint8_t) c);
  for (int i = 0; i < 8; i++) feed((uint8_t) (round >> (8 * i)));
  h += 0x9E3779B97F4A7C15ULL;
  h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
  h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
  return h ^ (h >> 31);
}
}

void HeadlessEnvironment::setBaseSeed(uint64_t seed) { g_baseSeed = seed; }
uint64_t HeadlessEnvironment::baseSeed() { return g_baseSeed; }

void HeadlessGameTest::beginRoundRng()
{
  // Same production path GameEngine::resetGame() uses. Unless the test pinned a
  // seed, derive one from (base seed, test name, rounds started so far).
  std::GameSettings* settings = std::GameSettings::getInstance();
  if (!settings->isRoundSeedPinned())
  {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string name = info ? std::string(info->test_suite_name()) + "." + info->name() : "";
    settings->setRoundSeed(mixSeed(g_baseSeed, name, m_roundsStarted));
  }
  m_roundsStarted++;
  m_roundSeedsUsed.push_back(settings->beginRoundSeed());
}

void HeadlessGameTest::setRoundSeed(uint64_t seed)
{
  std::GameSettings::getInstance()->setRoundSeed(seed);
}

void HeadlessGameTest::SetUp()
{
  // MickLogger's DEBUG level is compile-time enabled in Debug builds and is
  // extremely chatty. Swallow std::cout for the duration of the test; cerr is
  // left alone so warnings and errors still surface, and GoogleTest's own
  // output uses printf so it is unaffected.
  if (std::getenv("BITRIOT_TEST_VERBOSE") == nullptr)
  {
    m_savedCout = std::cout.rdbuf(nullptr);
  }

  resetSingletons();

  m_roundsStarted = 0;
  m_roundSeedsUsed.clear();
  std::GameSettings::getInstance()->clearRoundSeedPin();
  beginRoundRng();
}

void HeadlessGameTest::TearDown()
{
  // A failing round can be replayed on its own: pin the seed with
  // setRoundSeed(<seed>) before the matching startRound().
  if (HasFailure() && !m_roundSeedsUsed.empty())
  {
    std::cerr << "[bitriot] round seeds in this test (SetUp, then each startRound):";
    for (uint64_t seed : m_roundSeedsUsed)
    {
      std::cerr << " " << seed;
    }
    std::cerr << "\n[bitriot] replay one with setRoundSeed(<seed>) before startRound()" << std::endl;
  }

  m_spawningPool.reset();
  m_entityManager.reset();

  if (m_savedCout != nullptr)
  {
    std::cout.rdbuf(m_savedCout);
    m_savedCout = nullptr;
  }
}

void HeadlessGameTest::resetSingletons()
{
  // Order matters. Tear the world down first so nothing observes a half-reset
  // GameSettings, then restore settings, then rebuild the map-sized singletons.
  m_spawningPool.reset();
  m_entityManager.reset();

  // Drop anything a previous test posted but never processed. EntityManager's
  // constructor does this too (that is what protects a real "play again"), but
  // a test that never calls startRound() would otherwise inherit stale messages.
  EntityMessageQueue::getInstance()->clear();

  std::GameSettings* settings = std::GameSettings::getInstance();
  settings->setGameState(std::GameSettings::GAME_RUNNING);
  settings->setNpcSpawnerEnabled(true);
  settings->setBlockSpawnPercentage(55);

  m_mapWidth = DEFAULT_MAP_WIDTH;
  m_mapHeight = DEFAULT_MAP_HEIGHT;
  settings->setMapWidth(m_mapWidth);
  settings->setMapHeight(m_mapHeight);
  Map::MAP_WIDTH = m_mapWidth;
  Map::MAP_HEIGHT = m_mapHeight;

  // configurePlayers resets every per-slot field; 4 is the game's own default.
  configurePlayers(4);

  // Rebuilds the static wall/pillar layout at the current size and clears any
  // blocks a previous test seeded.
  Map::getInstance()->init(HeadlessEnvironment::surface());

  // Drops EntityInfo left over from a previous test. DynamicMap's constructor
  // does this too, but a test that never calls startRound() would otherwise
  // inherit stale entries.
  AIGameView::getInstance()->setMapSize(m_mapWidth, m_mapHeight);
}

void HeadlessGameTest::configurePlayers(int numPlayers)
{
  std::GameSettings* settings = std::GameSettings::getInstance();
  settings->setNumberOfPlayers(numPlayers);

  // setNumberOfPlayers deliberately *preserves* existing slots so the menu can
  // grow and shrink the roster without losing device assignments. For tests
  // that means it is not a reset, so put every field back explicitly.
  for (int i = 0; i < numPlayers; i++)
  {
    settings->setPlayerAI(i, true);
    settings->setPlayerTeam(i, i); // free-for-all: each player their own team
    settings->setPlayerController(i, -1);
    settings->setPlayerKeyboard(i, -1);
  }
}

void HeadlessGameTest::setTeams(std::initializer_list<int> teamsPerSlot)
{
  std::GameSettings* settings = std::GameSettings::getInstance();
  ASSERT_EQ((int) teamsPerSlot.size(), settings->getNumberOfPlayers())
      << "setTeams() needs one entry per configured player; call configurePlayers() first";

  int slot = 0;
  for (int team : teamsPerSlot)
  {
    settings->setPlayerTeam(slot++, team);
  }
}

void HeadlessGameTest::setMapSize(int width, int height)
{
  ASSERT_GE(width, MIN_TEST_MAP_DIMENSION)
      << "a map narrower than this has no interior once the outer wall is placed";
  ASSERT_GE(height, MIN_TEST_MAP_DIMENSION)
      << "a map shorter than this has no interior once the outer wall is placed";

  m_mapWidth = width;
  m_mapHeight = height;

  std::GameSettings::getInstance()->setMapWidth(width);
  std::GameSettings::getInstance()->setMapHeight(height);
  Map::MAP_WIDTH = width;
  Map::MAP_HEIGHT = height;
  Map::getInstance()->init(HeadlessEnvironment::surface());
  AIGameView::getInstance()->setMapSize(width, height);
}

void HeadlessGameTest::startRound()
{
  // Tearing down the old manager before building the new one reproduces the
  // real "play again" sequence, including the AIGameView clear that
  // DynamicMap's constructor performs.
  m_entityManager.reset();
  m_spawningPool.reset();

  // First thing, as in GameEngine::resetGame(): the SpawningPool and player
  // constructors draw random numbers.
  beginRoundRng();

  m_entityManager.reset(new EntityManager());

  std::GameSettings* settings = std::GameSettings::getInstance();
  const int numPlayers = settings->getNumberOfPlayers();

  // Sized exactly as GameEngine::resetGame() sizes it.
  m_spawningPool.reset(new SpawningPool(numPlayers * 100));

  std::vector<bool>* playerAIs = settings->getPlayerAIs();

  EntityMessageQueue* emq = EntityMessageQueue::getInstance();

  for (int i = 1; i <= numPlayers; i++)
  {
    int atX = 0;
    int atY = 0;
    startingTileFor(i, settings->getMapWidth(), settings->getMapHeight(), atX, atY);

    unsigned int flags = 0;
    if (playerAIs->at(i - 1))
    {
      flags |= GameEntity::AI_CONTROLLED_BOT_FLAG;
    }

    emq->postCreateMessage(PLAYER_CHARACTER, i, atX, atY, flags);
    m_entityManager->processMessageQueue();
  }
}

EntityManager& HeadlessGameTest::em()
{
  if (!m_entityManager)
  {
    ADD_FAILURE() << "em() called before startRound()";
    // Keep the process alive so the failure is reported rather than a segfault.
    m_entityManager.reset(new EntityManager());
  }
  return *m_entityManager;
}

SpawningPool& HeadlessGameTest::pool()
{
  if (!m_spawningPool)
  {
    ADD_FAILURE() << "pool() called before startRound()";
    m_spawningPool.reset(new SpawningPool(0));
  }
  return *m_spawningPool;
}

void HeadlessGameTest::runFrames(int frames)
{
  for (int i = 0; i < frames; i++)
  {
    em().runFrame();
  }
}

RoundOutcome HeadlessGameTest::runRoundToCompletion(int maxFrames)
{
  RoundOutcome out;

  for (int frame = 0; frame < maxFrames; frame++)
  {
    // Top of tick, before the pool updates - same as GameEngine::runEngine().
    if (em().oneOrZeroPlayersRemain(pool().canSpawnMore()))
    {
      out.ended = true;
      break;
    }

    pool().update();
    em().runFrame();
    out.frames++;
  }

  out.playersAlive = em().numPlayersAlive();
  out.aliveTeams = em().numAliveTeams();
  out.hostilesRemain = em().anyHostileNpcsAlive();
  out.poolSpent = !pool().canSpawnMore();

  std::shared_ptr<PlayerCharacterEntity> winner = em().getWinningPlayer(pool().canSpawnMore());
  if (winner)
  {
    out.winningPlayerNumber = winner->getPlayerNumber();
    out.winningTeamNumber = winner->getTeamNumber();
  }

  return out;
}

std::string RoundOutcome::describe() const
{
  std::ostringstream ss;
  ss << "{ended=" << (ended ? "true" : "false")
     << " frames=" << frames
     << " playersAlive=" << playersAlive
     << " aliveTeams=" << aliveTeams
     << " hostilesRemain=" << (hostilesRemain ? "true" : "false")
     << " poolSpent=" << (poolSpent ? "true" : "false")
     << " winningPlayer=" << winningPlayerNumber
     << " winningTeam=" << winningTeamNumber << "}";
  return ss.str();
}

std::shared_ptr<PlayerCharacterEntity> HeadlessGameTest::player(int playerNumber)
{
  return em().getPlayerPointer(playerNumber);
}

void HeadlessGameTest::killPlayer(int playerNumber)
{
  std::shared_ptr<PlayerCharacterEntity> p = player(playerNumber);
  ASSERT_NE(p, nullptr) << "no player character with playerNumber " << playerNumber;
  p->setAlive(false);
}

namespace
{
// GameEntity::m_health is protected. Naming it through a derived class lets us
// take a pointer-to-member that any GameEntity can be accessed with, without
// touching production code.
struct HealthAccess : public PlayerCharacterEntity
{
  static int GameEntity::* pointer() { return &HealthAccess::m_health; }
};
} // namespace

void HeadlessGameTest::setPlayerHealth(int playerNumber, int health)
{
  std::shared_ptr<PlayerCharacterEntity> p = player(playerNumber);
  ASSERT_NE(p, nullptr) << "no player character with playerNumber " << playerNumber;
  (*p).*HealthAccess::pointer() = health;
}

void HeadlessGameTest::destroyPlayer(int playerNumber)
{
  std::shared_ptr<PlayerCharacterEntity> p = player(playerNumber);
  ASSERT_NE(p, nullptr) << "no live player character with playerNumber " << playerNumber;
  EntityMessageQueue::getInstance()->postDestroyMessage(p->getID());
  em().processMessageQueue();
}

void HeadlessGameTest::spawnEntity(EntityType type, int playerNumber, int tileX, int tileY,
                                   unsigned int flags)
{
  EntityMessageQueue::getInstance()->postCreateMessage(type, playerNumber, tileX, tileY, flags);
  em().processMessageQueue();
}

int HeadlessGameTest::countEntities(EntityGroupType group)
{
  return countEntities(group, -1);
}

int HeadlessGameTest::countEntities(EntityGroupType group, int playerNumber)
{
  int count = 0;
  DynamicMap* map = em().getDynamicMap();
  // A local iterator, not DynamicMap::entityIterator - that member is shared
  // with EntityManager's own loops.
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getGroupType() != group)
    {
      continue;
    }
    if (playerNumber >= 0 && it->second->getPlayerNumber() != playerNumber)
    {
      continue;
    }
    count++;
  }
  return count;
}

int HeadlessGameTest::countAllEntities()
{
  return (int) em().getDynamicMap()->entityList.size();
}

void HeadlessGameTest::clearBlocks()
{
  Map::getInstance()->clearBlocks();
}

} // namespace bitriot_test
