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

`SpawningPool` and several entity behaviours call `rand()` directly, so
`test_main.cpp` pins the seed to `1`. To shake out luck-dependent tests:

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

Do a seed sweep before trusting a new test:

```bash
for s in $(seq 1 200); do BITRIOT_TEST_SEED=$s ./tests/bitriot_tests >/dev/null 2>&1 || echo "seed $s"; done
```

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
| `EntityMessageQueue` | Cleared by `SetUp()`, and by `EntityManager`'s constructor on every `startRound()` |

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

119 tests across nine files:

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

- `AISeekEnergyTest.cpp` — bot energy-seeking on a 9x9 board with one bot (the
  game needs two players, so the second is created and `destroyPlayer`ed): a
  bot below the 15-health lay limit walks to energy drops / powerups, looks
  across the whole board, picks the nearest, skips pickups it cannot reach or
  that sit on an enemy mine, bombs out of a block cage when it can survive its
  own blast (and no egg, mine or bomb would chain it), gives up on a pickup that
  keeps going nowhere and forgives it later, and 15+ health pickup choice. An
  `AIProbe` (a bot that is not in the round) lets a test ask for one goal
  decision and read the answer; it also checks the flood fill against A*, and
  that path searches leave no nodes allocated (`livePathNodes()`).

Not covered: rendering (rasterisation), input and device assignment, and
specific weapon behaviours and most AI behaviours. Input would need real refactoring to test;
weapon and AI behaviour is testable with this harness today and is the obvious
next area to extend. The other RmlUi menus (`playerselect.rml`,
`gameoptions.rml`) are now reachable through `RmlLayoutTest` the same way the
scoreboard is — only their data models need wiring up.
