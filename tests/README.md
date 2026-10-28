# BitRiot test harness

Headless tests that drive the **real** game simulation — no window, no audio
device, no menu system, and no stubbed-out game logic.

```
cd build && cmake .. && cmake --build . -j8
ctest --output-on-failure          # or: ./tests/bitriot_tests
./tests/bitriot_sim_tests          # the simulation tests alone (see below)
```

The tests run in a few seconds, so there is no reason not to run them on every
change.

There are **two test executables** (brief 03b), built from one set of simulation
tests:

| Executable | Links | Contains |
| --- | --- | --- |
| `bitriot_sim_tests` | `bitriot_sim` and GoogleTest only: no SDL, no RmlUi, no OpenGL | every simulation test (round lifecycle, AI, win conditions, fixed-point maths, `Simulation`, the state hash, the guards, `Map`, the sound and rumble seams) |
| `bitriot_tests` | the whole game (`bitriot_core`) | the same simulation tests, plus the ones that need the game: `ScoreboardLayoutTest`, `ScoreboardRandomTest`, `FrameLoopTest`/`TickPacerTest`, `RenderSideTest` |

The simulation tests are compiled once (an object library) and linked into both.
`bitriot_sim_tests` linking at all is a proof that the simulation needs no SDL; it
also plays real rounds, so it proves the simulation *runs* with nothing else in the
process. Run both when you touch the simulation. A test that needs a surface, a
renderer, RmlUi or a device belongs in `CORE_TEST_SOURCES` in `tests/CMakeLists.txt`.

## How it works

BitRiot's simulation touches no SDL at all (since brief 03b it is the
`bitriot_sim` library: no bitmaps, no surfaces, no SDL header; the animation frame
counts are a table, `Map` is only a layout, and sound and rumble go through
interfaces with silent defaults). So the harness needs no window, no renderer and
no surface: the process needs only a recording sound backend and a data directory.

That is what makes this harness cheap: nothing is mocked, and tests exercise
the same code the game loop calls. Since brief 03a that includes round setup and
the per-tick step: a round starts through `Simulation::start(MatchConfig)` and is
stepped by `Simulation::step()`, exactly what `GameEngine` does, so a harness
round **is** a real round (same spawn tiles, same block seeding, same draw order,
same win check and game-over countdown). The harness has no setup code of its own
any more.

The same trick works one level up for the menus: RmlUi resolves layout in
`Context::Update()` and only touches the `RenderInterface` in
`Context::Render()`, so a render interface that draws nothing still yields real
element positions and sizes. That is what `support/RmlLayoutHarness.*` provides,
and it turns "are the scoreboard columns aligned?" into an assertion instead of
a screenshot.

Two things are supplied:

- **A recording sound backend.** Entity constructors call `GameSound::playSound()`.
  With no backend installed that goes to a built-in silent default
  (`MickBaseSound::getInstance()` is never null), but `NullSound` registers itself
  as the backend and records requested sounds (see `soundLog()`), so a test can
  assert that an action made a noise.
- **Singleton resets.** See below.

CMake was restructured so this could work: the simulation builds into a
`bitriot_sim` static library (its sources are the `SIM_SOURCES` list in the
top-level `CMakeLists.txt`), everything that draws, plays, reads a device or shows a
menu builds into `bitriot_core`, which links `bitriot_sim`, and `BitRiot` is just
`main.cpp` plus those. The whole project is pinned to C++17 (`CMAKE_CXX_STANDARD`, extensions
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
| `setMapSize(w, h)` | Sets the size the next `startRound()` uses (GameSettings, the `Map` statics) and rebuilds `Map` and `AIGameView` now. `startRound()` / `Simulation::start` give the Map the config's size themselves since brief 03b, so this is only needed to prepare a Map before the round (a test of what a resize does to the old round) |
| `setBlockPercentage(p)` | Block density of the next `startRound()`. **The harness default is 0**, not the game's 55, so scenario tests keep open ground; use the game's value in tests about real rounds |
| `startRound()` | `Simulation::start()` with a `MatchConfig` built from `GameSettings`: a character per slot at its fixed start tile, the pool, blocks. Call twice to test "play again" |
| `sim()` | The test's `Simulation` (`step()`, `tick()`, `roundOver()`, `winner()`, `stateHash()`, ...). `em()` and `pool()` are its entity manager and spawning pool |
| `runFrames(n)` | `n` bare `EntityManager::runFrame()` calls — AI, message queue, collisions. **No tick, no win check, no countdown, no spawning pool**: the world moves and nothing else does. Use `sim().step()` for real ticks |
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

The harness seeds rounds the way `GameEngine::resetGame()` does: the seed goes
into the round's `MatchConfig` (`GameSettings::pickRoundSeed()`) and
`Simulation::start()` reseeds the sim stream from it before the `SpawningPool`,
the players or the blocks are built (their constructors draw numbers).
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

Do a seed sweep before trusting a new test (and run it for `bitriot_sim_tests` too
when you touched the simulation):

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

## The state hash

`Simulation::stateHash()` is the desync detector (FNV-1a 64 over integers; the
full specification, what it covers and what it leaves out on purpose, is the
comment at the top of `src/Simulation.h`). Tests use it to ask "is this the same
world?":

```cpp
const uint64_t before = sim().stateHash();
// ... change one thing ...
EXPECT_NE(sim().stateHash(), before);
```

- **Same config, same hash on every tick** is the determinism check
  (`SimulationTest.TheSameConfigGivesTheSameHashOnEveryTick`); play a config
  twice with `playHashes()`-style loops and compare vectors.
- **Two kinds of "the hash is still the same", for two kinds of change.**
  - **The schema** (`StateHashTest.TheHashSchemaIsPinned`) pins *which fields* the
    hash writes and in what order. Every `StateHasher` write names its field, the
    simulation can list them (`stateHashFields()`), and the test builds a world by
    hand (one of every entity type, a bot with a plan and pickup memory, queued
    messages) and compares the list with a copy in the test. It does not run the
    simulation, so AI, rules and data changes never move it, and dropping, adding,
    renaming or re-typing a hashed field always does. This is the test that stands
    behind each field.
  - **The golden digests** (`PinnedRoundsKeepTheirHashes`) play four pinned rounds
    to their natural end and compare a digest of every tick's hash. They pin the
    hash *and the way the simulation plays*.
- **When the digests move.** Any change that alters how a seeded round plays moves
  them, whether or not the hash changed: a bot's behaviour, a rule, a number in
  `data/*.txt`, how many random numbers a round draws, a new hashed field (and
  brief 04a's input state). A moved digest is then expected, not a bug, but the
  commit must **say so** ("moves the golden digests: bots now ...") so a reviewer
  can tell it from a determinism fault, and a digest that moves in a commit that
  should not change play is a real regression. The schema list and the
  `STATE_HASH_VERSION` bump are separate and only for changes to the hash itself.
  To regenerate:

  ```bash
  BITRIOT_HASH_DUMP=1   ./tests/bitriot_tests --gtest_filter='SimulationTest.PinnedRounds*'
  BITRIOT_HASH_SCHEMA_DUMP=1 ./tests/bitriot_tests --gtest_filter='StateHashTest.TheHashSchemaIsPinned'
  BITRIOT_HASH_DUMP=all ./tests/bitriot_tests --gtest_filter='SimulationTest.PinnedRounds*'  # every tick's hash
  ```

  The first prints one `digest=0x...ULL` per round (a dump run still compares, so
  it fails until you paste them into `GOLDEN_ROUNDS`); the second prints the schema
  lines for `HASH_SCHEMA`; the third prints `HASH <round> <tick> <hash>` lines,
  which is how two builds (GCC and Clang, or the real engine and the harness) are
  compared tick by tick. The same digests must pass on every compiler. Check a
  change is deliberate before regenerating: a digest that moves for no reason is a
  determinism bug.
- Hashing is read-only and needs no tick, so a test can hash a world it has just
  edited. To change a protected field, use the pointer-to-member trick in
  `SimulationTest.cpp` (`struct Access`).

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
  int converted to a float parameter either; `SimLibraryTest`'s object-level check
  (it disassembles `libbitriot_sim.a` and fails on any floating-point instruction)
  does.

## The simulation library and its guards

`SimLibraryTest.cpp` and `FixedPointGuardTest.cpp` keep `bitriot_sim` what it is
meant to be. Both read **the `bitriot_sim` target's own source list**, which CMake
writes to `bitriot_sim_sources.txt` in the build directory at generate time
(`BITRIOT_SIM_SOURCES_FILE`), so "the simulation" cannot drift from what is built.
The scan covers those sources and every project header they include.

- **No floating point in the text** (`FixedPointSourceGuardTest`): there is no
  allowlist any more. A file that needs a float belongs in `bitriot_core`.
- **No floating point in the machine code** (`TheCompiledSimulationHoldsNoFloatingPointInstruction`):
  `objdump -d` of the archive, checked for SSE/AVX scalar and packed float
  arithmetic, conversions and x87 (x86-64) or the `f*`/`scvtf` family (aarch64).
  It sees what the text scan cannot (`auto` carrying a double, an int converted at a
  float parameter, a float hidden by a macro). It needs binutils' `objdump` and
  fails, not skips, if it is missing or the architecture has no patterns.
- **No SDL, RmlUi or OpenGL in the source** (`TheSimulationSourcesNameNoSdlRmlUiOrOpenGL`):
  no such `#include`, none of the project's own render/device/UI headers, no SDL
  type or function.
- **No SDL library loaded** (`TheSimulationOnlyExecutableHasNoSdlOrUiLibraryLoaded`):
  in `bitriot_sim_tests`, on Linux.
- The seams: sound falls back to a silent default and a destroyed backend
  unregisters; rumble goes through `RumbleSink` (integer strength, discarding default);
  the animation frame counts are `EntityAnim::frameCount()`.

## The render side

`RenderSideTest.cpp` (core executable): the frame counts in `src/EntityAnim.cpp`
agree with every loaded bitmap (all types, all teams, all male/female player
sheets); every frame the simulation can show is drawn and the next is not; drawing
the world twice gives the same picture and leaves `stateHash()` alone; and
`TheWorldIsDrawnPixelForPixelAsItWasBeforeTheSplit` pins the picture of three
rounds at five ticks each (a digest of the blitted world, no text). Those digests
were taken from the code *before* brief 03b, so they show the renderer is
unchanged. Regenerate with
`BITRIOT_PIXEL_DUMP=1 ./tests/bitriot_tests --gtest_filter='RenderSideTest.TheWorldIs*'`
only after checking a deliberate change to a bitmap or to the drawing order.

## The frame loop

`FrameLoopTest.cpp` (core executable) drives `TickPacer` and `FrameLoop` with a fake
clock: no SDL, no real time. It covers N x 33 ms giving N steps, time that is not a
whole tick carrying over, a slow frame catching up, a long gap capped (5 ticks),
`canStep()` holding ticks back without blocking events or drawing and the held time
being paid back only up to the cap, nothing stepping outside a round, a step that
ends the round stopping the frame's other steps, the order (events, steps, one
render), the sleep hint, and a clock that wraps. `GameEngine`'s own part (the hooks)
cannot run headlessly: see the known gaps.

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
leak from the previous round. `Simulation::start()` builds an `EntityManager`
(so everything below is reset for every real and harness round), clears the
`Map`'s blocks, and then **checks that all five singletons are fresh** (queue,
timer, ids, `AIGameView`, `Map` blocks), throwing `std::logic_error` if one is
not, so a reset that someone removes fails loudly:

| State | Reset |
| --- | --- |
| `EntityMessageQueue` | Cleared |
| `GameOverTimer` | `reset()`: un-latched, so every round waits the full countdown. It keeps no clock: `advance(tick, endConditionHolds)` is handed `Simulation::tick()` |
| `EntityFactory` ids | `resetIds()`: every round numbers entities from 1 |

`Map` carries a generation counter, bumped by every `init()`. A `BlockEntity`
destroyed after the map was rebuilt (a map resize between rounds, where `Map` is
rebuilt before the old `EntityManager` goes) skips its write into the new map.
`setMapSize()` followed by `startRound()` reproduces that order.

**Known gaps** - no reset path or coverage exists for these:

- `GameEngine` cannot be driven headlessly (it needs the renderer, `InfoPanel`
  and the menu). Since 03a the world setup and the tick live in `Simulation`,
  which the tests do drive, and since 03b the pacing lives in `FrameLoop`, which
  the tests drive with a fake clock, so what is left unchecked is the glue: that
  `GameEngine::resetGame()` builds its `MatchConfig` from `GameSettings`, hands it to
  `Simulation::start()` and then resizes the window to the Map; that `stepTick()`
  applies input, calls `step()` and notes the round end (or ESC); that
  `renderFrame()` loads the scoreboard on the round's last frame and ends a round
  on ESC when `canStep()` held the tick back; and the scoreboard game length
  (`Simulation::tick()` x `NORMAL_TICKS_SPEED`). These were checked by hand
  (briefs 03a and 03b: a scratch autoplay hook under `xvfb-run`, which compared the
  engine's per-tick state hashes with the harness's, and exercised ESC and a held
  tick).
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
Both were green while the composition — what the game loop actually calls (now
inside `Simulation::step()`) — ended a co-op round on frame one:

```cpp
m_entityManager->oneOrZeroPlayersRemain(m_spawningPool->canSpawnMore())
```

`RoundLifecycleTest.cpp` exists to wire real objects together in exactly the
shape the engine uses. When you add a feature, add at least one test at that
level as well as the unit ones.

## Playing a whole round (and then another)

`runRoundToCompletion(maxFrames)` drives a round with `Simulation::step()` — win
check at the *top* of the tick, then `pool.update()`, then `runFrame()`, exactly
`GameEngine`'s tick order (`stepTick()`) — until `roundOver()`, which is **60 ticks
after the end condition first holds** and after the whole of that tick (see the
round-end semantics at the top of `src/Simulation.h`). It never asserts; it
returns a `RoundOutcome` with `ended`, `frames`, `ticks`, `playersAlive`,
`aliveTeams`, `hostilesRemain`, `poolSpent` and the winner, so a round that fails
to terminate is diagnosable instead of just timing out. `frames` is the number
of ticks before the end condition first held (so `frames > 0` still means "the
round was not over at kickoff"); `ticks` is the total stepped, countdown
included. The player and pool fields describe the state when the round ended.
`describe()` renders it for a failure message.

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

296 tests in `bitriot_tests` (257 of them also in `bitriot_sim_tests`), across the
files below:

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
  ticks (2 s; `GameTimer` is handed the tick, it keeps no clock), ends exactly that many ticks after the end condition first holds,
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
  libm call in any file of the `bitriot_sim` target or a header it includes (no
  allowlist), and that the guard reads the simulation and not the renderer. The scan
  itself is tested too. Shared scanning helpers are in `support/SourceScan.h` and
  `support/SimSources.h`.

- `SimulationTest.cpp` - `Simulation`, `MatchConfig` and the state hash (brief
  03a; suites `SimulationTest`, `SingletonResetTest`, `StateHashTest`,
  `StateHasherTest`): config capture, apply, validation and restore; one
  `Simulation` per process; the world `start()` builds (start tiles, blocks per
  the config, a real round to its natural end); the round-end semantics (the
  60-tick countdown, the whole final tick, the winner fixed at the top of it);
  the same config giving the same hash on every tick; the hash's field list
  pinned apart from how the sim plays; a 4-player local roster surviving a
  2-player host match (`GameSettings::snapshot()`/`restore()`); strict versus
  loose `MatchConfig` validation; round A, round B, round A
  with each singleton left stale in turn; what the hash does and does not
  notice (health, offsets as raw bits, tile-list order, pending messages,
  per-type counters; not `m_currentAnimFrame`, the cosmetic RNG or local
  devices); the FNV-1a vectors; and golden digests of four pinned rounds.

- `SimLibraryTest.cpp` - the simulation library (brief 03b): the SDL/UI source scan,
  the object-level floating-point scan (and the tests of both scanners), the
  sim-only executable having no SDL library loaded, a full round with blocks, sound
  with no backend installed, rumble through `RumbleSink`, and the animation frame
  table (`EntityAnimTest`).
- `MapTest.cpp` - `Map` with no surface: the layout, reads outside it are WALL,
  writes outside it are ignored, the stride follows the built size, `resize()`
  rebuilds only when the size changes.
- `FrameLoopTest.cpp` (core executable) - `TickPacer` and `FrameLoop` on a fake clock.
- `RenderSideTest.cpp` (core executable) - frame counts against the loaded bitmaps,
  drawing is read-only, the world is pixel-for-pixel what it was before 03b.
- `ScoreboardRandomTest.cpp` (core executable) - the scoreboard's win text draws one
  cosmetic value and no sim value (moved out of `SimRandomTest.cpp`, which is built
  into the simulation-only executable and cannot link the UI).

Not covered: rendering (rasterisation beyond the pinned digests), the HUD text, input and device assignment, and
specific weapon behaviours and most AI behaviours. Input would need real refactoring to test;
weapon and AI behaviour is testable with this harness today and is the obvious
next area to extend. The other RmlUi menus (`playerselect.rml`,
`gameoptions.rml`) are now reachable through `RmlLayoutTest` the same way the
scoreboard is — only their data models need wiring up.
