# Code facts for the netplay / WASM work

A fact check of the current code, made for the briefs in `doco/netplay/`. File:line refs are against `claude` at 996c1ba (Oct 2026). Since then `fix/round-message-leak` has been merged, which shifts lines in `src/EntityManager.*` and `src/EntityMessageQueue.*`, so re-find anything in those two files by name. Re-check any line ref before relying on it.

*Brief 03a (Oct 2026) moved round setup and the tick out of `GameEngine` into `Simulation` (`src/Simulation.*`); §1, §2, §3, §7, §10 and §12 are updated for it, and the `GameEngine.cpp` line refs below that predate it (marked) no longer exist.*

*Brief 03b (Oct 2026) split the simulation into its own CMake library, `bitriot_sim`, with no SDL at all, and replaced the busy-wait loop with `FrameLoop` (`src/FrameLoop.*`, `src/TickPacer.*`) and a split `GameEngine` (`pumpEvents` / `stepTick` / `renderFrame`). §1, §2, §4, §9, §10 and §12 are updated; refs marked "(before 03b)" no longer exist.*

*Brief 04b (Oct 2026) made who controls a slot an explicit `ControlEvent` that `Simulation::step` applies at the start of a tick, replaced the sim's rumble call with `Simulation::hitThisTick(slot)` (the local layer rumbles), and made `InfoPanel` a read-only view of the sim's per-slot selection. §1, §4, §6, §7, §10 and §12 are updated.*

*Brief 09 (Oct 2026) ported the game to Emscripten: both `#error` guards, `find_package(OpenGL)` and the RmlUi glue's clipping bugs are gone, `main()`'s loop body and teardown are functions the browser's main loop also calls, and `MickSDLRenderer` throws by value. §1, §9, §11 and §12 are updated.*

*Brief 04a (Oct 2026) split input into a local sampler and a deterministic apply: `Simulation::step(const TickInputs&)` moves players, and the per-slot input state lives in the sim and in the state hash (version 2). §1, §5, §6, §10, §11 and §12 are updated; refs marked "(before 04a)" no longer exist.*

*Brief 05 (Oct 2026) added the match record (`src/MatchRecord.*`), the replayer (`src/MatchReplay.*`), `Simulation::dataHash()` and eager loading of the data files (`src/DataHash.*`), a build id generated at build time (`src/BuildId.h`, `cmake/GenerateBuildId.cmake`), `Simulation::dumpState()`, the `bitriot_headless` binary (`src/headless/`) and `--record <dir>` with `getopt_long` (`src/GameOptions.*`). §7, §10 and §12 are updated; the format is specified in [`match-record-format.md`](../match-record-format.md).*

*Brief 05b (Oct 2026) added the cross-platform determinism gate (`scripts/determinism-gate.sh`), `scripts/setup-emsdk.sh` and `bitriot_headless replay --perturb-at`; no simulation source changed. §12 has the Emscripten facts and the result for libstdc++ vs libc++.*

An earlier design chat analysed the April-2022 code. Where this file disagrees with that chat, this file wins.

**Corrections to the earlier analysis**

- **Wrong:** the `#error` guard tests `SDL_VIDEO_OPENGL`, not `SDL_VIDEO_RENDER_OGL`.
- **Wrong:** the RmlUi glue uses **no** OpenGL; it renders through `SDL_Renderer`.
- **Outdated:** `attachNewControllerToPlayer()` is gone, replaced by the "Phase 2.7" join selector.
- **Imprecise:** `consoleMain()` is a 33 ms frame cap, not a fixed-timestep accumulator.
- **Overstated:** float risk. The sim has no multiply-adds, so there is nothing for FMA contraction to fuse.

**What it missed**

- per-player sim state living in the input layer
- extra wall-clock and RNG-stream leaks
- GameOverTimer bugs
- exceptions used as control flow
- a map-resize memory bug

## 1. Game loop

*(Rewritten by brief 03b. Before it: `main()` locked the surface, called `consoleMain()` (which ran `GameEngine::runEngine()` once and busy-waited in `SDL_Delay(1)` until 33 ms had passed, so a slow frame slowed the game and there was no catch-up), then `SDL_RenderPresent`.)*

- **`main()`'s loop** (main.cpp): lock the surface, call `frame()`, `SDL_RenderPresent`, unlock, then `SDL_Delay(frameLoop->sleepHintMs())`. `frame()` is a plain function (`FrameResult frame()`), which a web build can hand to `emscripten_set_main_loop`; it never blocks. `FAST_TICKS_SPEED` (GameSettings.h) is unused.
- **`FrameLoop::frame()`** (`src/FrameLoop.h` has the full contract), in order:
  1. `pumpEvents()`: read device and window events (during a round only);
  2. `TickPacer::advance(now)`: add real time to a backlog, capped at 5 ticks' worth (a stall is forgiven, not fast-forwarded); outside a round the pacer is restarted every frame, so time spent in a menu is never owed;
  3. while a whole tick (`GameSettings::NORMAL_TICKS_SPEED` = 33 ms) is owed, up to 5 per frame, and `simulating()` and `canStep()` allow it: `step()`. `canStep()` is the netplay veto (absent locally = always yes); `simulating()` is re-checked after every step so a step that ends the round stops the frame;
  3b. optionally `extraTicks()`: up to 2 more steps beyond the clock's (netplay catch-up), under the same two gates, not touching the backlog;
  4. `render()` once. Rendering never steps, and a frame owing three ticks runs three steps and draws once. A frame that started outside a round and ended inside one (`GAME_INIT`) restarts the pacer after `render()`, so round set-up time is not owed as ticks.
  The clock is SDL's performance counter in ms (`clockMs()` in main.cpp), not `SDL_GetTicks()`: on Linux `SDL_GetTicks()` can step 1 ms at second boundaries, which made one frame in thirty arrive a millisecond early with no tick due.
- **`GameEngine`** exposes the pieces as `frameHooks()`; `runEngine()` is gone. They switch on the global `GameSettings::gameState`:
  - `MENU_RUNNING`: `renderFrame()` calls `menuSystem->showMenu()`. This pumps SDL events itself and calls RmlUi Update/Render (MenuSDLRenderer.cpp). Not simulating, so no tick.
  - `GAME_INIT`: `renderFrame()` clears the scoreboard and calls `resetGame()`. No tick runs. `resetGame()` builds a `MatchConfig` from `GameSettings` with `pickRoundSeed()`, calls `Simulation::start(config)` (which builds the Map at the config's size), then `resizeWindowIfMapChanged()` (window, back buffer and texture, and `MapRenderer::init` against the new surface), then the presentation side: the `InfoPanel` (given the sim to read), `resetInputEvents`, each player's team sprite, `InputHandler::setPointers` (which seeds the slot-to-pad mapping from the menu's choice in `GameSettings`). The tick counter is `Simulation::tick()`.
  - `GAME_RUNNING`:
    - `pumpEvents()` (frame start): `InputHandler::pumpEvents()` polls SDL (`GameRunningSDLInput::updateEventQueue`).
    - `stepTick()` (zero or more per frame): reads the local quit flag (`InputHandler::quitRequested()`; ESC is a flag, not an exception any more), samples the devices (`InputHandler::sampleTick()` returns a `TickInputs`, one `PlayerInput` byte per human slot) and calls `Simulation::step(inputs)` (tick++, the tick's control events applied, then the inputs applied to the players, then the win check and `GameOverTimer::advance(tick, ...)`, then `SpawningPool::update()`, then `EntityManager::runFrame()`; see Simulation.h and TickInputs.h), then buzzes the pad of every slot the sim says was hit (`InputHandler::rumbleForHits`). The `InfoPanel` reads each slot's selection from the sim itself (`Simulation::heldSelection`) when it draws. If `roundOver()` (or ESC was seen) it sets `m_roundEnding` and the loop stops stepping. *(Before 04a: `InputHandler::applyInput()` moved the players directly and threw `InputException(GOT_QUIT)` on ESC. Before 04b: `presentSelections()` pushed the selections to the panel, and a hit rumbled from `onCollision`.)*
    - `renderFrame()` draws the world and HUD onto the CPU surface only if a tick ran, the round is ending, an event was read this frame or the surface is new, and always pushes the surface to the texture (the renderer's back buffers alternate); ESC seen on a frame that ran no tick (held by `canStep()`) ends the round too. If a round is ending, state goes to `MENU_RUNNING` and the scoreboard loads (the state after the whole last step); then the world (`WorldRenderer::renderWorld`) and the HUD are drawn and `pushCpuBufferToHardwareBuffer()` runs. The menu takes over on the next frame, as it always did.
  - `GAME_QUIT`: `renderFrame()` returns false.
- **The round is over only after the whole step has run**: pool and `runFrame` run on the tick it ends. The scoreboard therefore sees the state after that final step (the winner itself is fixed at the top of the tick). Nothing steps after that: the old "round-end tick still runs input after flipping to `MENU_RUNNING`" (brief 01's follow-up) is moot. **During the countdown the sim still runs** (intentional: AI takeover is allowed), and ESC still lets its tick finish.
- **Exceptions.**
  - ESC no longer throws (brief 04a): it is the flag `InputHandler::quitRequested()` (`GameRunningSDLInput::doQuit()`), checked by `GameEngine::stepTick` and `renderFrame`. `InputException.h` is gone.
  - There are still 17 `throw` sites in src (the renderer-init failures, the menu, the sound and asset loaders, and `Simulation`'s `logic_error`/`invalid_argument`/`out_of_range` checks). WASM builds therefore need `-fexceptions` or `-fwasm-exceptions`.
- **The WASM main loop (brief 09, done).** `main()`'s loop body is `loopIteration()` (lock, `frame()`, present, unlock) and the un-init section is `shutdownGame()`; the native `while` calls them and sleeps `sleepHintMs()`, and on Emscripten `emscripten_set_main_loop(webLoopIteration, 0, 1)` calls `loopIteration()` once per animation frame (no sleep: the browser paces, and `frame()` runs however many whole ticks are owed). `webLoopIteration()` catches anything the frame throws, and on quit or error calls `emscripten_cancel_main_loop()`, `shutdownGame()` and tells the page. `frame()` itself is unchanged.

## 2. Randomness and other nondeterminism

**Seeding.** *Done by brief 02.* The sim draws from `SimRandom::sim()` (`src/SimRandom.h`, PCG32), reseeded by `Simulation::start()` from `MatchConfig::seed` before anything that draws is built (`GameEngine::resetGame()` picks the seed with `GameSettings::pickRoundSeed()`, which takes a pin or draws a fresh one). Presentation randomness uses `SimRandom::cosmetic()`. There is no `srand`/`rand` left in `src/`, and `SimRandomSourceGuardTest` keeps it that way. (Before: `srand(SDL_GetTicks())` once at window init in MickSDLRenderer.cpp, and the tests called `srand(seed)` once per process.)

**RNG call sites** (re-found by grep for brief 02; 28 `rand()` sites, 26 sim and 2 cosmetic, all formerly on the single libc stream; the cosmetic ones, EffectEntity and ScoreBoardBinder, consumed the sim stream too).

| File | Controls | Stream |
|---|---|---|
| SpawningPool.cpp | spawn delay (ctor and `update`), type rolls (2), tile x/y (2), robot flags (2) | sim |
| Simulation.cpp (`seedBlocks`, was `GameEngine::seedBlocksOnMap`) | block seeding per tile (a draw for every eligible tile, whatever the percentage) | sim |
| PlayerCharacterEntity.cpp | `m_checkTimer` 37/67 (ctor and `update`, also for humans), initial wait, initial EGG2 goal, `determineGoals` (7 draws), energy goal, per-create wait | sim |
| RobotEntity.cpp | action counter (ctor and `update`) | sim |
| SimpleBrain.cpp | initial heading; heading roll inside a `while` loop, so the number of draws depends on data | sim |
| EffectEntity.cpp | damage-effect anim frame (`m_currentAnimFrame`, render-only; it never affects timing, which runs off `m_health` and `m_animCounter`) | cosmetic |
| ScoreBoardBinder.cpp | `randomWinText` | cosmetic |

- No full-expression contains two draws, so there is no evaluation-order hazard (the rule is in the SimRandom.h header comment).
- `% 0` is now defined: `nextInt(0)` and `nextInt(1)` return 0.
- RmlUi's `Math::Random*` functions exist but are unused.
- **For the state hash (03a, done):** a `DAMAGE_EFFECT` entity's `m_currentAnimFrame` comes from the cosmetic stream and differs per machine, so it is not hashed (nor is any other `m_currentAnimFrame`).

**Wall clock**

- ~~GameTimer.h:25 and :35 (the GameOverTimer).~~ ~~GameEngine.cpp:138, :157 and :169: scoreboard game length.~~ Both fixed by brief 01: they count sim ticks (game length = ticks x `NORMAL_TICKS_SPEED`).
- RmlUi `GetElapsedTime` (SystemInterfaceSDL2.cpp:409): UI only.

**Containers.** There are no unordered containers.

- Value-keyed containers:
  - `set<coord>` (PlayerCharacterEntity.cpp:862, :935)
  - `map<coord,Node*>` (:1056)
  - the A* open list, `map<float,Node*>` (:1057), with `+0.001f` collision nudges (:1230, :1259)
  - `map<int,…>` in InputHandler
- Nothing is keyed by pointer. The only sort (ScoreBoardBinder.cpp:102) is by team and player number.

**Undefined behaviour and bounds**

- GameEntity's constructor leaves `m_health`, `m_velocity`, `m_ID`, `m_type` and others unset. SimpleMovingEntity.cpp:27 reads the uninitialised `m_health`.
- ~~`Map::staticTileAt` has no bounds check.~~ Fixed by brief 03b: a read outside the built map is WALL and a write outside it is ignored, and the row stride is the built height, not the `MAP_HEIGHT` static.
- `runCollisions` does `entityList.find(id)->second` with no end check.

**Singletons with sim state:** `Map`, `AIGameView`, `EntityFactory`, `EntityMessageQueue`, `GameSettings`, `GameOverTimer`, `DataReader`. Because of these, **only one simulation can exist per process.**

## 3. IDs and timers

**`EntityFactory::m_idCount`**

- Set to 1 only in the constructor (EntityFactory.cpp:88-91), incremented at :9-10, never reset per round. *(Brief 01: `EntityFactory::resetIds()`, called from `EntityManager`'s constructor, now resets it every round.)*
- IDs are only compared for equality, and `std::map` order follows creation order. So a different base changes checksums, not behaviour. *(03a: the state hash never contains an absolute id; references between entities are written as walk positions. `Simulation::start()` checks `EntityFactory::nextId() == 1` after building the `EntityManager`.)*
- InputHandler keys its per-player maps by entity id (InputHandler.cpp:66, :498).

**GameOverTimer: wall-clock, with three bugs** *(1, 2 and the wall-clock part fixed by brief 01: the timer now counts ticks, is reset by `EntityManager`'s constructor, and the countdown is `GameOverTimer::GAME_OVER_COUNTDOWN_TICKS = 60`, i.e. 2 s. Item 3 stands.)*

1. It latches: `timerTriggered` is never reset, so from round 2 onwards the round ends on the tick the end condition first holds.
2. The base constructor calls the virtual `getDefaultTimeout()` (GameTimer.cpp:20). The countdown is therefore 5 s (`STANDARD_COUNTDOWN`), not the intended `GAME_OVER_COUNTDOWN=2`.
3. During the countdown the sim keeps running, and AI takeover is still allowed.

`GameScoreBoardTimer` was defined but never used; brief 01 removed it.

*(03a: `GameTimer` keeps no clock of its own any more. `advance(tick, endConditionHolds)` and `getTimerCompleted(tick)` are handed `Simulation::tick()`, so the scoreboard length and the countdown derive from the one counter. `getTriggeredAtTick()` exposes the latch for the state hash.)*

**All other sim timing is integer tick counters.** Bomb and mine fuses, egg hatch, power-up decay, effect and explosion health, rocket lifespan, robot actions, the spawner delay, AI WAIT/`checkTimer`, and SimpleBrain `m_minMoveTimer`.

## 4. Floats

> **Done by brief 02b (Oct 2026):** the simulation uses no floating point. This section describes what landed; the old float analysis is kept at the end for the record. See 02b's Outcome for the velocity table, the behaviour changes and the remaining non-sim floats.

**Representation** (`src/Fixed.h`)

- `fixed_t` is an `int32_t` in Q16.16 (`FIXED_ONE` = 65536). It is named `fixed_t`, not `fixed`, because `using namespace std;` is in force in several headers and `std::fixed` (the iostream manipulator) makes `fixed` ambiguous.
- An entity's offset from the middle of its tile still runs from -1 to +1 (a tile is 2.0 wide), and velocities are in the same units per tick.
- Helpers, with their rounding rules in the header comment: `fixedFromInt`, `fixedFromRatio` (int32 arguments; nearest, exact ties away from zero, symmetric for negatives, a non-positive denominator gives 0, saturates), `fixedFromRatioCeil` (rounds toward +infinity; the rocket's velocity), `fixedAbs`, `fixedToPixels` (truncates toward zero, like the old `(int)` cast; `>>` would floor).

**Where fixed values live**

- `GameEntity::m_offsetX/Y` and `m_velocity` are `fixed_t`; so are the `getOffsetX/Y`/`getVelocity` accessors, the setters, `SimpleBrain`'s references, and the offset parameters of `EntityMessage`, `EntityMessageQueue::postCreateMessage`, `EntityFactory::getEntity`, `EffectEntity` and `SimpleMovingEntity`.
- Velocities are `fixedFromRatio(health + k, modifier)`: player `k=10`, robot/drone/wall eater/egg thief `k=2`, rocket `10 / ROCKET_VELOCITY` rounded **up** (`fixedFromRatioCeil`, 26215 not 26214, which keeps the float code's rocket timing exactly). They are recomputed from health every tick for players and monsters.
- `GameEntity::moveInDirection` keeps its branch structure; it only adds, subtracts and compares.
- The rocket's target tile travels as two plain ints (`targetX`, `targetY`) through `postCreateMessage` / `EntityMessage` / `EntityFactory` into `RocketFiredEntity`; it no longer rides in the float offset parameters.
- `RocketFiredEntity`'s arrival checks called the **float** `std::abs` (checked by a `static_assert` on the old code), so nothing was being truncated; they are now `fixedAbs`.
- The A* open list is `map<pair<int,int>, Node*>` keyed by `(cost + heuristic, tie-break)`. The tie-break is the next free number counting up from 0, which is how the float key took the next free `+0.001f`; it is **not** insertion order (see below).

**Things the old float code did that matter**

- `abs(m_offsetX) <= m_velocity` for rockets, and `nearTileCenter` for players and `SimpleBrain`, compare an offset with exactly one step. For many velocities (a robot's 4/19, a rocket's 0.4) the offset lands on exactly +-velocity on some ticks, and the float answer was decided by rounding noise. The fixed answer is deterministic but can differ from the float one. Rockets are pinned to the float timing by rounding their velocity up; monsters' turns and bots' step-finish ticks can differ by a tick (02b Outcome).
- The A* float keys: totals that collided were nudged by `+0.001f` until free. A slot freed when a node left the list was reused by the next node with that total, so the order is *not* plain insertion order. A key of `(total, insertion sequence)` gives different routes (the 02b test shows it); `(total, next free number)` gives identical ones.

**Where floats remain in `src/`** (none in `bitriot_sim`): rumble strength (`InputHandler`'s `HIT_RUMBLE_STRENGTH`, `MickSDLUtil`, `GameRunningSDLInput`; since 04b the simulation passes nothing to a device, it only records `hitThisTick`), HUD drawing (`InfoPanel.cpp`), window scaling (`MickSDLRenderer.cpp`), and RmlUi's API (`src/RmlUI`). All of them are in `bitriot_core`. Since brief 03b `FixedPointSourceGuardTest` scans the text of the `bitriot_sim` target's sources and the headers they include (no allowlist), and `SimLibraryTest` disassembles the compiled archive and fails on any floating-point instruction, which sees what the text scan cannot (an `auto` carrying a double, an implicit int to float conversion at a call, a float hidden by a macro).

**Current flags** (CMakeLists.txt): `-Wall -Wno-comment`; Debug (the default build type) `-ggdb -O0`; Release `-O2`; `-include cstdint`. With no floating point in the sim, `-ffp-contract=off` and the `-ffast-math` rejection are not needed for determinism and were not added. The project is pinned to C++17 (`CMAKE_CXX_STANDARD`, extensions off) for every target, and the library builds with `-Wfloat-conversion`.

**Before 02b (history)**

- The sim's float maths was only add, subtract, divide and compare, so IEEE-754 binary32 would have given bit-identical results on x86-64 SSE, aarch64 and wasm provided there was no `-ffast-math`, no x87 code, and `-ffp-contract=off` as a defensive measure. The only multiply was in rendering. The user chose fixed-point up front instead (README D6).
- Files that changed in the conversion: GameEntity, SimpleBrain, SimpleMovingEntity, PlayerCharacterEntity, Robot, Drone, WallEater, RocketFired, RocketSpin, EffectEntity, EntityMessage, EntityMessageQueue, EntityFactory, EntityManager (the create call), InfoPanel (the pixel conversion), plus the zero-offset initialisers in the static entities.

## 5. Iteration order: deterministic

- `DynamicMap::entityList` is a `map<int, shared_ptr<GameEntity>>` (DynamicMap.h:20), iterated in id order in `runAI`.
- Updates only post FIFO messages (a `std::queue`), which are processed after all updates.
- **Exceptions to that rule:**
  - Human input moves players directly, before `runAI` (since 04a that is `Simulation::step`'s input phase, `applySlotInput`).
  - `onCollision` mutates health and `m_playerNumber` immediately (egg steal, EggEntity.cpp:89).
- **Per-tile lists** (`entityMap[x][y]` and the AIGameView per-tile lists) are in insertion order. The AI takes the first match, which is deterministic.
- **Collisions** run row-major over the map interior, over all ordered pairs (EntityManager.cpp ~346-386). Messages posted there are processed on the next tick.
- **Hazards:**
  - `DynamicMap` has shared member iterators (`entityIterator`, `mapLocationIterator`) that several functions use.
  - The `BlockEntity` constructor and destructor write `Map` BLOCK tiles (BlockEntity.cpp:18, :34), so terrain depends on `shared_ptr` lifetime.

## 6. Input

**Buttons and devices**

- Six logical buttons: UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2 (InputHandler.h:42-43).
- Four fixed keyboard sets (InputHandler.cpp:131-167):
  - WASD + LCtrl/LShift
  - arrows + RCtrl/RShift
  - KP8/5/4/6 + KP+/KP-
  - IJKL + [ ]
- Slots 5-6 are controller-only. The maximum is 6 players.
- Controllers: A/X = action1, B/Y = action2, d-pad = directions, START = join. Analog sticks are unused (GameRunningSDLInput.cpp:186-218).

**Sampling and applying are separate** *(brief 04a; before it they were fused in `InputHandler::applyInput()`, with the sim state in `InputHandler`)*

- **Sample (local).** `InputHandler::pumpEvents()` polls SDL once per frame (`GameRunningSDLInput::updateEventQueue`). Every key event of a keyboard set that drives a live human, and every button of the pad attached to a slot, becomes `InputSampler::setButton(slot, button, down)` (`InputHandler::keyboardEvent` / `setControllerButtonState`). `InputHandler::sampleTick()` (once per **stepped** tick) returns a `TickInputs`. `InputSampler` (src/InputSampler.*, SDL-free) keeps, per slot, the buttons currently down and the action-release edges since the last sample; a sample consumes the edges.
- **Apply (deterministic).** `Simulation::step(const TickInputs&)` applies a slot's `PlayerInput` to its player (`applySlotInput`), with per-slot state it owns (`action1Down`, `action2Down`, `spawnSelection`; `Simulation::inputState(slot)`; in the state hash). The rules are the table at the top of `src/TickInputs.h`. Bits for a slot whose player is an AI, dead or missing are ignored.
- **A `PlayerInput` is one byte:** held UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2 (bits 0-5) and the release edges ACTION1_RELEASED, ACTION2_RELEASED (bits 6, 7). **Slot = playerNumber - 1.** A tap (pressed and released between two samples) is RELEASED with HELD clear; the sim reads it as press-then-release when the action was not down (the "tap rule": places BLOCK for ACTION1, EGG1 for ACTION2).
- Buttons: UP, DOWN, LEFT, RIGHT, ACTION1, ACTION2 (`PlayerInput::Button`; `InputHandler::ButtonName` has the same values).
- Four fixed keyboard sets (`InputHandler::setAvailableKeys`):
  - WASD + LCtrl/LShift
  - arrows + RCtrl/RShift
  - KP8/5/4/6 + KP+/KP-
  - IJKL + [ ]
- Slots 5-6 are controller-only. The maximum is 6 players. Controllers: A/X = action1, B/Y = action2, d-pad = directions, START = join. Analog sticks are unused.
- `m_keyMap` (slot -> the six keys of its set) is keyed by **slot**, no longer by entity id (before 04a: entity id).
- **Local slot mask.** `InputSampler::sampleTick(mask)` reports only the slots in the mask and drops the other slots' pending edges. `InputHandler::sampleTick()` passes every human slot (today all humans are local); netplay (07) passes only its own players.
- **Cadence.** Events accumulate in the sampler every frame; a sample is taken only when a tick is stepped, so a tick held back by `canStep()` loses no press, and a catch-up frame reports a release on its first tick only (held buttons are held on every tick). The old global `isAKeyDown()||isAKeyReleased()` gate is gone (it never changed sim behaviour).
- **What changed from the old behaviour** (every other rule is the same, see TickInputs.h):
  1. *The same-tick tap.* A press and release inside one tick used to be lost; its release stayed pending and ended the next hold of that action on its first tick (the "stale release"). Now the tap places the default and releases are per tick.
  2. On a catch-up frame (several ticks owed) a release is reported on the first tick's sample only and not repeated, and a press-and-release between two samples is not lost (rule 1); the old code applied a release seen anywhere in the window on the first tick and lost a press-and-release entirely.
  3. *Application order.* Humans are applied in slot order 0..n-1; the old `applyInput()` did keyboard humans first, then pad humans, so same-tick placements by a lower-slot pad human and a higher-slot keyboard human get their entity ids in the opposite order to before.
- `GameRunningSDLInput`'s key sets (`keysCurrentlyDown` / `keysRecentlyReleased`, `isKeyDown`, `isKeyReleased`) are still maintained for the `MickBaseInput` interface, but nothing in gameplay reads them any more.

**Sim state formerly in InputHandler** *(moved by 04a)*: `m_action1Down`, `m_action2Down` and `m_spawnSelection` were keyed by entity id; they are now `Simulation::SlotInputState` per slot, reset by `start()`, hashed.

`processPlayerButtons` called `p_infoPanel->setSelection` (UI) and `p_dynamicMap->tileHasStaticEntity` (a sim read). Now the sim does the second itself; the first is a read: `InfoPanel` is given the `Simulation` (`setSimulation`) and asks `Simulation::heldSelection(slot)` each time it draws (while an action is held it shows that selection, otherwise `PLAYER_CHARACTER`, the "no selection" marker; the HUD updates on the press tick instead of one tick later). *(04a pushed it with `GameEngine::presentSelections()` and `InfoPanel::setSelection`; both are gone.)*

**Control events** *(brief 04b; before it the input layer wrote `m_controlledByAI` and `attachedController` on the entity directly, from inside `pumpEvents()`)*

- **The type.** `ControlEvent {slot, kind}` in `src/TickInputs.h`: `HUMAN_TAKES_OVER`, `RETURN_TO_AI`, and `LEAVE` (reserved for netplay; today it does exactly what `RETURN_TO_AI` does). They ride in `TickInputs::control`. **Slot is playerNumber - 1.**
- **Applied by the sim, at the start of the tick** (`Simulation::step`: tick++, clear last tick's hit flags, the events in order, then the inputs). `PlayerCharacterEntity::m_controlledByAI` is private; only `Simulation` (a friend) changes it, through `applyControl(toAI)`. Rules:
  - both transitions **clear the player's AI goal and instruction lists** (`clearGoalsAndInstructions()`), and **reset the slot's `SlotInputState`** (no hold, selection BLOCK);
  - an event that changes nothing (a takeover of a human, a return of a bot), or one for a dead player or a missing one, **does nothing** and resets nothing;
  - a slot outside the round's players, or an unknown kind, is refused with `std::out_of_range` / `std::invalid_argument` **before anything of the tick happens** (the tick counter does not move);
  - a takeover tick's input is applied right after the event (the human moves on that tick); a return tick's input is ignored (the slot is a bot from the start of the tick).
- **Hashed:** the flag already was (`player.controlledByAI`); the cleared goal and instruction lists are; the reset slot state is. Nothing new is hashed: `STATE_HASH_VERSION` is still 2.
- **Produced locally** (all `InputHandler`): the join selector's `confirmJoin` (via `takeOverSlotWithController` / `takeOverSlotWithKeyboard`) queues `HUMAN_TAKES_OVER`; `detachController` queues `RETURN_TO_AI` for a live player with no keymap (a player that also has a keymap just loses the pad). `sampleTick()` hands the queued events out in `TickInputs::control`.
- **The input layer's own view**, changed when the event is *produced*, not when the sim applies it (the two are at most a pump-to-step apart): `m_slotController[slot]` (the pad that drives each slot, seeded from the menu's `GameSettings` choice by `setPointers`, so the entity no longer has `attachedController`), `m_localSlots` (the local slot mask: every slot a device here drives; `sampleTick()` passes it to the sampler, `keyboardEvent` routes by it, `getTakeableSlots` excludes it so a joiner is not offered a slot another device has just claimed), and `m_keyMap`. The slot's sampler state is cleared at the same moment.
- The join selector is still called from inside the event pump (`processJoinKeyboardEvent` / `processJoinControllerEvent`, `openJoin` / `navigateJoin` / `confirmJoin`); it only queues now. `InfoPanel::setJoinIndicator` calls remain in `InputHandler` (join UI; skipped when there is no panel).

**Hotplug**

- On `SDL_CONTROLLERDEVICEADDED`, the controller is opened.
- On `REMOVED`, `detachController()` clears the slot's pad and queues `RETURN_TO_AI` if the player has no keymap.

**AI takeover** (the Phase 2.7 selector)

1. `processJoinKeyboardEvent` / `processJoinControllerEvent`, called from inside the event pump.
2. `openJoin` / `navigateJoin` / `confirmJoin`.
3. `takeOverSlotWithController` / `takeOverSlotWithKeyboard` update the local mapping and mask, clear the slot's sampler state and queue a `HUMAN_TAKES_OVER`.

The old AI goal and instruction lists used to persist across a switch; they are cleared on both transitions now (a behaviour change, brief 04b).

**Rumble** *(brief 04b; before it `onCollision` called `RumbleSink::getInstance()->rumble(attachedController, 500, 200)` from inside the sim)*

- `PlayerCharacterEntity::onCollision` sets a private `m_hitThisTick` when it damages the player (not hashed: it follows from health). `Simulation::step` clears the flags at its start and collects them at its end into `hitThisTick(slot)`, valid between two steps. Any player can be hit, bots included: the sim knows nothing of devices.
- After the step `GameEngine::stepTick` calls `InputHandler::rumbleForHits(sim)`, which rumbles (`ControllerRumble::rumble(controllerId, 0.5f, 200 ms)`, SDL haptics by default, replaceable with `setRumble` for a test) every slot that was hit, is a human and has a pad. Several hits in one tick are one rumble (they used to restart the 200 ms rumble each time). `RumbleSink`, `MickSDLRumbleSink` and the entity's `attachedController` are gone. `SimLibraryTest`'s source scan refuses a `rumble`/`haptic` identifier in the simulation.

## 7. Match settings that must be shared

**From `GameSettings` (GameSettings.h:229-242)**

- `numPlayers` (2-6).
- Per-slot `Player.human`/AI and `Player.team` (`m_players`, mirrored into `playerAIs`).
- `blockPercentage` (20-75, default 55).
- `npcSpawnerEnabled`. It is read live every tick (SpawningPool.cpp:72), so it must be frozen per match.
- `mapWidth`/`mapHeight`: odd values, 13-33 by 11-25. The `-b` flag gives 25×19 (main.cpp:153-162).

*(03a: all of the above, plus the seed, is `MatchConfig`. `Simulation::start()` applies it into `GameSettings`, and the sim keeps reading `GameSettings`. `GameSettings::setNumberOfPlayers` silently ignores a count outside 2-6, so `MatchConfig::problem()` rejects those up front.)*

**Derived values**

- Pool points = `numPlayers*100` (`Simulation::start`).
- Co-op vs PvP (`numConfiguredTeams`).
- Fixed spawn tiles per slot (`startingTileFor` in Simulation.cpp, was `GameEngine::initPlayerCharacters`).

**Local or cosmetic only:** gender/portrait, `attachedControllerId`, `attachedKeyboardId` (the menu's choice of device; since 04b the input layer's own `m_slotController` / `m_keyMap` follow it mid-round, and the sim has no device at all).

**Data files**

- `data/entity.txt` and `data/player.txt` are sim parameters: fuses, hatch times, velocities, costs, spawn chances, health. **A missing key silently returns 1** (`DataReader::getIntFromFile`, which counts it in `errorCount()`; the test harness fails a test that makes one).
- *(Brief 05.)* **Both files are loaded eagerly:** `Simulation::start()` calls `DataHash::loadSimData()` before touching anything, so a file that is missing or does not parse throws `std::runtime_error` there instead of every constant answering 1 later. A key missing from a file that does load still reads as 1 in whatever asks for it; `Simulation::dataHash()` (FNV-1a 64 over the parsed, normalised, name-sorted `name=value` items of both files plus the named sim constants in code, `src/DataHash.h`) is what tells a peer with a different file. `DataReader` now strips carriage returns before parsing: before, a CRLF file left a `\r` in the value and did not parse on Linux (on Windows the text-mode stream hid it).
- `DataReader::loadFile()` / `normalisedEntries()` are the eager-load and the parsed-items API.
- `renderer.txt`, `audio.txt` and `messages.txt` are not sim-relevant.
- `map.txt` is never read.

## 8. Networking

There is no networking code at all: no SDL_net, sockets, enet or Emscripten. README.md:72 says "Network play (this will probably never get done)".

## 9. Rendering, menus, audio, threads, filesystem

**`#error` guards** *(removed by brief 09)*. main.cpp and RenderInterfaceSDL2.h tested `SDL_VIDEO_OPENGL`. Verified: Emscripten's `SDL_config_emscripten.h` has `/* #undef SDL_VIDEO_OPENGL */` and defines only `SDL_VIDEO_OPENGL_ES2` and `SDL_VIDEO_OPENGL_EGL`, so both would have fired. `find_package(OpenGL)` and `OPENGL_LIBRARIES` are gone from the native build too (nothing links GL: SDL loads it itself).

**RmlUi glue**

- It uses only `SDL_Renderer`: `SDL_RenderGeometryRaw` (needs SDL ≥ 2.0.18), `SDL_RenderSetClipRect`, and SDL textures (RenderInterfaceSDL2.cpp:53-183).
- There are no `gl*` calls in the compiled sources. src/RmlUI/shell/* and events/Shell.h are dead code (not in SOURCES).
- It works on Emscripten's GLES2 `SDL_Renderer` (and on SDL's software renderer) once the guards and `find_package(OpenGL)` are dropped (brief 09; the smoke test plays through both). It needs SDL_image's **BMP** loader as well as PNG: the player-select page's faces are BMP files, loaded through `IMG_LoadTyped_RW`.
- **Its clipping was wrong, in three ways, and is fixed (brief 09).** (1) `SetScissorRegion` flipped Y the way GL's `glScissor` needs; RmlUi's origin is the top left and so is SDL's, so the flip was wrong. (2) The rectangle was only stored by `SetScissorRegion` and applied by `EnableScissorRegion`, but RmlUi calls Enable first and Set second, so each clipped element was drawn with the previous element's rectangle. (3) `RenderGeometry` translated by setting the renderer's viewport to the element's origin, and SDL takes a clip rectangle relative to the viewport (its GL and GLES2 renderers also do not re-apply the clip when only the viewport changes), so the clip moved with each draw. The shipped menus never showed it because every clipped `.strip` was big enough to contain its content wherever the clip landed; giving `.strip` a fixed `height` makes the content vanish. Now the vertices are translated on the CPU (whole pixels, as before), the rectangle is applied when set, and there is no flip.

**Game renderer**

- Software `SDL_Surface` blits into a CPU back buffer. Since brief 03b the drawing is `MapRenderer` (the tile bitmaps and `Map` drawing), `WorldRenderer` (entities, read-only on the simulation) and `EntityRenderer`/`EntityRendererFactory`, all in `bitriot_core`; the simulation holds no bitmap.
- `SDL_UpdateTexture` copies it to a streaming texture, which is shown with `RenderCopy` and letterboxing (MickSDLRenderer.cpp:154-183).
- SDL_ttf (InfoPanel) and SDL_image for PNG.

**Audio**

- SDL_mixer, `Mix_OpenAudio(44100,…,4096)`, WAV only, loaded lazily on first play. There is no music and no MIDI.
- Sounds are triggered from sim code: entity constructors, `explode`, collisions.
- `MickBaseSound::getInstance()` returns the installed backend (`setBaseInstance`, called by `MickSDLSound` and the test `NullSound`) or, since brief 03b, a built-in silent one, so it is never null and `bitriot_sim` does not link SDL_mixer. A backend that is destroyed unregisters itself.

- **On the web (brief 09):** the page starts the game from its "Click to start" button (`Module.callMain` inside the click handler), so `Mix_OpenAudio` runs while the page may make sound and the `AudioContext` is created running; SDL's own Emscripten audio would also resume a suspended context on the first gesture. If `Mix_OpenAudio` fails there, the game logs a warning and plays on silently (natively it still throws). `MickSDLSound::soundsPlayed()` counts accepted `Mix_PlayChannel` calls, for the smoke test. SDL_mixer is built with no formats (`-sSDL2_MIXER_FORMATS=[]`): WAV only, no ogg or vorbis.

**Threads:** none.

**Filesystem**

- All paths are relative to the cwd: `data/*.txt`, `assets/...`, and the RmlUi root `assets/menu/`.
- Nothing is ever written, so the WASM build needs no IDBFS. The web build preloads `data@/data` and `assets@/assets` (about 4.2 MB packed) and runs with the working directory at `/`; that is all it needs (brief 09, checked).

**RmlUi version**

- 4.3 (the submodule, and `scripts/cloud-setup.sh`). The 4.3 tree has no `Backends/`.
- RmlUi 5.x is believed to ship SDL backends; unverified. The TODO lists "bump RmlUi" as its top item.
- FindRmlUi requires RmlDebugger, which is used when `!NDEBUG`.
- **The web build compiles RmlUi from the submodule** (`cmake/EmscriptenGame.cmake`, from RmlUi's own `CMake/FileList.cmake`, with the FreeType port), not through RmlUi's CMakeLists: that file declares `cmake_minimum_required(VERSION 3.1)`, which CMake 4 refuses. Both libraries are built; the linker drops RmlDebugger in a Release build.

## 10. Harness capabilities for determinism testing

**Already there**

- Headless whole rounds with AI-only players.
- Small test maps.
- `runRoundToCompletion(maxFrames)` in engine order.
- Consecutive rounds in one process.
- A base seed per process (`BITRIOT_TEST_SEED`); each test derives its round seeds from it, its name and its round count.
- Record and replay (05): `MatchRecorder` captures the `TickInputs` handed to each `step` and a hash every K ticks; `replayRecord()` plays a record back on a `Simulation` and names the first checkpoint that disagrees; `bitriot_headless replay|run|dump|version` do both from a shell, with stdout identical on every build. The golden full-round record is `tests/data/golden-round.brmr`.
- Scripted control events (04b): a `TickInputs::control` in `Simulation::step`, and the join, takeover, unplug and rumble paths of `InputHandler` driven by hand with a fake rumble device (`tests/ControlEventTest.cpp`, `tests/InputControlTest.cpp`).

**Missing**

- ~~A per-round seed or PRNG object.~~ Done by brief 02: `GameSettings::setRoundSeed()` / `pickRoundSeed()` (03a: the reseed itself moved into `Simulation::start()`), harness `setRoundSeed()`, `BITRIOT_TEST_SEED` as the base seed.
- ~~A state-hash function.~~ Done by 03a: `Simulation::stateHash()`.
- ~~An input-injection path. `InputHandler::applyInput` hard-wires the `GameRunningSDLInput` singleton.~~ Done by 04a: tests drive `Simulation::step(TickInputs)` directly (`tests/InputApplyTest.cpp`, with `tests/support/ScriptedInput.h`), and `InputHandler`'s device-to-slot routing is testable without SDL events (`tests/InputSamplerTest.cpp`).
- ~~A headless `GameEngine` tick; the harness duplicated the spawn positions and seeded no blocks.~~ Done by 03a: the harness starts rounds with `Simulation::start()` and steps `Simulation::step()`, the code `GameEngine` runs. It defaults to 0% blocks so older scenario tests keep open ground. `GameEngine` itself still cannot run headlessly (renderer, `InfoPanel`, menu), but it is now only glue around `Simulation`.
- Two sims in one process, which the singletons rule out.
- ~~A sim without bitmaps.~~ Done by brief 03b: `GameEntity::incAnimFrame` reads `EntityAnim::frameCount()` (a table in `src/EntityAnim.cpp`, kept in agreement with the bitmaps by `RenderSideTest`), `Map` has no surfaces, and the harness creates no surface. `bitriot_sim_tests` links `bitriot_sim` alone and plays real rounds.

## 11. Other surprises

- **Map-resize bug.**
  - `applyMapDimensionsIfChanged()` rebuilt `Map` before `Simulation::start()` (via `resetGame()`) deleted the old EntityManager. *(Since brief 03b `Simulation::start()` tears the old round down first and then resizes the Map itself; the engine only resizes the window afterwards. The generation guard below still makes the order harmless.)*
  - The old `BlockEntity` destructors then call `setBlockAt(x,y,false)` with the *old* map's tile coordinates against the *new* map's array and stride. That can overwrite the new map's walls with EMPTY (an old block at (8,3) hits the east wall of a 9x7 map), or write past the end of the array if the map shrank. *(Correction to the earlier wording, which blamed "the wrong stride": the stride is the new one; the coordinates are the old ones.)*
  - *Fixed by brief 01:* `Map` has a generation counter bumped by every `init()`. `BlockEntity` remembers the generation it was placed in and its destructor skips the write if the map has since been rebuilt.
- **ESC/quit** is local: a flag (`InputHandler::quitRequested()`, set by `GameRunningSDLInput`), no longer an exception. `GameEngine::stepTick` reads it, still runs the tick (with that tick's input applied, which the old exception skipped) and flips this machine to the scoreboard; `renderFrame()` also checks it on a frame that ran no tick.
- **`GameSettings::setNumberOfPlayers`** keeps existing slots. `InputHandler::setPointers` writes keyboard assignments back into `GameSettings` (InputHandler.cpp:106).
- **Debug logging** is compiled in by default (the default build type is Debug), and `MickLogger` writes debug output to `cout` every frame. The Release web build (the default for `emcmake`) has `NDEBUG`, so it prints info and above, which is the brief's "default to info level on web"; a Debug web build still logs debug lines.
- **`Map::~Map`** uses `delete` rather than `delete[]` (Map.cpp:122).

## 12. Further facts (from the brief review)

**`m_currentAnimState` is sim state, not render state.** It drives movement (PlayerCharacterEntity.cpp:741; SimpleMovingEntity.cpp:90-92), and `SimpleBrain` holds a reference to it (`rAnimState`). Only `m_currentAnimFrame` and `m_animCounter` are render-only.

**A second SDL-surface coupling** *(removed by brief 03b)*. `Map::init(SDL_Surface*)` loaded BMPs and converted them against the primary surface, which, with `EntityRendererFactory`'s frame counts, is why the sim needed bitmaps. `Map::init()` now builds the layout only (and `Map::resize()` does so when the size changes, called by `Simulation::start()`), and `MapRenderer::init(surface)` loads the tile pictures.

**The sim reads `GameSettings` all the time.** Examples:

- `GameEntity::getTeamNumber`/`isTeammatePlayer` go through `teamNumberForPlayer` on every collision.
- EntityManager.cpp ~342 reads `numConfiguredTeams`.
- There are many `getMapWidth`/`getMapHeight` reads.

**RNG is consumed before world seeding.** `SpawningPool`'s constructor and the `PlayerCharacterEntity` constructors draw from the sim stream before block seeding. That is why `Simulation::start()` reseeds before it builds anything (the order is pool, players, blocks, and is part of the replay contract). The harness goes through the same function.

**`% 0` was undefined behaviour.** `rand() % (m_maxDelay - m_minDelay)` in SpawningPool (and `rand() % range` in RobotEntity) divided by zero if the two bounds were equal. `SimRandom::nextInt(n)` returns 0 for n <= 1. (The shipped data has min < max in both cases, so it never fired.)

**The round-end tick.** On the tick the GameOverTimer completes, pool and `runFrame` still run (and, in the engine, so does that tick's input): the round is over only after the whole step (03a pinned this down in `Simulation`; the engine used to flip the state to `MENU_RUNNING` part-way through the tick). An ESC quit behaves the same way. `winner()` is fixed at the top of the end tick; the roster and scoreboard stats are those after the step.

**Action input rules** (the old `processPlayerButtons`, now `Simulation::applySlotInput`; the full table is at the top of `src/TickInputs.h`):

- A release is only acted on inside the matching action's hold. ACTION1 released while ACTION2 is down, and vice versa, is ignored (and so is the other action's press during a hold). If the other action is still held when a hold ends, its hold starts on the *next* tick.
- Holding an action blocks movement, and turns directions into item or egg selection (A1: UP bomb, DOWN block, LEFT mine, RIGHT rocket spinner; A2: UP EGG3, DOWN EGG5, LEFT EGG2, RIGHT EGG4). The selection sticks when the direction is let go; the direction held on the release tick still counts.
- An ACTION2 press selects EGG1 immediately; an ACTION1 hold with no direction places BLOCK (both verified against `PlayerCharacterEntity::createEntity`).
- *(Before 04a)* A release with no prior down state was ignored *and kept pending*. Now: with no hold on, a RELEASED bit is a tap (press then release in that tick) and places the default.
- The old `isAKeyDown()||isAKeyReleased()` gate changed only when the UI updated, not sim behaviour; it is gone.

**`InputHandler::setPointers`** (~:21-129) gives keyboard sets to every human slot (by `attachedKeyboardId`, seeding the lowest free set), and writes those assignments back into `GameSettings`. It also clears the sampler. Brief 07 must exclude remote humans from this assignment.

**`MickSDLRenderer` threw pointers** *(fixed by brief 09)*. It did `throw new runtime_error(msg)`, so `catch (const exception&)` never caught them and a renderer-init failure aborted without a message. It throws by value now (`main()` logs the message, and on the web shows it on the page), falls back to `SDL_HINT_RENDER_DRIVER=software` if no accelerated renderer can be made, and `MickSDLRenderer::getInstanceIfCreated()` lets the teardown cope with a renderer that was never made. (A browser with no WebGL needs no fallback from us: `SDL_CreateRenderer(-1)` already ends at the software driver, which the smoke test checks.)

**The scoreboard's "Play Again"** (assets/menu/scoreboard.rml ~:34) sets `GAME_INIT` locally (EventManager.cpp ~:114).

**`main` parses options with `getopt_long`** *(brief 05; it was `getopt("fb")`)*: `GameOptions.cpp` (`parseGameOptions`, testable, resets `optind` so it can run twice) takes `-f/--fullscreen`, `-b/--big-map`, `-h/--help` and `--record <dir>`. An unknown option or a stray argument now prints the usage and exits 2 (before, an unknown option printed the usage and the game started anyway); `-h` exits 0. `main.cpp` applies the result. `--record` hands the directory to `GameEngine::setRecordDirectory`; the engine records each round with a `MatchRecorder` (a checkpoint every 30 ticks, RLE) and writes `<dir>/round-<UTC>-seed<seed>.brmr` when the round ends or is quit; `main()` flushes a round still open when the window is closed.

**`InputHandler` holds no per-player game state** (04a). Its `m_keyMap`, the `InputSampler`, `m_slotController` and the local slot mask are keyed by slot (playerNumber - 1) and rebuilt/cleared in `setPointers`, so resetting entity ids per round no longer matters to them. What it holds is the *device* side (04b): which pad and keyboard set drives each slot, which slots are local, and the control events produced and not yet sampled. Brief 07 must keep remote humans out of the local mask and out of `setPointers`' keyboard assignment.

**libstdc++ vs libc++** (Emscripten uses libc++): *checked by brief 05b, no divergence.* The gate replays the golden record and six more rounds, a hash on every tick, under GCC and Clang with libstdc++, Clang with LLVM libc++ 18 natively, aarch64 under qemu-user and wasm32 under Node (details in 05b's Outcome and `tests/README.md`).

- The sim uses only `map`, `set`, `list`, `queue`, `vector` and `std::find`.
- The single `std::sort` is in UI code, over unique keys.
- There is no `long`/`size_t` arithmetic in the sim, so wasm32's 32-bit `long` doesn't matter.
- Function-argument evaluation order *does* differ between GCC and Clang, which only matters if two RNG draws share one expression. There are none today.

**Emscripten and the simulation (05b).**

- **Exceptions need a mode.** Emscripten builds with no exception support by default, and the simulation's refusals (`std::logic_error`, `std::out_of_range`, `std::invalid_argument` from `Simulation::start/step`, `MatchRecordError`, the CLI's usage errors) are caught C++ exceptions. A wasm `bitriot_headless` built without `-fwasm-exceptions` replays the golden record correctly and then **aborts** on the first throw ("Aborted(undefined)", exit 1). The gate's exception probes exist for this. With `-fwasm-exceptions` Emscripten 6.0.13 emits the *legacy* wasm EH (`-sWASM_LEGACY_EXCEPTIONS` defaults to 1); `-sWASM_LEGACY_EXCEPTIONS=0` emits the standard exnref EH. Both run on Node 22.22.0 (V8 12.4) and on the Node 24.19.0 that emsdk ships, with no flag, and both replay identically.
- **`-sNODERAWFS=1 -sENVIRONMENT=node`** is all the headless binary needs to read the record and `data/` from the working directory. No `--preload-file`, no Emscripten ports (the sim has no SDL). The 64 KB default stack was enough for every round replayed (checked with `-sSTACK_OVERFLOW_CHECK=2`); the gate links with a 1 MiB stack and growable memory anyway.
- **Argument evaluation order differs by compiler and target.** An experiment (not committed) with two calls in one argument list: x86-64 GCC evaluates them right to left; Clang, aarch64 GCC and wasm (Clang) left to right. So the rule in `SimRandom.h` (never two sim draws in one expression) is not hygiene, it is what keeps the platforms equal, and the gate catches a breach at the first tick it runs.
- **`replay --perturb-at TICK`** (test only): adds 1 to the x offset of the first block after tick TICK-1. It targets a block on purpose: a player's or monster's offset is pulled back towards the tile centre by the next step (`GameEntity::moveInDirection`), so a one-unit nudge there vanishes without changing the next tick's hash.
- **`g++-multilib` and `g++-aarch64-linux-gnu` conflict on Ubuntu** (installing one removes the other), so a `-m32` run (a cheap stand-in for wasm32's 32-bit `long`) and the aarch64 cross build cannot both be set up on one machine with apt.
