# Code facts for the netplay / WASM work

A fact check of the current code, made for the briefs in `doco/netplay/`. File:line refs are against `claude` at 996c1ba (Oct 2026). Since then `fix/round-message-leak` has been merged, which shifts lines in `src/EntityManager.*` and `src/EntityMessageQueue.*`, so re-find anything in those two files by name. Re-check any line ref before relying on it.

*Brief 03a (Oct 2026) moved round setup and the tick out of `GameEngine` into `Simulation` (`src/Simulation.*`); §1, §2, §3, §7, §10 and §12 are updated for it, and the `GameEngine.cpp` line refs below that predate it (marked) no longer exist.*

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

- **`main()`'s loop** (main.cpp:199-247): lock the surface, call `consoleMain()`, call `SDL_RenderPresent` (:234).
- **`consoleMain()` runs one frame** (main.cpp:115-130). It calls `engine->runEngine()`, then busy-waits in `SDL_Delay(1)` until 33 ms have passed (`GameSettings::NORMAL_TICKS_SPEED=33`, GameSettings.h:220).
  - A slow frame slows the game; there is no catch-up.
  - `FAST_TICKS_SPEED` (GameSettings.h:221) is unused.
- **`GameEngine::runEngine()`** (GameEngine.cpp:123-196) switches on the global `GameSettings::gameState`:
  - `MENU_RUNNING`: `menuSystem->showMenu()`. This pumps SDL events itself and calls RmlUi Update/Render (MenuSDLRenderer.cpp:108-256).
  - `GAME_INIT`: `applyMapDimensionsIfChanged()` + `resetGame()`. No sim tick runs. *(03a: `resetGame()` builds a `MatchConfig` from `GameSettings` with `pickRoundSeed()`, calls `Simulation::start(config)`, then does the presentation side: the `InfoPanel`, `resetInputEvents`, each player's controller and team sprite, `InputHandler::setPointers`. The tick counter is `Simulation::tick()`; the engine's own `m_gameTicks` is gone.)*
  - `GAME_RUNNING`, in this order *(03a; it used to be win check, then input, then pool, runFrame)*:
    1. `m_pInputHandler->processKeyboardInput()`. This is where SDL events are pumped; ESC sets a local quit flag.
    2. `Simulation::step()`: tick++, the win check and `GameOverTimer::advance(tick, ...)` at the top, then `SpawningPool::update()`, then `EntityManager::runFrame()` (`runAI` → `processMessageQueue` → `runCollisions`). See Simulation.h for the exact semantics.
    3. If `roundOver()` (or ESC was pressed): state to `MENU_RUNNING` and the scoreboard loads, from the state after the step.
    4. Render to the CPU surface, then `pushCpuBufferToHardwareBuffer()`.
- **The engine does one sim step and one render per call.** The round is over only after the whole step has run: pool and `runFrame` run on the tick it ends. The scoreboard therefore sees the state after that final step (the winner itself is fixed at the top of the tick). Moving the win check ahead of input is harmless: input only moves players and posts messages, and the win check reads alive flags and entity groups.
- **Exceptions are control flow.**
  - ESC throws `InputException(GOT_QUIT)` (InputHandler.cpp:650), caught at GameEngine.cpp:166 and main.cpp:218.
  - There are 16 `throw` sites in src. WASM builds therefore need `-fexceptions` or `-fwasm-exceptions`.
- **What a WASM main-loop inversion touches:**
  - main.cpp:171-278: the loop locals, the per-frame try/catch, and teardown (258-277), which moves to a quit path.
  - The busy-wait (main.cpp:121-126).
  - Sim/render decoupling inside `runEngine`.

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
- `Map::staticTileAt` has no bounds check (Map.cpp:28-31).
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

**Where floats remain in `src/`** (none in sim code): rumble strength (`MickSDLUtil`, `GameRunningSDLInput`, and one literal at the call in `PlayerCharacterEntity::onCollision` until brief 04b removes rumble from the sim), HUD drawing (`InfoPanel.cpp`), window scaling (`MickSDLRenderer.cpp`), and RmlUi's API (`src/RmlUI`). `FixedPointSourceGuardTest` scans `src/` and keeps it that way (a text scan: it cannot see an `auto` carrying a double or an implicit int to float conversion, e.g. `MenuSDLRenderer.cpp`'s `ProcessMouseWheel(event.wheel.y, ...)`; brief 03b should add an object-level check); the allowlist is in `tests/FixedPointGuardTest.cpp`.

**Current flags** (CMakeLists.txt): `-Wall -Wno-comment`; Debug (the default build type) `-ggdb -O0`; Release `-O2`; `-include cstdint`. With no floating point in the sim, `-ffp-contract=off` and the `-ffast-math` rejection are not needed for determinism and were not added. The project is pinned to C++17 (`CMAKE_CXX_STANDARD`, extensions off) for every target, and the library builds with `-Wfloat-conversion`.

**Before 02b (history)**

- The sim's float maths was only add, subtract, divide and compare, so IEEE-754 binary32 would have given bit-identical results on x86-64 SSE, aarch64 and wasm provided there was no `-ffast-math`, no x87 code, and `-ffp-contract=off` as a defensive measure. The only multiply was in rendering. The user chose fixed-point up front instead (README D6).
- Files that changed in the conversion: GameEntity, SimpleBrain, SimpleMovingEntity, PlayerCharacterEntity, Robot, Drone, WallEater, RocketFired, RocketSpin, EffectEntity, EntityMessage, EntityMessageQueue, EntityFactory, EntityManager (the create call), InfoPanel (the pixel conversion), plus the zero-offset initialisers in the static entities.

## 5. Iteration order: deterministic

- `DynamicMap::entityList` is a `map<int, shared_ptr<GameEntity>>` (DynamicMap.h:20), iterated in id order in `runAI`.
- Updates only post FIFO messages (a `std::queue`), which are processed after all updates.
- **Exceptions to that rule:**
  - Human input moves players directly, before `runAI`.
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

**Sampling and applying are fused.** `processKeyboardInput()` (InputHandler.cpp:642-689) pumps SDL (`updateEventQueue`, GameRunningSDLInput.cpp:221-291), then applies the input through `processPlayerButtons(player, isDown, isReleased)` (:494-619). That function already takes `std::function` predicates, which is the natural seam for a bitmask.

**Semantics**

- `isKeyDown` means the key is held.
- `isKeyReleased` is a sticky pending release that the first read consumes. Controllers work the same way.
- Keyboard players are processed only when some key is down or released.
- **Quirk:** a tap shorter than one tick is lost, but its release stays pending.
- Directions use else-if precedence: up, then down, then left, then right.
- Actions: press to select, release to place.

**Human sim state lives in InputHandler**, keyed by entity id: `m_action1Down`, `m_action2Down` and `m_spawnSelection` (InputHandler.h:64-66).

`processPlayerButtons` also calls:

- `p_infoPanel->setSelection` (UI)
- `p_dynamicMap->tileHasStaticEntity` (a sim read)

**Hotplug**

- On `SDL_CONTROLLERDEVICEADDED`, the controller is opened.
- On `REMOVED`, `detachController()` reverts the player to AI if it has no keymap (InputHandler.cpp:463-491).

**AI takeover** (the Phase 2.7 selector)

1. `processJoinKeyboardEvent` / `processJoinControllerEvent` (:375-461), called from inside the event pump.
2. `openJoin` / `navigateJoin` / `confirmJoin` (:268-363).
3. `takeOverSlotWithController` / `takeOverSlotWithKeyboard` (:195-223) set the public field `m_controlledByAI=false` (PlayerCharacterEntity.h:69).

The old AI goal and instruction lists persist across the switch.

**Rumble from the sim:** `onCollision` rumbles through the entity's `attachedController` (PlayerCharacterEntity.cpp:121-125).

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

**Local or cosmetic only:** gender/portrait, `attachedControllerId`, `attachedKeyboardId`.

**Data files**

- `data/entity.txt` and `data/player.txt` are sim parameters: fuses, hatch times, velocities, costs, spawn chances, health. **A missing key silently returns 1** (DataReader.cpp ~83-89).
- `renderer.txt`, `audio.txt` and `messages.txt` are not sim-relevant.
- `map.txt` is never read.

## 8. Networking

There is no networking code at all: no SDL_net, sockets, enet or Emscripten. README.md:72 says "Network play (this will probably never get done)".

## 9. Rendering, menus, audio, threads, filesystem

**`#error` guards.** main.cpp:67-69 and RenderInterfaceSDL2.h:35-37 test `SDL_VIDEO_OPENGL`. Emscripten's SDL is believed to define only the GLES2/EGL macros, so both guards would fire there. This is unverified.

**RmlUi glue**

- It uses only `SDL_Renderer`: `SDL_RenderGeometryRaw` (needs SDL ≥ 2.0.18), `SDL_RenderSetClipRect`, and SDL textures (RenderInterfaceSDL2.cpp:53-183).
- There are no `gl*` calls in the compiled sources. src/RmlUI/shell/* and events/Shell.h are dead code (not in SOURCES).
- It should work on Emscripten's GLES2 `SDL_Renderer` once the guards and `find_package(OpenGL)` are dropped.
- Minor: `SetScissorRegion` flips Y the way GL does (:102), which is probably wrong for SDL.

**Game renderer**

- Software `SDL_Surface` blits into a CPU back buffer.
- `SDL_UpdateTexture` copies it to a streaming texture, which is shown with `RenderCopy` and letterboxing (MickSDLRenderer.cpp:154-183).
- SDL_ttf (InfoPanel) and SDL_image for PNG.

**Audio**

- SDL_mixer, `Mix_OpenAudio(44100,…,4096)`, WAV only, loaded lazily on first play. There is no music and no MIDI.
- Sounds are triggered from sim code: entity constructors, `explode`, collisions.
- `MickBaseSound::setBaseInstance` lets a null backend be swapped in to mute playback.

**Threads:** none.

**Filesystem**

- All paths are relative to the cwd: `data/*.txt`, `assets/...`, and the RmlUi root `assets/menu/`.
- Nothing is ever written, so the WASM build needs no IDBFS. `--preload-file data --preload-file assets` should suffice.

**RmlUi version**

- 4.3 (the submodule, and `scripts/cloud-setup.sh`). The 4.3 tree has no `Backends/`.
- RmlUi 5.x is believed to ship SDL backends; unverified. The TODO lists "bump RmlUi" as its top item.
- FindRmlUi requires RmlDebugger, which is used when `!NDEBUG`.

## 10. Harness capabilities for determinism testing

**Already there**

- Headless whole rounds with AI-only players.
- Small test maps.
- `runRoundToCompletion(maxFrames)` in engine order.
- Consecutive rounds in one process.
- A base seed per process (`BITRIOT_TEST_SEED`); each test derives its round seeds from it, its name and its round count.

**Missing**

- ~~A per-round seed or PRNG object.~~ Done by brief 02: `GameSettings::setRoundSeed()` / `pickRoundSeed()` (03a: the reseed itself moved into `Simulation::start()`), harness `setRoundSeed()`, `BITRIOT_TEST_SEED` as the base seed.
- ~~A state-hash function.~~ Done by 03a: `Simulation::stateHash()`.
- An input-injection path. `processKeyboardInput` hard-wires the `GameRunningSDLInput` singleton. (Brief 04a.)
- ~~A headless `GameEngine` tick; the harness duplicated the spawn positions and seeded no blocks.~~ Done by 03a: the harness starts rounds with `Simulation::start()` and steps `Simulation::step()`, the code `GameEngine` runs. It defaults to 0% blocks so older scenario tests keep open ground. `GameEngine` itself still cannot run headlessly (renderer, `InfoPanel`, menu), but it is now only glue around `Simulation`.
- Two sims in one process, which the singletons rule out.
- A sim without bitmaps: it still needs `EntityRendererFactory` surfaces, because `incAnimFrame` calls `getNumFrames` (GameEntity.cpp:358-368).

## 11. Other surprises

- **Map-resize bug.**
  - `applyMapDimensionsIfChanged()` rebuilds `Map` before `Simulation::start()` (via `resetGame()`) deletes the old EntityManager.
  - The old `BlockEntity` destructors then call `setBlockAt(x,y,false)` with the *old* map's tile coordinates against the *new* map's array and stride. That can overwrite the new map's walls with EMPTY (an old block at (8,3) hits the east wall of a 9x7 map), or write past the end of the array if the map shrank. *(Correction to the earlier wording, which blamed "the wrong stride": the stride is the new one; the coordinates are the old ones.)*
  - *Fixed by brief 01:* `Map` has a generation counter bumped by every `init()`. `BlockEntity` remembers the generation it was placed in and its destructor skips the write if the map has since been rebuilt.
- **ESC/quit** is local: it throws `InputException` and flips this machine to the scoreboard (GameEngine.cpp:166-172).
- **`GameSettings::setNumberOfPlayers`** keeps existing slots. `InputHandler::setPointers` writes keyboard assignments back into `GameSettings` (InputHandler.cpp:106).
- **Debug logging** is compiled in by default (the default build type is Debug), and `MickLogger` writes debug output to `cout` every frame.
- **`Map::~Map`** uses `delete` rather than `delete[]` (Map.cpp:122).

## 12. Further facts (from the brief review)

**`m_currentAnimState` is sim state, not render state.** It drives movement (PlayerCharacterEntity.cpp:741; SimpleMovingEntity.cpp:90-92), and `SimpleBrain` holds a reference to it (`rAnimState`). Only `m_currentAnimFrame` and `m_animCounter` are render-only.

**A second SDL-surface coupling.** `Map::init(SDL_Surface*)` (Map.cpp:37, ~85-92) loads BMPs and converts them against the primary surface. Together with `EntityRendererFactory`'s frame counts, this is why the sim currently needs bitmaps.

**The sim reads `GameSettings` all the time.** Examples:

- `GameEntity::getTeamNumber`/`isTeammatePlayer` go through `teamNumberForPlayer` on every collision.
- EntityManager.cpp ~342 reads `numConfiguredTeams`.
- There are many `getMapWidth`/`getMapHeight` reads.

**RNG is consumed before world seeding.** `SpawningPool`'s constructor and the `PlayerCharacterEntity` constructors draw from the sim stream before block seeding. That is why `Simulation::start()` reseeds before it builds anything (the order is pool, players, blocks, and is part of the replay contract). The harness goes through the same function.

**`% 0` was undefined behaviour.** `rand() % (m_maxDelay - m_minDelay)` in SpawningPool (and `rand() % range` in RobotEntity) divided by zero if the two bounds were equal. `SimRandom::nextInt(n)` returns 0 for n <= 1. (The shipped data has min < max in both cases, so it never fired.)

**The round-end tick.** On the tick the GameOverTimer completes, pool and `runFrame` still run (and, in the engine, so does that tick's input): the round is over only after the whole step (03a pinned this down in `Simulation`; the engine used to flip the state to `MENU_RUNNING` part-way through the tick). An ESC quit behaves the same way. `winner()` is fixed at the top of the end tick; the roster and scoreboard stats are those after the step.

**Action input rules today** (InputHandler.cpp ~494-619):

- `isReleased` is only checked inside the matching action-down branch (~:565, ~:604).
- Holding an action blocks movement, and turns directions into item or egg selection.
- An ACTION2 press selects EGG1 immediately.
- A release with no prior down state is ignored.
- Keyboard players are processed only when `isAKeyDown()||isAKeyReleased()` (~:653). This gate changes only when the UI updates, not sim behaviour.

**`InputHandler::setPointers`** (~:93-112) gives keyboard sets to every human slot, and writes those assignments back into `GameSettings`.

**`MickSDLRenderer` throws pointers.** It does `throw new runtime_error(msg)` (MickSDLRenderer.cpp:35-143), so `catch (const exception&)` never catches them, and a renderer-init failure aborts without a useful message.

**The scoreboard's "Play Again"** (assets/menu/scoreboard.rml ~:34) sets `GAME_INIT` locally (EventManager.cpp ~:114).

**`main` parses options with `getopt("fb")`.** Long options need `getopt_long`.

**`InputHandler`'s id-keyed maps** are cleared in `setPointers`, so resetting entity ids per round is safe for them.

**libstdc++ vs libc++** (Emscripten uses libc++): no known divergence.

- The sim uses only `map`, `set`, `list`, `queue`, `vector` and `std::find`.
- The single `std::sort` is in UI code, over unique keys.
- There is no `long`/`size_t` arithmetic in the sim, so wasm32's 32-bit `long` doesn't matter.
- Function-argument evaluation order *does* differ between GCC and Clang, which only matters if two RNG draws share one expression. There are none today.
