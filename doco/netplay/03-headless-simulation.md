# 03 — Headless `Simulation` and state hash

- **Repo:** BitRiot
- **Branch:** `netplay/03-simulation`
- **Depends on:** 01, 02
- **Status:** todo

## Goal

Take the round setup and the per-tick step out of `GameEngine` and into a renderer-free `Simulation` that the game, the test harness, and later the netplay layer and a headless binary all drive the same way. Also add a deterministic state hash. When this is done, a harness round **is** a real round, with the real block seeding and real spawns.

## Read first

- [`research/code-facts.md`](research/code-facts.md): §1 (the `runEngine` order), §5 (iteration order and hazards), §10 (harness gaps).
- `src/GameEngine.cpp` (`runEngine`, `resetGame`, `seedBlocksOnMap`, `initPlayerCharacters`, `applyMapDimensionsIfChanged`).
- `tests/support/HeadlessGame.{h,cpp}`. The harness currently duplicates spawn positions and seeds no blocks.
- `src/EntityManager.*`, `src/SpawningPool.*`, `src/InfoPanel.*` (render-only, but it is constructed in `resetGame`).

## In scope

1. **`MatchConfig`:** a plain struct holding everything that must be identical on every peer.
   - The seed (from 02), map width and height, block %, number of players, each slot's team, and each slot's initial controller (human or AI).
   - `npcSpawnerEnabled`, frozen for the match. Today it is read live every tick (code-facts §7).
   - Any other setting the sim reads.
   - Build it from `GameSettings` at round start. The sim reads `MatchConfig`, not `GameSettings`, wherever practical. Make sure no sim code reads a `GameSettings` value that can change mid-round.
2. **The `Simulation` class:**
   - `Simulation(const MatchConfig&)` or `start(const MatchConfig&)`: all of today's `resetGame()` world setup, including block seeding, player spawning and the spawning pool. It does **not** include `InfoPanel`, the renderer or the menu.
   - `step()`: exactly today's per-tick order (pool `update` → `EntityManager::runFrame`, plus the round-end evaluation and the tick-based GameOverTimer from 01).
     - Human input is still applied by `GameEngine` before `step()` for now. Brief 04 moves it into `step(inputs)`. Shape the API so that change is easy, for example `step()` taking an empty or ignored inputs argument already, or a clear seam.
   - Read-only accessors: tick number, round-over state and result, the entity manager or roster.
   - Only one `Simulation` can be alive at a time, because of the singletons (code-facts §2). Assert or document this. Don't try to remove the singletons here.
3. **`GameEngine` uses `Simulation`.** Rendering, InfoPanel, sound, the menu and input stay in `GameEngine`. Behaviour must be unchanged for a player.
4. **The harness uses `Simulation`:**
   - Delete the harness's duplicated spawn and setup code, so harness rounds include real blocks.
   - Expect some existing tests to change behaviour because blocks now exist. Adjust test *setups*, for example by using a block % of 0 where a test needs open ground. Don't weaken assertions.
5. **Bitmaps:**
   - The sim still needs `EntityRendererFactory` surfaces for `getNumFrames` (code-facts §10). Either keep that requirement and document it, or decouple the animation-frame count from the renderer, for example by loading frame counts from data.
   - Prefer decoupling if it's small: the dedicated headless binary in 07/13 benefits.
6. **State hash:** `uint64_t Simulation::stateHash()`.
   - Use a stable hash such as FNV-1a 64 over an order-stable walk of:
     - the tick number;
     - every entity in `entityList` order: its type, its tile, its offsets as raw bit patterns, health, player number, alive flag, and the relevant per-type counters (fuses, timers);
     - static map tiles;
     - spawning-pool state;
     - the sim RNG state.
   - Exclude render-only state (animation frames), and **exclude absolute entity ids**. Use the position in the order instead, so the hash doesn't depend on the id base.
   - Document exactly what goes in. It's the desync detector for everything that follows.
7. **Float guard check:** add `static_assert`s or a configure check that `float` is IEEE-754 binary32 (`std::numeric_limits<float>::is_iec559`).

## Out of scope

- The input refactor (04).
- Replay files (05).
- Removing the singletons.
- Networking.

## Tests

- Harness rounds now go through `Simulation`. The whole existing suite passes, with setups adjusted only where blocks changed the scenario. List every adjusted test with the reason.
- **Hash stability:** same seed and config → an identical hash on every tick across two runs.
- **Hash sensitivity:** changing one entity's health or position, or advancing the RNG, changes the hash.
- **Real round:** a harness round has the block count the config implies (± the existing seeding randomness).
- Run a 200-seed sweep before and after, and explain any change.

## Acceptance criteria

- `GameEngine::runEngine` delegates world setup and stepping to `Simulation`.
- The harness has no duplicated setup.
- `stateHash()` exists, is documented, and is tested.
- Gameplay is unchanged for players: run the game, play a round, and describe a manual check in the Outcome.

## Outcome

*(fill in when done)*
