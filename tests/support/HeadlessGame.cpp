#include "HeadlessGame.h"

#include <cstdlib>
#include <iostream>
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
}

void HeadlessGameTest::TearDown()
{
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
  m_entityManager.reset();

  // Drain anything a previous test posted but never processed.
  EntityMessageQueue* emq = EntityMessageQueue::getInstance();
  while (!emq->empty())
  {
    emq->deleteMessageAtFront();
  }

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
  ASSERT_EQ(width % 2, 1) << "map width must be odd";
  ASSERT_EQ(height % 2, 1) << "map height must be odd";

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
  m_entityManager.reset(new EntityManager());

  std::GameSettings* settings = std::GameSettings::getInstance();
  const int numPlayers = settings->getNumberOfPlayers();
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

void HeadlessGameTest::runFrames(int frames)
{
  for (int i = 0; i < frames; i++)
  {
    em().runFrame();
  }
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
