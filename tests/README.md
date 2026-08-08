# BitRiot test harness

Headless tests that drive the **real** game simulation — no window, no audio
device, no menu system, and no stubbed-out game logic.

```
cd build && cmake .. && cmake --build . -j8
ctest --output-on-failure          # or: ./tests/bitriot_tests
```

The tests run in well under a second, so there is no reason not to run them on
every change.

## How it works

BitRiot's simulation touches SDL only through *surfaces* — `Map::init()` and
`EntityRendererFactory::initSurfaces()` load bitmaps and colour-convert them
against a "primary" surface, and nothing in the entity/AI/collision code needs
a window or a renderer. A bare `SDL_CreateRGBSurface()` after `SDL_Init(0)`
satisfies all of it.

That is what makes this harness cheap: **no production code was refactored and
nothing is mocked.** Tests exercise the same `EntityManager::runFrame()` the
game loop calls.

Two things do have to be supplied:

- **A sound backend.** Entity constructors call `GameSound::playSound()`, and
  `MickBaseSound::getInstance()` would otherwise be null. `NullSound` registers
  itself as the backend and records requested sounds (see `soundLog()`) instead
  of playing them.
- **Singleton resets.** See below.

CMake was restructured so this could work: everything except `main.cpp` now
builds into a `bitriot_core` static library, which both `BitRiot` and
`bitriot_tests` link. The game stays on C++14; the test target uses C++17
because GoogleTest ≥ 1.17 requires it.

## Writing a test

Derive from `HeadlessGameTest` (`support/HeadlessGame.h`):

```cpp
TEST_F(MyTest, TeammatesDoNotEndTheRound)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});   // two pairs; 0-based slot teams
  startRound();             // spawns a character per slot

  killPlayer(1);
  killPlayer(3);

  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));
}
```

Useful fixture members:

| Call | Purpose |
| --- | --- |
| `configurePlayers(n)` | Set roster size; resets every slot to AI, own team, no device |
| `setTeams({...})` | 0-based alliance team per slot |
| `setMapSize(w, h)` | Resize map (must be odd); rebuilds `Map` and `AIGameView` |
| `startRound()` | Fresh `EntityManager` + a character per slot. Call twice to test "play again" |
| `runFrames(n)` | `n` × `EntityManager::runFrame()` — AI, message queue, collisions |
| `player(n)` | The `PlayerCharacterEntity` for playerNumber `n` (1-based; 0 is the CPU) |
| `killPlayer(n)` | Flip the alive flag. Win-condition tests should not depend on damage numbers |
| `spawnEntity(type, owner, x, y)` | Create an entity and process the queue |
| `countEntities(group[, owner])` | Count live entities by group, optionally by owner |
| `clearBlocks()` | Strip terrain when a test needs predictable movement |

**Numbering gotcha, worth internalising:** entity `playerNumber` is 1-based and
`0` means the CPU/neutral owner, while `GameSettings` player *slots* are
0-based. `teamNumberForPlayer()` bridges the two, and team `0` is reserved for
the CPU. Getting this backwards is the single easiest mistake to make here.

## Determinism

`SpawningPool` and several entity behaviours call `rand()` directly, so
`test_main.cpp` pins the seed to `1`. To shake out luck-dependent tests:

```bash
BITRIOT_TEST_SEED=42 ./tests/bitriot_tests   # any seed; 0 = time-based
./tests/bitriot_tests --gtest_shuffle        # randomise test order
```

The suite currently passes under shuffling and across several seeds. If you add
a test that only passes on seed 1, it is testing luck, not behaviour.

`std::cout` is swallowed during each test because `MickLogger`'s DEBUG level is
compile-time enabled in Debug builds and is very chatty. Set
`BITRIOT_TEST_VERBOSE=1` to see it. `std::cerr` is left alone, so warnings and
errors (including `DynamicMap::moveEntityOnMap` desync warnings) always show.

## Singleton state — read this before adding tests

The game is built on process-lifetime singletons, and stale state leaking
between rounds is not hypothetical: it is exactly what caused the "spinning
rockets fire at nothing" bug (`AIGameView` was never cleared between games).
`HeadlessGameTest::SetUp()` resets what it can:

| Singleton | Reset |
| --- | --- |
| `AIGameView` | `setMapSize()` clears it |
| `Map` | `init()` rebuilds walls and clears blocks |
| `GameSettings` | Every field restored explicitly — note `setNumberOfPlayers()` deliberately *preserves* existing slots, so it is not a reset |
| `EntityMessageQueue` | Drained |

**Known gaps** — no reset path exists for these. They do not currently affect
any test, but they are real:

- `EntityFactory::m_idCount` only ever increments, so entity ids keep climbing
  across tests (and across games in the real binary). Harmless today because
  nothing assumes an id range.
- `GameOverTimer` / `GameScoreBoardTimer` latch `timerTriggered` with no way to
  clear it. In the shipped game this means the second and later rounds of a
  session skip the game-over countdown: `getTimerCompleted()` compares against
  the *previous* round's `startTime`, which is always long past. Cosmetic, but
  it is a genuine bug — the death animation pause only happens once per launch.
  Fixing it needs a `reset()` on `GameTimer` called from `GameEngine::resetGame()`.

## Coverage today

57 tests across four files:

- `WinConditionTest.cpp` — free-for-all, team and co-op round-end rules,
  including the co-op "board cleared and pool exhausted" win and the
  player-owned-vs-CPU-owned egg distinction that makes it correct.
- `GameSettingsTest.cpp` — roster sizing, alliance team mapping, the 6-player
  map-height gate and the undrivable-human start guard.
- `SpawningPoolTest.cpp` — both ways the pool stops producing hostiles (points
  spent, spawner toggled off) and that power-ups are exempt from the toggle.
- `EntitySimTest.cpp` — round setup, multi-frame simulation on 4- and 6-player
  maps, `AIGameView` add/move/destroy bookkeeping, and ghost-entity regression
  guards across repeated rounds.

Not covered: rendering, input/device assignment, the RmlUi menus and
scoreboard, and specific weapon/AI behaviours. Input and menus would need real
refactoring to test; weapon and AI behaviour is testable with this harness today
and is the obvious next area to extend.
