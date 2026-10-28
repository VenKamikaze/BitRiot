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

The same trick works one level up for the menus: RmlUi resolves layout in
`Context::Update()` and only touches the `RenderInterface` in
`Context::Render()`, so a render interface that draws nothing still yields real
element positions and sizes. That is what `support/RmlLayoutHarness.*` provides,
and it turns "are the scoreboard columns aligned?" into an assertion instead of
a screenshot.

Two things do have to be supplied:

- **A sound backend.** Entity constructors call `GameSound::playSound()`, and
  `MickBaseSound::getInstance()` would otherwise be null. `NullSound` registers
  itself as the backend and records requested sounds (see `soundLog()`) instead
  of playing them.
- **Singleton resets.** See below.

CMake was restructured so this could work: everything except `main.cpp` now
builds into a `bitriot_core` static library, which both `BitRiot` and
`bitriot_tests` link. The whole project is pinned to C++17 (`CMAKE_CXX_STANDARD`, extensions
off, in the top-level `CMakeLists.txt`) so every compiler agrees; GoogleTest ≥ 1.17
requires it anyway.

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
| `destroyPlayer(n)` | Real death path: DESTROY_ENTITY, so the player is also erased from the live entity list |
| `setPlayerHealth(n, hp)` | Force a player's health (protected state, no public setter). For "what does a badly hurt bot do?" tests |
| `spawnEntity(type, owner, x, y)` | Create an entity and process the queue |
| `countEntities(group[, owner])` | Count live entities by group, optionally by owner |
| `clearBlocks()` | Strip terrain when a test needs predictable movement |

**Numbering gotcha, worth internalising:** entity `playerNumber` is 1-based and
`0` means the CPU/neutral owner, while `GameSettings` player *slots* are
0-based. `teamNumberForPlayer()` bridges the two, and team `0` is reserved for
the CPU. Getting this backwards is the single easiest mistake to make here.

## Determinism

The simulation draws every random number from `SimRandom::sim()` (`src/SimRandom.h`,
PCG32), reseeded at the start of every round; libc `rand()` is gone from `src/`.
Presentation randomness (damage-effect frame, scoreboard win text) uses
`SimRandom::cosmetic()`, which is seeded from the OS and must never influence
the sim. A test (`SimRandomSourceGuardTest`) fails if `rand`/`srand` or a std
random engine reappears in `src/`.

The harness seeds rounds the way `GameEngine::resetGame()` does: `startRound()`
calls the production `GameSettings::beginRoundSeed()` first, before the
`EntityManager` and `SpawningPool` are built (their constructors draw numbers).
`SetUp()` seeds the sim stream too, so a test that builds entities without
`startRound()` is deterministic as well.

- **Default:** each round gets a seed derived from the base seed, the test's
  own name and how many rounds that test has started. A test therefore plays the
  same rounds whatever order or `--gtest_filter` it runs with, and several rounds
  in one test (a "play again" test) are different rounds.
- **Pin a round:** `setRoundSeed(n)` before `startRound()` pins the next round to
  exactly `n` (one round only). Use it to replay a round or compare two runs.
- **Reproduce a failing round:** when a test fails, the harness prints every
  round seed it used (`[bitriot] round seeds in this test: ...`, the first is
  `SetUp()`'s, then one per `startRound()`). Put `setRoundSeed(<seed>)` before the
  matching `startRound()` to replay exactly that round, whatever the base seed.
- **Base seed:** `BITRIOT_TEST_SEED` (default `1`) changes every test's rounds.

To shake out luck-dependent tests:

```bash
BITRIOT_TEST_SEED=42 ./tests/bitriot_tests   # any seed; 0 = time-based
./tests/bitriot_tests --gtest_shuffle        # randomise test order
```

If you add a test that only passes on seed 1, it is testing luck, not behaviour.
A test that waits for a random event should pump until it happens (with a
generous cap) rather than guess a tick count: `ADisabledPoolStillLetsPowerUpsThrough`
used a fixed 4000 ticks, which misses a 10%-per-attempt power-up ~2.5% of the
time, and failed on those seeds and on whichever test orderings happened to
hand it one.

Never call `std::srand`/`std::rand` in a test: they no longer influence the sim.

Do a seed sweep before trusting a new test:

```bash
for s in $(seq 1 200); do BITRIOT_TEST_SEED=$s ./tests/bitriot_tests >/dev/null 2>&1 || echo "seed $s"; done
```

The sweep is run from `build/` and needs the `BitRiot` target built at least
once: building it creates the `data` and `assets` symlinks the tests read through
(`cmake --build build -j8` builds everything). A tests-only build directory
(`--target bitriot_tests`) has no symlinks, so run its binary from the source root.

`std::cout` is swallowed during each test because `MickLogger`'s DEBUG level is
compile-time enabled in Debug builds and is very chatty. Set
`BITRIOT_TEST_VERBOSE=1` to see it. `std::cerr` is left alone, so warnings and
errors (including `DynamicMap::moveEntityOnMap` desync warnings) always show.

## Fixed-point maths

The simulation uses `fixed_t` (`src/Fixed.h`, Q16.16 in an `int32_t`) for every
offset and velocity, so there is no floating point in sim code and
`FixedPointSourceGuardTest` keeps it that way. In tests:

- Floats are fine in test code (a float reference model is how the conversion was
  checked), but write expectations about the sim as raw integers: 65536 is 1.0,
  and a tile is 2.0 wide (offsets run from -1 to +1).
- Read positions the way the screen does: `entity.pixelX()` / `pixelY()` (tile
  origin plus `fixedToPixels(offset, TILE_WIDTH / 2)`, truncating toward zero).
- `getOffsetX()` and friends return `fixed_t`. The real hazard is the other
  direction: `setOffsetX(0.5f)`, or any float passed to a `fixed_t` parameter,
  compiles and silently truncates to 0. `-Wall` does not flag it; the library is
  built with `-Wfloat-conversion`, which does, so keep the build free of those
  warnings. The source guard cannot see an `auto` that carries a double, or an
  int converted to a float parameter either.

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
| `EntityMessageQueue` | Cleared by `SetUp()`, and by `EntityManager`'s constructor on every `startRound()` |

**Reset on every round.** `EntityManager`'s constructor is the one point every
new round passes through, so it resets the process-lifetime state that must not
leak from the previous round:

| State | Reset |
| --- | --- |
| `EntityMessageQueue` | Cleared |
| `GameOverTimer` | `reset()`: un-latched, so every round waits the full countdown |
| `EntityFactory` ids | `resetIds()`: every round numbers entities from 1 |

`Map` carries a generation counter, bumped by every `init()`. A `BlockEntity`
destroyed after the map was rebuilt (a map resize between rounds, where `Map` is
rebuilt before the old `EntityManager` goes) skips its write into the new map.
`setMapSize()` followed by `startRound()` reproduces that order.

**Known gaps** - no reset path or coverage exists for these:

- `GameEngine` cannot be driven headlessly (it needs the renderer, `InfoPanel`
  and the menu). So `GameEngine::resetGame()`, the scoreboard game length
  (`m_gameTicks`, ticks x `NORMAL_TICKS_SPEED`) and the call to
  `GameOverTimer::advance()` are checked by hand, not by a test. The countdown
  logic itself is `GameTimer::advance()`, which the tests do call.
- That `resetGame()` reseeds the sim RNG as its very first statement is checked
  by hand: the harness's `startRound()` copies the order, and the production
  piece it calls (`GameSettings::beginRoundSeed()`) is tested, but the call in
  `resetGame()` is not.
- There is no automated AddressSanitizer job. To run one by hand, outside the
  source tree:

  ```bash
  cmake -S . -B /tmp/asan-build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
  cmake --build /tmp/asan-build -j4 --target bitriot_tests
  ASAN_OPTIONS=detect_leaks=0 /tmp/asan-build/tests/bitriot_tests
  ```

  `RoundHygieneTest.ShrinkingTheMapNeverWritesOutsideIt` is only meaningful
  under ASan: without the `Map` generation guard it is a silent heap overflow.

## Test at the seams, not just the units

The suite's first real catch came from a test that composed two things each of
which passed its own tests. `WinConditionTest` feeds `oneOrZeroPlayersRemain()`
a literal `true`/`false`; `SpawningPoolTest` checks `canSpawnMore()` on its own.
Both were green while the composition — what `GameEngine` actually calls —
ended a co-op round on frame one:

```cpp
m_pEntityManager->oneOrZeroPlayersRemain(m_pSpawningPool->canSpawnMore())
```

`RoundLifecycleTest.cpp` exists to wire real objects together in exactly the
shape the engine uses. When you add a feature, add at least one test at that
level as well as the unit ones.

## Playing a whole round (and then another)

`runRoundToCompletion(maxFrames)` drives a round to its natural end in exactly
the order `GameEngine::runEngine()` uses — win check at the *top* of the tick,
then `pool.update()`, then `runFrame()`. It never asserts; it returns a
`RoundOutcome` with `ended`, `frames`, `playersAlive`, `aliveTeams`,
`hostilesRemain`, `poolSpent` and the winner, so a round that fails to
terminate is diagnosable instead of just timing out. `describe()` renders it
for a failure message.

Small maps (`useTestMap()` = 7×7, or `setMapSize()` down to 4×4) exist for this:
they force AI players into contact so a round ends in hundreds of frames rather
than tens of thousands. **These sizes are test-only and must not become a menu
option** — `InfoPanel` computes portrait positions as `((i*3)+1)*TILE_HEIGHT`
with no clamp, so on a short map players' HUD blits off the bottom of the
surface, and even dimensions violate the odd-map invariant `Map::init`'s pillar
grid assumes. The harness has no `InfoPanel` and no start-gate, so neither
applies here.

## Proving a test can fail

A green test proves nothing until you've seen it go red for the right reason.
Both of the team-play fixes were verified differentially — the tests were run
against the pre-fix code and confirmed to fail:

- Reverting `assets/menu/scoreboard.{rml,rcss}` to `e28cbf3` fails 8 of the 9
  `ScoreboardLayoutTest` cases (no header row, misaligned columns, overlapping
  cells; only the "document loads" case passes).
- Restoring the old co-op branch (`return allPlayersDead();`) fails all three
  co-op round tests with `{ended=false frames=60000 playersAlive=2
  hostilesRemain=false poolSpent=true}` — the reported bug exactly.
- Making `EntityManager::getRoundPlayerRoster()` return the live set again (the
  "scoreboard only lists survivors" bug) fails 6 tests: 4 of the 9
  `RoundRosterTest` cases, `FullRoundTest.RosterAfterARealRoundListsEveryPlayerWithTheirRealOutcome`
  and `ScoreboardLayoutTest.ScoreboardFromRealRoundListsPlayersWhoDiedAsWellAsSurvivors`.
  The remaining roster tests only check state that is true for survivors too
  (initial roster, reset, type filtering, max players), so they pass either way.

Assets are read at runtime, so the scoreboard check needs no rebuild: swap the
files, run, swap back.

## Coverage today

186 tests across fourteen files:

- `WinConditionTest.cpp` — free-for-all, team and co-op round-end rules,
  including the co-op "board cleared and pool exhausted" win and the
  player-owned-vs-CPU-owned egg distinction that makes it correct.
- `RoundLifecycleTest.cpp` — the `EntityManager` × `SpawningPool` seam as
  `GameEngine` composes it, including the guard that a spawner-off co-op round
  is an enemy-free sandbox rather than an instant win.
- `GameSettingsTest.cpp` — roster sizing, alliance team mapping, the 6-player
  map-height gate and the undrivable-human start guard.
- `SpawningPoolTest.cpp` — both ways the pool stops producing hostiles (points
  spent, spawner toggled off) and that power-ups are exempt from the toggle.
- `EntitySimTest.cpp` — round setup, multi-frame simulation on 4- and 6-player
  maps, `AIGameView` add/move/destroy bookkeeping, ghost-entity regression
  guards across repeated rounds, undelivered `EntityMessageQueue` messages not
  leaking into the next round, and destroying an entity that has a MOVE pending
  or is missing from the map.
- `FullRoundTest.cpp` — whole rounds played to their natural end (PvP, team,
  2- and 4-player co-op), the scoreboard roster after a real round, plus
  consecutive rounds in one process.
- `RoundRosterTest.cpp` — `EntityManager::getRoundPlayerRoster()`: every player
  who played stays listed (with stats and alive flag) after dying and being
  erased from the live entity list, the live-set semantics of
  `getAllPlayerCharacters()` are unchanged, and the roster resets per round.
- `ScoreboardLayoutTest.cpp` — the scoreboard's real computed geometry: column
  headings exist, every data cell aligns with its heading, columns don't
  overlap or wrap, rows group by team, the winning team is marked, and a real
  round with dead players yields one row per player, dead ones included.

- `RoundHygieneTest.cpp` - the round lifecycle: the game-over countdown is 60
  ticks (2 s), ends exactly that many ticks after the end condition first holds,
  ignores wall-clock time and is the same length in every round; entity ids
  restart each round; shrinking or growing the map between rounds with blocks
  present (including a wider-but-shorter one) leaves the new walls intact; `GameEntity` scalars are initialised;
  `runCollisions` tolerates a stale id.

- `AISeekEnergyTest.cpp` — bot energy-seeking on a 9x9 board with one bot (the
  game needs two players, so the second is created and `destroyPlayer`ed): a
  bot below the 15-health lay limit walks to energy drops / powerups, looks
  across the whole board, picks the nearest, skips pickups it cannot reach or
  that sit on an enemy mine, bombs out of a block cage when it can survive its
  own blast (and no egg, mine or bomb beside the bomb tile would chain it), gives up on a pickup that
  keeps going nowhere and forgives it later, and 15+ health pickup choice. An
  `AIProbe` (a bot that is not in the round) lets a test ask for one goal
  decision and read the answer; it also checks the flood fill against A*, and
  that path searches leave no nodes allocated (`livePathNodes()`).

- `SimRandomTest.cpp` - the sim RNG (brief 02): PCG32 known answers (the
  reference implementation's own output for seed 42, plus values from an
  independent implementation), `nextInt` edge cases (0 and 1 return 0 and still
  consume one draw), the round-seed plumbing in `GameSettings` (a pin lasts one
  round), whole rounds (same seed gives the same winner, frame count and
  per-tick entity counts; different seeds differ; cosmetic draws change
  nothing), that a damage effect and the scoreboard win text each draw exactly
  one cosmetic value and no sim value, and a source guard that fails if libc
  `rand`/`srand` or a std random engine appears in `src/`.

- `FixedMathTest.cpp` - fixed-point maths (brief 02b): `src/Fixed.h` rounding
  (positive, negative, exact ties, round-up for the rocket, saturation, zero and negative denominators),
  `fixedToPixels` equal to the old `(int)(offset * 16)` for every offset in range;
  every entity type's velocity at initial, max and low health against raw
  expected values (with the float value and ticks per tile alongside); 240
  movement rows (4 directions, open and blocked, assorted start offsets) from an
  independent integer model of the original algorithm; walls stopping entities
  exactly on the middle of a tile; `nearTileCenter` at the +-velocity boundary for
  players and `SimpleBrain`.
- `FixedPointSimTest.cpp` - characterisation of the sim's maths, written against
  the float code first and kept passing across the conversion: scripted walks
  compared tick by tick, pixel by pixel with a float reference model of the old
  `moveInDirection` kept in the test, self-steering robots/drones/wall eaters/egg
  thieves, the tick and tile each rocket explodes on, a rocket created by message
  and a rocket fired by a real spinner exploding in its target tile, and the A* routes on 48 block layouts (2592
  searches). Where fixed-point differs from float the test says why next to the
  expected value. `BITRIOT_FIXED_DUMP=1` prints the digests for regenerating them.
- `FixedPointGuardTest.cpp` - no `float`, `double`, floating literal, `<cmath>` or
  libm call in any `src/` file that is not on its allowlist (each entry has a
  reason; a new file counts as simulation code until listed). The scan, and the
  line-level exception for the rumble call in `PlayerCharacterEntity.cpp`
  (removed by brief 04b), are tested too. Shared scanning helpers are in
  `support/SourceScan.h`.

Not covered: rendering (rasterisation), input and device assignment, and
specific weapon behaviours and most AI behaviours. Input would need real refactoring to test;
weapon and AI behaviour is testable with this harness today and is the obvious
next area to extend. The other RmlUi menus (`playerselect.rml`,
`gameoptions.rml`) are now reachable through `RmlLayoutTest` the same way the
scoreboard is — only their data models need wiring up.
