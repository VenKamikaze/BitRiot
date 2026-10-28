# 01 — Round lifecycle hygiene

- **Repo:** BitRiot
- **Branch:** `netplay/01-round-lifecycle`
- **Depends on:** open question Q1 (countdown length; until answered, keep 5 s)
- **Status:** todo

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
   - **Duration:** keep 5 s (150 ticks at 33 ms/tick) unless Q1 says otherwise. Make it a single named constant.
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

*(fill in when done)*
