# 03a — `Simulation`, `MatchConfig` and state hash

- **Repo:** BitRiot
- **Branch:** `netplay/03a-simulation`
- **Depends on:** 01, 02, 02b
- **Status:** todo

## Goal

Move round setup and the per-tick step out of `GameEngine` into a `Simulation` class. The game, the test harness, and later netplay and a headless binary all drive it the same way. Add a deterministic, fully specified state hash.

When this lands, a harness round goes through exactly the code path a real round does.

Decoupling the simulation from bitmaps and SDL surfaces, and the frame loop, are brief 03b.

## Read first

- [`research/code-facts.md`](research/code-facts.md): §1 (`runEngine` order), §5 (iteration order and hazards), §7 (shared settings), §10 (harness gaps), and §12 (anim state, `GameSettings` reads, the round-end tick).
- `src/GameEngine.cpp`: `runEngine`, `resetGame`, `seedBlocksOnMap`, `initPlayerCharacters`, `applyMapDimensionsIfChanged`.
- `tests/support/HeadlessGame.{h,cpp}`. The harness currently duplicates spawn positions and seeds no blocks.
- `src/EntityManager.*`, `src/SpawningPool.*`, `src/InfoPanel.*`.

## In scope

1. **`MatchConfig`.** A plain struct holding everything that must be identical on every peer:
   - the seed (from 02)
   - map width and height
   - block percentage
   - number of players
   - each slot's team and initial controller (human or AI)
   - `npcSpawnerEnabled`, frozen for the match
   - any other setting the sim reads

   The sim reads `GameSettings` everywhere: teams on every collision, map dimensions, and so on (code-facts §12). Converting every read would be a large change. Instead, **`Simulation::start` applies `MatchConfig` into `GameSettings`**, and the sim keeps reading `GameSettings`. Document this. Netplay (07) will snapshot and restore the local menu settings around a networked match, so expose a way to do that, or at least leave a note.
2. **The `Simulation` class.**
   - `start(const MatchConfig&)` performs today's `resetGame()` world setup: reseed (02), blocks, players and pool. It does not touch `InfoPanel`, the renderer or the menu.
   - **`start` must reset every sim singleton or assert that it is fresh:** `Map` blocks, `AIGameView`, `EntityMessageQueue`, `EntityFactory` ids and `GameOverTimer`.
   - `step()` runs exactly today's per-tick order. **Pin down the round-end semantics** (code-facts §12):
     1. At the top of the tick: the win check and GameOverTimer evaluation.
     2. Then the full step: pool update → `runFrame`.
     3. `roundOver()` becomes true only after that full step.

     Every caller sees the same number of stepped ticks and the same final state. Document this precisely, because netplay compares final hashes.
   - Human input is still applied by `GameEngine` before `step()` for now. Brief 04a changes this to `step(inputs)`, so leave a clear seam for it.
   - Read-only accessors: tick number, round-over flag and result, and the entity manager/roster.
   - **Only one `Simulation` per process.** The singletons make this unavoidable. Assert it.
3. **`GameEngine` uses `Simulation`.** Rendering, InfoPanel, sound, the menu and input stay in `GameEngine`. Behaviour for players must be unchanged.
4. **The harness uses `Simulation`.**
   - Delete the duplicated setup code.
   - To keep this cycle reviewable, **the harness defaults to `blockPercentage = 0`**, so existing tests keep their open-ground scenarios. Real block seeding is covered by new tests (below).
   - Change test setups only where needed, never assertions. List every changed test.
5. **`uint64_t Simulation::stateHash()`.** Use FNV-1a 64, or another algorithm you fully specify, over an order-stable walk that **includes**:
   - the tick number
   - each entity, in `entityList` order:
     - type
     - tile
     - `m_offsetX/Y` and `m_velocity` as raw bit patterns
     - health
     - player number
     - alive flag
     - `m_controlledByAI`
     - **`m_currentAnimState`** (sim state: it drives movement)
     - per-type counters (fuses, timers, AI wait and check timers, goal/instruction state as far as it can be cheaply reached)
   - **per-tile list order** of `entityMap[x][y]` and the AIGameView tile lists. Collisions and AI "first match" depend on it.
   - static map tiles
   - spawning pool state
   - GameOverTimer state
   - sim RNG state

   It **excludes** `m_currentAnimFrame`, `m_animCounter`, the cosmetic RNG and absolute entity ids (use position in the walk instead).

   Document exactly what goes in, as the desync detector everything later relies on. Brief 04a adds the per-slot input state to it.
6. **Integers only.** After 02b the sim has no floats (D6). The hash covers only integer and fixed-point fields, and must not convert anything through `float` or `double`.

## Out of scope

- Decoupling from bitmaps and surfaces, the sim library target, and the frame loop (03b).
- The input refactor (04a).
- Replay (05).
- Removing the singletons.

## Tests

- The existing suite passes through `Simulation`, with setup changes listed.
- **Real rounds:** with the default block percentage on a real-sized map, the round has blocks consistent with the config and plays to a natural end.
- **Hash stability:** the same seed and config give an identical hash on every tick across two runs.
- **Singleton reset:** run round A, then a different round B, then A again. The repeat reproduces A's per-tick hashes. This test must fail if any singleton reset is removed.
- **Hash sensitivity:** changing one entity's health, position or anim state, or advancing the RNG, changes the hash.
- **Round-end semantics:** a test asserting the documented tick count at the end of a round.
- Run a 200-seed sweep before and after, and explain any change.

## Acceptance criteria

- `GameEngine` delegates world setup and stepping to `Simulation`.
- The harness has no duplicated setup.
- `stateHash()` is specified, documented and tested.
- The singleton-reset test passes.
- Gameplay is unchanged for players. This needs a play-test, which may be pending at merge (see README).

## Outcome

*(fill in when done)*
