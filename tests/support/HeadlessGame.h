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

// Result of driving a round to its natural conclusion. Every field is filled in
// whether or not the round ended, so a round that runs out of frame budget is
// diagnosable from the outcome rather than just timing out.
struct RoundOutcome
{
  bool ended = false;          // did the win condition fire within the budget?
  int frames = 0;              // frames simulated
  int playersAlive = 0;
  int aliveTeams = 0;
  bool hostilesRemain = false; // CPU-owned eggs/monsters still on the board
  bool poolSpent = false;      // spawning pool out of points
  int winningPlayerNumber = 0; // 0 = draw / nobody left
  int winningTeamNumber = 0;   // 0 = draw

  std::string describe() const;
};

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
    static constexpr int DEFAULT_MAP_WIDTH = 17;
    static constexpr int DEFAULT_MAP_HEIGHT = 13;

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

    // Resizes the map, applying it to Map's statics, GameSettings and the
    // rebuilt static map together.
    //
    // The *game* only ever uses odd dimensions (Map::init's pillar grid and
    // GameSettings::MAP_SIZE_STEP assume it), and menu-reachable sizes are
    // bounded by MIN_MAP_WIDTH/HEIGHT. Tests are not bound by either: there is
    // no InfoPanel here to blit player portraits off the bottom of a short map,
    // and no start-gate to satisfy. Small maps are the point - they force AI
    // players into contact quickly so a round reaches its natural end in a
    // usable number of frames.
    //
    // MIN_TEST_MAP_DIMENSION is the smallest that leaves a playable interior
    // once the outer wall is placed.
    // constexpr, not const: gtest's ASSERT_GE binds its arguments by reference,
    // which ODR-uses the constant and would need an out-of-line definition.
    static constexpr int MIN_TEST_MAP_DIMENSION = 4;
    void setMapSize(int width, int height);

    // A small map that still gives both players room to move and meet. Used by
    // the round-completion tests.
    void useTestMap() { setMapSize(7, 7); }

    // -- round lifecycle ----------------------------------------------------

    // Builds a fresh EntityManager and spawns one PlayerCharacterEntity per
    // configured slot at a distinct tile. Safe to call more than once in a
    // test: the previous round is torn down first, which is exactly the
    // "play again" path that historically leaked AIGameView state.
    void startRound();

    EntityManager& em();

    // The round's spawning pool, sized as GameEngine::resetGame() sizes it
    // (numPlayers * 100). Created by startRound().
    SpawningPool& pool();

    // Advances the simulation. Each call is one EntityManager::runFrame(),
    // i.e. AI + message queue + collisions - the same work the real game loop
    // does per tick.
    void runFrames(int frames);

    // Drives the round until the win condition fires or the frame budget runs
    // out, in exactly the order GameEngine::runEngine() uses for GAME_RUNNING:
    //
    //     check oneOrZeroPlayersRemain(pool.canSpawnMore())   <- top of tick
    //     pool.update()
    //     entityManager.runFrame()
    //
    // The check being at the *top* matters: it is what makes an "already over
    // at kickoff" bug observable on frame zero.
    //
    // Does not assert. Inspect the returned RoundOutcome so a round that fails
    // to terminate is diagnosable rather than a bare timeout.
    RoundOutcome runRoundToCompletion(int maxFrames);

    // -- world queries and manipulation -------------------------------------

    std::shared_ptr<PlayerCharacterEntity> player(int playerNumber);

    // Marks a player dead the way the win-condition code observes it. This is
    // deliberately a flag flip rather than simulated damage: tests of win
    // conditions should not depend on how much health an explosion does.
    void killPlayer(int playerNumber);

    // Kills a player the way the game does: posts DESTROY_ENTITY and processes
    // the queue, so the character is flagged dead *and* erased from the live
    // entity list (unlike killPlayer, which only flips the flag). Use this for
    // anything that cares about what happens to a dead player's entity.
    void destroyPlayer(int playerNumber);

    // Sets a player's health directly. Health is protected state on
    // GameEntity with no public setter, and AI behaviour is full of health
    // thresholds, so tests of "what does a badly hurt bot do?" need this.
    // Does not kill the player if given 0 - use destroyPlayer() for that.
    void setPlayerHealth(int playerNumber, int health);

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
    std::unique_ptr<SpawningPool> m_spawningPool;
    int m_mapWidth = DEFAULT_MAP_WIDTH;
    int m_mapHeight = DEFAULT_MAP_HEIGHT;
    std::streambuf* m_savedCout = nullptr;
};

} // namespace bitriot_test

#endif // BITRIOT_TESTS_HEADLESSGAME_H_
