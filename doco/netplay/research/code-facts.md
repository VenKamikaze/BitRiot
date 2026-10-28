# Code facts for the netplay / WASM work

A fact check of the current code, made for the briefs in `doco/netplay/`. File:line refs are against `claude` at 996c1ba (Oct 2026). Since then `fix/round-message-leak` has been merged, which shifts lines in `src/EntityManager.*` and `src/EntityMessageQueue.*`, so re-find anything in those two files by name. Re-check any line ref before relying on it.

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
  - `GAME_INIT`: `applyMapDimensionsIfChanged()` + `resetGame()`, (GameEngine.cpp:140-147). No sim tick runs. *(It used to stamp `m_gameStartedAtTicks=SDL_GetTicks()`; brief 01 replaced that with the per-round tick counter `m_gameTicks`, zeroed in `resetGame()`.)*
  - `GAME_RUNNING`, in this order:
    1. Win check and GameOverTimer, as one `GameOverTimer::advance(...)` call (150-162).
    2. `m_pInputHandler->processKeyboardInput()` (164). This is where SDL events are pumped.
    3. `m_pSpawningPool->update()` (177).
    4. `m_pEntityManager->runFrame()` (179), which is `runAI` → `processMessageQueue` → `runCollisions`.
    5. Render to the CPU surface (181-183), then `pushCpuBufferToHardwareBuffer()` (185).
- **Sim and render are one call.** On the tick a round ends, the state flips to `MENU_RUNNING` and the scoreboard loads mid-tick (159-160). Input, pool, runFrame and render still run for the rest of that tick.
- **Exceptions are control flow.**
  - ESC throws `InputException(GOT_QUIT)` (InputHandler.cpp:650), caught at GameEngine.cpp:166 and main.cpp:218.
  - There are 16 `throw` sites in src. WASM builds therefore need `-fexceptions` or `-fwasm-exceptions`.
- **What a WASM main-loop inversion touches:**
  - main.cpp:171-278: the loop locals, the per-frame try/catch, and teardown (258-277), which moves to a quit path.
  - The busy-wait (main.cpp:121-126).
  - Sim/render decoupling inside `runEngine`.

## 2. Randomness and other nondeterminism

**Seeding.** `srand(SDL_GetTicks())` at MickSDLRenderer.cpp:95, once at window init, never reseeded per round. Tests call `srand(seed)` once per process (tests/test_main.cpp:33).

**`rand()` call sites.** 27 sim sites and 1 UI site, all on the single libc stream.

| File | Lines | Controls |
|---|---|---|
| SpawningPool.cpp | :19, :39 | spawn delay |
| | :129, :160 | type roll |
| | :197-198 | tile x/y |
| | :233, :240 | robot flags |
| GameEngine.cpp | :269 | block seeding per tile |
| PlayerCharacterEntity.cpp (bot AI) | :30, :76 | `m_checkTimer` 37/67 (:30 also runs for humans) |
| | :37 | initial wait |
| | :43 | initial EGG2 goal |
| | :335, :339, :346, :350, :354, :357, :361 | `determineGoals` |
| | :385 | energy goal |
| | :486 | per-create wait |
| RobotEntity.cpp | :34, :57 | action counter |
| SimpleBrain.cpp | :16 | initial heading |
| | :70 | heading roll inside a `while` loop, so the number of calls depends on data |
| EffectEntity.cpp | :23 | damage-effect anim frame. Cosmetic, but it consumes the sim stream. |
| ScoreBoardBinder.cpp (UI) | :114 | `randomWinText`; consumes the same stream between rounds |

- No expression contains two `rand()` calls, so there is no evaluation-order hazard today.
- RmlUi's `Math::Random*` functions exist but are unused.

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
- IDs are only compared for equality, and `std::map` order follows creation order. So a different base changes checksums, not behaviour.
- InputHandler keys its per-player maps by entity id (InputHandler.cpp:66, :498).

**GameOverTimer: wall-clock, with three bugs** *(1, 2 and the wall-clock part fixed by brief 01: the timer now counts ticks, is reset by `EntityManager`'s constructor, and the countdown is `GameOverTimer::GAME_OVER_COUNTDOWN_TICKS = 60`, i.e. 2 s. Item 3 stands.)*

1. It latches: `timerTriggered` is never reset, so from round 2 onwards the round ends on the tick the end condition first holds.
2. The base constructor calls the virtual `getDefaultTimeout()` (GameTimer.cpp:20). The countdown is therefore 5 s (`STANDARD_COUNTDOWN`), not the intended `GAME_OVER_COUNTDOWN=2`.
3. During the countdown the sim keeps running, and AI takeover is still allowed.

`GameScoreBoardTimer` was defined but never used; brief 01 removed it.

**All other sim timing is integer tick counters.** Bomb and mine fuses, egg hatch, power-up decay, effect and explosion health, rocket lifespan, robot actions, the spawner delay, AI WAIT/`checkTimer`, and SimpleBrain `m_minMoveTimer`.

## 4. Floats

> **Decision (Q2, Oct 2026):** the sim converts to fixed-point in brief 02b, so this section describes the code *before* 02b. See 02b's Outcome for what landed.

**Where floats appear**

- `m_offsetX/Y` and `m_velocity` (GameEntity.h:144, :146).
- Movement (`GameEntity::moveInDirection`, GameEntity.cpp:31-335) uses only add, subtract and compare.
- Velocity is an int converted to float, then a plus or divide (PlayerCharacterEntity.cpp:22, :66; SimpleMovingEntity.cpp:27, :85; Robot :22; Drone :15; WallEater :13; RocketFired :22).
- Compares in `nearTileCenter` (PlayerCharacterEntity.cpp:1026-1040; SimpleBrain.cpp:191-205), and RocketFired `abs()` (RocketFiredEntity.cpp:78, :84).
- The A* open list uses float keys.
- The rocket's target tile is passed through the float offset parameters (RocketSpinEntity.cpp:152 → RocketFiredEntity.cpp:50-51).

**The only multiply is in rendering** (GameEntity.cpp:350-351). IEEE-754 binary32 add, subtract, divide and compare therefore give bit-identical results on x86-64 SSE, aarch64 and wasm, provided there is:

- no `-ffast-math`
- no x87 code (a 32-bit MinGW build needs `-msse2 -mfpmath=sse`)
- `-ffp-contract=off` as a defensive measure. GCC defaults to `fast` under gnu++14, though there is nothing to fuse today.

**Current flags** (CMakeLists.txt): `-Wall -Wno-comment`; Debug (the default build type) `-ggdb -O0`; Release `-O2`; `-include cstdint`.

**Fixed-point conversion scope, if it is ever needed**

- About 12 files, mostly mechanical: GameEntity, SimpleBrain, SimpleMovingEntity, PlayerCharacterEntity, Robot, Drone, WallEater, RocketFired, EffectEntity, EntityMessage (.h:16, :52-79), EntityMessageQueue (.h:21), EntityFactory, and the A* key.
- Velocities such as 110/400 are not exactly representable in fixed-point, so a conversion slightly changes how play feels.

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

**Derived values**

- Pool points = `numPlayers*100` (GameEngine.cpp:111).
- Co-op vs PvP (`numConfiguredTeams`).
- Fixed spawn tiles per slot (GameEngine.cpp:283-374, `initPlayerCharacters`).

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
- A fixed seed per process (`BITRIOT_TEST_SEED`).

**Missing**

- A per-round seed or PRNG object.
- A state-hash function.
- An input-injection path. `processKeyboardInput` hard-wires the `GameRunningSDLInput` singleton.
- A headless `GameEngine` tick. GameEngine needs the renderer, InfoPanel and the menu, and `seedBlocksOnMap`/`initPlayerCharacters` are private. **The harness duplicates the spawn positions and seeds no blocks, so harness rounds are not real rounds.**
- Two sims in one process, which the singletons rule out.
- A sim without bitmaps: it still needs `EntityRendererFactory` surfaces, because `incAnimFrame` calls `getNumFrames` (GameEntity.cpp:358-368).

## 11. Other surprises

- **Map-resize bug.**
  - `applyMapDimensionsIfChanged()` rebuilds `Map` before `resetGame()` deletes the old EntityManager (GameEngine.cpp:143, :72, :91).
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

**RNG is consumed before world seeding.** `SpawningPool`'s constructor (SpawningPool.cpp:19) and the `PlayerCharacterEntity` constructors draw from `rand()` before `seedBlocksOnMap`.

**`% 0` is undefined behaviour.** `rand() % (m_maxDelay - m_minDelay)` in SpawningPool divides by zero if the two delays are equal.

**The round-end tick.** On the tick the GameOverTimer completes, the state flips to `MENU_RUNNING`, but input, pool and `runFrame` still run for that tick (GameEngine.cpp ~157-179). An ESC quit behaves the same way.

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
