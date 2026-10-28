# 01 — Round lifecycle hygiene

- **Repo:** BitRiot
- **Branch:** `netplay/01-round-lifecycle`
- **Depends on:** nothing
- **Status:** todo

## Goal

Make every round start from the same clean state and end on a defined tick. This clears up the small round-lifecycle bugs that would otherwise surface later as netplay desyncs or replay mismatches. Each fix is small, and each is also a correctness fix for local play today.

## Read first

- [`README.md`](README.md): decisions and workflow.
- [`research/code-facts.md`](research/code-facts.md): §1 (loop), §3 (IDs and timers), §11 (map-resize bug).
- `tests/README.md`: the harness, singleton resets, and the "Known gaps" list. This brief closes two of those gaps.
- `src/GameEngine.cpp`: `runEngine`, `resetGame`, `applyMapDimensionsIfChanged`.
- `src/GameTimer.*`, `src/GameOverTimer.*`, `src/GameScoreBoardTimer.*`.
- `src/EntityFactory.cpp`, `src/BlockEntity.cpp`, `src/Map.cpp`.

## In scope

1. **The GameOverTimer counts ticks, not wall-clock time.**
   - Replace `SDL_GetTicks()` in `GameTimer`/`GameOverTimer` with a count of sim ticks, advanced from the sim loop.
   - Keep the class's public role (how long after the end condition first holds the round really ends), so `GameEngine` changes stay small.
   - **Fix the latch.** `timerTriggered` is never reset, so from round 2 onwards the round ends instantly. The timer must be reset per round, ideally owned by or reset in `resetGame()`.
   - **Fix the virtual call in the base constructor.** `getDefaultTimeout()` is called from `GameTimer`'s constructor, so the subclass value is never used. The countdown is 5 s where 2 s (`GAME_OVER_COUNTDOWN`) was intended.
   - **Duration: keep 5 s for now**, as 150 ticks at 33 ms/tick, because that is what players have experienced (open question 1 in the README). Make the length a single named constant so it's easy to change.
2. **Measure the scoreboard's game length in ticks.** Use ticks × `NORMAL_TICKS_SPEED`, not `SDL_GetTicks()` deltas (GameEngine.cpp ~:138, :157, :169), so every peer shows the same number.
3. **Reset `EntityFactory::m_idCount` per round**, in `resetGame()` or wherever the new `EntityManager` is constructed. Look at how the message-queue clear was done in the same constructor (merged in `fix/round-message-leak`) and follow that pattern.
   - Check nothing holds entity ids across rounds. `InputHandler` keys per-player maps by entity id (code-facts §3/§6). Make sure they are rebuilt or cleared on reset.
4. **Fix the map-resize bug** (code-facts §11).
   - Today `applyMapDimensionsIfChanged()` rebuilds `Map` *before* the old `EntityManager` is destroyed. The old `BlockEntity` destructors then write into the new map with the wrong stride.
   - Reorder so the old world is torn down before `Map` is resized, or make `BlockEntity` not write to `Map` once its round is over. Choose the smaller, clearer change and explain it.
5. **Smaller fixes on the same path:**
   - Initialise all `GameEntity` scalar members in its constructor (`m_health`, `m_velocity`, `m_ID`, `m_type`, …). `SimpleMovingEntity.cpp:27` reads an uninitialised `m_health`.
   - `Map::~Map`: use `delete[]` for the array.
   - `runCollisions`: guard the `entityList.find(id)->second` lookups against `end()`.
6. **Remove `GameScoreBoardTimer`** if it is still unused, or leave a note if it turns out to be used.

## Out of scope

- The RNG (brief 02).
- Extracting a `Simulation` class (brief 03). Keep changes in `GameEngine` minimal so 03 can move them.
- Any change to *when* the end condition is evaluated, beyond the latch fix.

## Tests

Use the headless harness.

- **GameOverTimer, ticks:** it ends the round exactly N ticks after the condition first holds, with no wall-clock dependence. Run it with a fake or no SDL clock if needed.
- **GameOverTimer, latch:** round 2 also waits N ticks. This test fails today.
- **ID reset:** after a reset, the first entity of round 2 gets the same id as the first entity of round 1.
- **Map resize:** shrink and grow the map between rounds with blocks present. Walls stay intact and nothing writes out of bounds. Add an AddressSanitizer run (`-fsanitize=address`) of this test if practical, and report it.
- Prove each new test fails without its fix.
- Run a full suite plus a 200-seed sweep before and after.

## Acceptance criteria

- No `SDL_GetTicks()` remains in round-end or scoreboard-length logic.
- Consecutive rounds behave identically: the same id base and the same end-countdown length.
- Map resize between rounds is memory-safe.
- `tests/README.md` "Known gaps" is updated, with the closed gaps removed.
- All tests pass, and the sweep is no worse than before.

## Outcome

*(fill in when done)*
