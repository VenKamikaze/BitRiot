// HeadlessGame.h
//
// Test harness for driving the real BitRiot simulation with no window, no
// audio device and no menu system.
//
// The game's simulation code is entangled with SDL only through *surfaces*,
// never through a window or a renderer: Map::init() and
// EntityRendererFactory::initSurfaces() just load bitmaps and blit-convert them
// against a "primary" surface. A plain SDL_CreateRGBSurface() satisfies that,
// so tests get the genuine entity/collision/AI code with zero production-side
// refactoring and zero stubbing.
//
// Two things do need supplying:
//   * a MickBaseSound backend, because entity constructors call
//     GameSound::playSound() and MickBaseSound::getInstance() is otherwise null;
//   * a reset of the process-lifetime singletons between tests (see
//     HeadlessGameTest::SetUp).
//
// Usage:
//
//   TEST_F(HeadlessGameTest, PlayersOnTheSameTeamDoNotEndTheRound)
//   {
//     configurePlayers(4);
//     setTeams({0, 0, 1, 1});
//     startRound();
//     ...
//   }

#ifndef BITRIOT_TESTS_HEADLESSGAME_H_
#define BITRIOT_TESTS_HEADLESSGAME_H_

#include <gtest/gtest.h>

#include <SDL2/SDL.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "EntityEnum.h"
#include "EntityManager.h"
#include "MickBaseSound.h"
#include "PlayerCharacterEntity.h"
#include "SpawningPool.h"

namespace bitriot_test
{

// Silent MickBaseSound backend. Registering itself in the constructor mirrors
// what MickSDLSound does, so GameSound::playSound() becomes a no-op instead of
// dereferencing a null instance.
class NullSound : public MickBaseSound
{
  public:
    NullSound();
    void initAudio() override {}
    void playSound(std::string fileName) override;

    // Sounds requested since the last clearSoundLog(). Handy for asserting that
    // an action actually fired (e.g. an explosion) without inspecting entities.
    const std::vector<std::string>& soundLog() const { return m_soundLog; }
    void clearSoundLog() { m_soundLog.clear(); }

  private:
    std::vector<std::string> m_soundLog;
};

// One-time process setup: SDL, the backing surface, static map tiles, entity
// renderer surfaces and the null sound backend. Registered from test_main.cpp.
//
// Asset loading is relative ("data/renderer.txt", "assets/bitmaps/..."), so the
// test binary must run with the repository root as its working directory. The
// CMake add_test() call sets WORKING_DIRECTORY accordingly.
class HeadlessEnvironment : public ::testing::Environment
{
  public:
    void SetUp() override;
    void TearDown() override;

    static SDL_Surface* surface();
    static NullSound* sound();
};

// Base fixture. Resets every process-lifetime singleton the simulation touches
// so tests are order-independent.
class HeadlessGameTest : public ::testing::Test
{
  public:
    // Map dimensions used unless a test calls setMapSize(). Matches the game's
    // own defaults.
    static const int DEFAULT_MAP_WIDTH = 17;
    static const int DEFAULT_MAP_HEIGHT = 13;

  protected:
    void SetUp() override;
    void TearDown() override;

    // -- configuration (call before startRound) -----------------------------

    // Sets the player count and resets every slot to a clean free-for-all
    // default: AI-controlled, its own team, no attached device.
    void configurePlayers(int numPlayers);

    // Assigns 0-based alliance teams per slot, e.g. {0,0,1,1} is two pairs.
    // Size must equal the configured player count.
    void setTeams(std::initializer_list<int> teamsPerSlot);

    // Resizes the map. Must be odd in both dimensions (the wall/pillar layout
    // assumes it) and is applied to Map's statics, GameSettings and the
    // rebuilt static map together.
    void setMapSize(int width, int height);

    // -- round lifecycle ----------------------------------------------------

    // Builds a fresh EntityManager and spawns one PlayerCharacterEntity per
    // configured slot at a distinct tile. Safe to call more than once in a
    // test: the previous round is torn down first, which is exactly the
    // "play again" path that historically leaked AIGameView state.
    void startRound();

    EntityManager& em();

    // Advances the simulation. Each call is one EntityManager::runFrame(),
    // i.e. AI + message queue + collisions - the same work the real game loop
    // does per tick.
    void runFrames(int frames);

    // -- world queries and manipulation -------------------------------------

    std::shared_ptr<PlayerCharacterEntity> player(int playerNumber);

    // Marks a player dead the way the win-condition code observes it. This is
    // deliberately a flag flip rather than simulated damage: tests of win
    // conditions should not depend on how much health an explosion does.
    void killPlayer(int playerNumber);

    // Posts a create message and processes the queue, returning immediately
    // with the entity live on the map. playerNumber 0 is the CPU/hostile
    // owner; 1..N are the player slots.
    void spawnEntity(EntityType type, int playerNumber, int tileX, int tileY,
                     unsigned int flags = 0);

    int countEntities(EntityGroupType group);
    int countEntities(EntityGroupType group, int playerNumber);
    int countAllEntities();

    // Clears every block from the static map. Useful when a test needs
    // predictable movement and does not care about terrain.
    void clearBlocks();

  private:
    void resetSingletons();

    std::unique_ptr<EntityManager> m_entityManager;
    int m_mapWidth = DEFAULT_MAP_WIDTH;
    int m_mapHeight = DEFAULT_MAP_HEIGHT;
    std::streambuf* m_savedCout = nullptr;
};

} // namespace bitriot_test

#endif // BITRIOT_TESTS_HEADLESSGAME_H_
