# 01 — Round lifecycle hygiene

- **Repo:** BitRiot
- **Branch:** `netplay/01-round-lifecycle`
- **Depends on:** none (open question Q1, countdown length, is answered: 2 s / 60 ticks)
- **Status:** done

## Goal

Every round starts from the same clean state and ends on a defined tick. This fixes the small round-lifecycle bugs that would later show up as netplay desyncs or replay mismatches. Each fix is small, and each is also a correctness fix for local play today.

## Read first

- [`README.md`](README.md): decisions and workflow.
- [`research/code-facts.md`](research/code-facts.md): §1 (loop), §3 (IDs, timers), §11 (map-resize bug), §12.
- `tests/README.md`: harness, singleton resets, "Known gaps". This brief closes two of them.
- `src/GameEngine.cpp` (`runEngine`, `resetGame`, `applyMapDimensionsIfChanged`), `src/GameTimer.*`, `src/GameOverTimer.*`, `src/GameScoreBoardTimer.*`, `src/EntityFactory.cpp`, `src/BlockEntity.cpp`, `src/Map.cpp`.

## In scope

1. **GameOverTimer counts ticks, not wall-clock time.**
   - Replace `SDL_GetTicks()` in `GameTimer`/`GameOverTimer` with a count of sim ticks, advanced from the sim loop.
   - Keep the class's role (how long after the end condition first holds the round really ends), so the `GameEngine` changes stay small.
   - **Fix the latch.** `timerTriggered` is never reset. It must be reset per round, ideally owned by or reset in `resetGame()`.
   - **Fix the virtual call in the base constructor.** The base constructor calls `getDefaultTimeout()`, so the subclass's value is never used.
   - **Duration:** 2 s, which is 60 ticks at 33 ms/tick (Q1 answered). Make it a single named constant.
2. **Scoreboard game length in ticks.** Compute it as ticks × `NORMAL_TICKS_SPEED`, not from `SDL_GetTicks()` deltas (GameEngine.cpp ~:138, :157, :169). This lives in `GameEngine`, which the harness can't drive until 03a, so a manual check is acceptable here. Say how you checked.
3. **Reset `EntityFactory::m_idCount` per round.** Do it in `resetGame()` or where the new `EntityManager` is constructed, following the existing message-queue clear in `EntityManager`'s constructor. `InputHandler`'s id-keyed maps are cleared in `setPointers` (code-facts §12), so the reset is safe for them. Check for any other holder of ids across rounds.
4. **Fix the map-resize bug** (code-facts §11).
   - Today `applyMapDimensionsIfChanged()` rebuilds `Map` *before* the old `EntityManager` is destroyed, so the old `BlockEntity` destructors write into the new map with the wrong stride.
   - **Prefer a fix the harness can test.** For example: make `BlockEntity`'s destructor stop writing to `Map` once its round has ended, or have `Map` reject writes from a stale generation. Alternatively, extract the "tear down the old world, then resize" sequence into a function that both `GameEngine` and the harness call.
   - A fix that only reorders lines in `GameEngine` can't be tested headlessly, so avoid that unless you also extract the sequence.
5. **Smaller fixes on the same path:**
   - Initialise all `GameEntity` scalar members in its constructor (`m_health`, `m_velocity`, `m_ID`, `m_type`, …).
   - `Map::~Map` should use `delete[]`.
   - Guard `runCollisions`' `entityList.find(id)->second` against `end()`.
6. **`GameScoreBoardTimer`:** remove it if it is still unused.

## Out of scope

- The RNG (02).
- The `Simulation` extraction (03a). Keep the `GameEngine` changes minimal.
- Changing when the end condition is evaluated, beyond the latch fix.

## Tests

- **GameOverTimer, ticks:** it ends exactly N ticks after the condition first holds, with no wall-clock dependence.
- **GameOverTimer, latch:** round 2 also waits N ticks. This fails today.
- **ID reset:** the first entity of round 2 gets the same id as the first entity of round 1.
- **Map resize:** shrink and grow between rounds with blocks present. Walls stay intact and nothing writes out of bounds. Add an AddressSanitizer run of this test if practical.
- Prove each new test fails without its fix.
- Run the full suite and a 200-seed sweep, before and after.

## Acceptance criteria

- No `SDL_GetTicks()` in round-end or scoreboard-length logic.
- Consecutive rounds use the same id base and the same countdown length.
- Map resize is memory-safe.
- "Known gaps" in `tests/README.md` is updated.
- All tests pass, and the sweep is no worse than before.

## Outcome

**What landed**

1. **GameOverTimer counts ticks.** `GameTimer` now holds a tick clock (`tick()`, `setTimerTriggered()`, `getTimerCompleted()`, `reset()`) and a single `advance(endConditionHolds)` that runs the whole per-tick protocol. `GameEngine` calls `advance()` once per `GAME_RUNNING` tick, so the engine and the tests share the same code. The subclass passes its timeout to the base constructor, so there is no virtual call from the constructor any more. The countdown is `GameOverTimer::GAME_OVER_COUNTDOWN_TICKS = 60` (2 s at 33 ms/tick; Q1 answered). The round ends exactly 60 ticks after the first tick the end condition holds. The latch is fixed by `reset()`, called from `EntityManager`'s constructor (see Deviations).
2. **Scoreboard game length** is `m_gameTicks * NORMAL_TICKS_SPEED / 1000`, via `GameEngine::gameLengthSeconds()`. `m_gameStartedAtTicks` and all `SDL_GetTicks()` use in `GameEngine.cpp` are gone.
3. **Entity ids restart each round**: `EntityFactory::resetIds()`, called from `EntityManager`'s constructor. `InputHandler`'s id-keyed maps are cleared in `setPointers`, and nothing else holds ids across rounds (checked: the only other id-keyed holders are `AIGameView`, which `DynamicMap`'s constructor clears, and the message queue, which is cleared).
4. **Map-resize bug**: `Map::generation()` is bumped by every `init()`. `BlockEntity` records it at construction and its destructor skips the write if the map has since been rebuilt. The harness reproduces the exact order (`setMapSize()` then `startRound()`), so it is covered by tests. `GameEngine` is unchanged for this item.
5. **Smaller fixes**: `GameEntity` constructor initialises `m_type`, `m_groupType`, `m_ID`, `m_playerNumber`, `m_health`, `m_velocity` (the other scalars were already set); `Map::~Map` uses `delete[]`; `runCollisions` skips an id that is on the tile but not in `entityList` (both lookups).
6. **`GameScoreBoardTimer` removed** (unused). `GameOverTimer::getInstance(name, seconds)` was also unused and went with the seconds-based constructor.

**Tests** (`tests/RoundHygieneTest.cpp`, 13 new; suite 125 to 138): countdown constant, exactly-N-ticks, wall-clock independence, three consecutive real rounds each waiting the full countdown, id restart, shrink with blocks on the new walls, shrink with blocks outside the new map, a mixed resize (17x13 to 21x11), grow, a block placed after a resize still cleans up, same-size play-again still clears blocks, `GameEntity` scalar init (constructed over 0xAB-filled memory), stale id in `runCollisions`.

What was shown to fail with its fix reverted, and what was not:

- **Fails without its fix:** the latch test (no `reset()` in `EntityManager`'s constructor), the countdown-constant test (150 ticks), the id test (no `resetIds()`), `ShrinkingTheMapWithBlocksPresent...` and `ResizingWiderButShorterWithBlocksPresent...` (no generation guard: a new wall is overwritten, tiles (3,6) of 9x7 and (4,0) of 21x11), the scalar-init test (reads 0xABABABAB), and the stale-id `runCollisions` test (SIGSEGV, which takes the test binary down).
- **Fails only under AddressSanitizer:** `ShrinkingTheMapNeverWritesOutsideIt`. Its indices are past the array, so without ASan it is silent heap corruption that the test cannot observe.
- **Not expected to fail:** `BlocksStillClearTheirTileAfterAResize` and `SameSizeRoundsKeepClearingBlocksBetweenRounds` guard against the fix being too aggressive. `GrowingTheMapWithBlocksPresentKeepsTheNewMapIntact` is a regression guard only: when both dimensions grow, a stale write lands on an interior tile, so it passes without the guard. The mixed resize is the one that can fail.
- **Cannot compile against the old code:** the `GameTimer` API tests (`GameOverTimerEndsExactlyNTicks...`, `GameOverTimerIgnoresWallClockTime`) use `advance()`/`reset()`/`tick()`, which did not exist. They were not run against the old API.

The 200-seed sweep has 1 failing seed (62) both before and after: `FullRoundTest.ThreeConsecutiveCoopRoundsAllComplete`, the same test as the co-op stalemate recorded in `TODO` (seed 276 there). The failure is identical on `claude` and on this branch, so it is not caused by this work. AddressSanitizer run of the whole suite: clean; with the generation guard reverted it reports `heap-buffer-overflow` in `Map::setStaticTileAt` from `~BlockEntity`; with `delete` instead of `delete[]` it reports `alloc-dealloc-mismatch`.

**Manual check of the scoreboard length and the engine path.** `GameEngine` cannot run headlessly. A scratch copy of the tree (not committed) was built with an env-var hook that starts two all-AI rounds and logs to stderr, run under `xvfb-run` with `SDL_AUDIODRIVER=dummy`. Round 1: countdown 60 ticks (2112 ms wall); 2365 ticks; the scoreboard got 78 s (2365 x 33 / 1000) although the wall clock read 83.8 s, because frames under Xvfb's software renderer took ~35 ms. Round 2: countdown again 60 ticks (2109 ms wall), so the latch fix works in the real engine; scoreboard 96 s for 2938 ticks.

**Deviations from the brief**

- The timer reset lives in `EntityManager`'s constructor rather than `GameEngine::resetGame()`, next to the message-queue clear and the id reset. Reason: it is the one round-start point the harness also goes through, so the latch fix is tested through production code instead of a harness copy. `resetGame()` still resets the engine's own `m_gameTicks`.
- `GameTimer::advance()` was added (not in the brief) so the engine and the tests run identical countdown logic.
- The map fix is the generation guard, as the brief preferred; no reordering in `GameEngine`.
- code-facts §11 said the old `BlockEntity` destructors wrote "using the new map's stride". More precisely they use the old coordinates with the new array and stride. Corrected in code-facts.

**Follow-ups**

- Brief 03a's `start` list says it must reset "`EntityFactory` ids and `GameOverTimer`". Both are now reset by `EntityManager`'s constructor. 03a will need to keep that behaviour when `Simulation` takes over construction.
- `Map::staticTileAt`/`setStaticTileAt` still have no bounds check. The generation guard removes the one known out-of-range writer; any other caller is still unprotected.
- During the countdown the sim still runs, and the round-end tick still runs input, pool and `runFrame` after flipping to `MENU_RUNNING` (code-facts §1, §12). Unchanged; belongs with 03b.
- (Done in the fix round) `~GameTimer` is now protected and `~GameOverTimer` is an empty private destructor, so nothing can delete the singleton; the old recursive destructor is gone.
- **03a: two per-round tick counters.** `GameEngine::m_gameTicks` and `GameTimer::currentTick` both count sim ticks. `Simulation` should own one tick and derive both the scoreboard length and the countdown from it.
- **03a: countdown semantics.** "60 ticks" assumes the end condition keeps holding. `GameTimer::advance()` latches on the first tick it holds; if it stops holding and later holds again, the round ends on that later tick with no fresh countdown. This is the same as the old behaviour and was deliberately left alone (out of scope: "changing when the end condition is evaluated").
- `GameEntity`'s constructor defaults `m_type` to `BLOCK` and `m_groupType` to `BLOCK_GROUP`, only because they are the zero values. A proper "none" sentinel in `EntityType` would be better but touches the enums, so it was left.
- Play-test: none needed. The countdown length changed from 5 s to 2 s on purpose, so the death-animation pause is shorter than players are used to.
