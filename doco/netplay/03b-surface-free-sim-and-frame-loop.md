# 03b — Surface-free sim library and frame loop

- **Repo:** BitRiot
- **Branch:** `netplay/03b-sim-lib-frame-loop`
- **Depends on:** 03a
- **Status:** done (play-test pending)

## Goal

Two structural changes that later briefs build on:

1. **The simulation runs with no bitmaps, no SDL surfaces and ideally no SDL at all.** It becomes its own CMake library, `bitriot_sim`, with no RmlUi, SDL_ttf, SDL_mixer or SDL_image. This makes the following possible:
   - a headless binary (05)
   - a wasm-under-Node determinism gate that needs no Emscripten SDL ports (05b)
   - an aarch64 check under qemu (05b)
   - hot-join fast-forward (13)
2. **One frame loop shared by the native and web builds.** It uses a fixed-tick accumulator and renders without stepping. It also has a "may I step?" hook, which netplay (07) uses to stall and catch up, and the WASM main loop (09) wraps directly.

## Read first

- [`research/code-facts.md`](research/code-facts.md): §1 (loop), §9 (renderer), §10 (bitmap coupling), §12 (`Map::init` surface coupling).
- Brief 03a's Outcome, covering the `Simulation` API as it landed.
- `main.cpp` (`main`, `consoleMain`), `src/GameEngine.cpp` (`runEngine`), `src/Map.cpp` (`init`), `src/EntityRendererFactory.*`, `src/GameEntity.cpp` (`incAnimFrame`/`getNumFrames`), and `CMakeLists.txt` (the `bitriot_core` library).

## In scope

1. **Decouple the sim from surfaces.**
   - **Animation frame counts.** `incAnimFrame` asks `EntityRendererFactory` for the frame count. Move these counts to data: a table, or values derived once at load and stored sim-side. Make the sim read them without a surface. Rendering must stay identical.
   - **`Map::init`.** Split it into sim-side tile layout, which needs no surface, and render-side bitmap loading and conversion, which moves into the renderer.
   - **Find any other SDL use in sim code:** `SDL_Rect` and other SDL types in entity headers, `SDL_GetTicks`, logging. Aim for **zero SDL includes** in `bitriot_sim`.
     - If a type like `SDL_Rect` is used only for render bookkeeping, move it to the renderer side.
     - If removing SDL entirely proves disproportionate, stop at "no SDL_image/ttf/mixer/RmlUi, and SDL core headers only". Record exactly what remains, because 05b would then need the Emscripten `sdl2` port via `EMCC_LOCAL_PORTS`.
   - **Sound from the sim.** Sim code calls `GameSound`. Keep that behind the existing `MickBaseSound` interface with a null default, so `bitriot_sim` doesn't link SDL_mixer. The game installs the real backend.
2. **CMake targets.**
   - `bitriot_sim`: sim sources only.
   - `bitriot_core`: game, UI and render, linking `bitriot_sim`.
   - `bitriot_tests` links what it needs. Move the sim-only tests to depend on `bitriot_sim` where that is easy.
   - Keep the pinned C++17 (`CMAKE_CXX_STANDARD` in the top-level `CMakeLists.txt`, extensions off) for every target and compiler.
3. **Frame loop.**
   - Replace `consoleMain()`'s busy-wait cap with a `frame()` function. It:
     1. pumps events
     2. adds elapsed real time to an accumulator
     3. steps the sim once per whole `NORMAL_TICKS_SPEED` (33 ms), up to a per-frame cap
     4. renders once
   - A `canStep()` hook (or similar) can veto a step without blocking rendering or event handling. Locally it always allows. Netplay uses it to stall, and to step up to two extra ticks to catch up (07).
   - Native `main()` calls `frame()` in a loop and sleeps for the remainder. The web build (09) calls the same `frame()` from `emscripten_set_main_loop`. Don't add Emscripten code here; just keep `frame()` free of blocking calls.
   - **Menu states keep working.** `MENU_RUNNING` drives RmlUi as today and doesn't step the sim.
   - Input sampling stays where it is for now. 04a moves it to once per stepped tick.
   - **Behaviour must be the same at 33 ms per tick.** A slow frame now catches up, up to the cap, instead of slowing the game. That is an intended change; document it.

## Out of scope

- Emscripten (09).
- Netplay pacing logic (07); only provide the hook here.
- The input refactor (04a).

## Tests

- **The sim tests run with no bitmaps or surfaces:**
  - the harness no longer creates a primary surface for the sim;
  - a test target that links only `bitriot_sim` runs a full round;
  - render-only code paths still get frame counts that agree with the bitmaps, checked by a test comparing the data table against the loaded bitmaps.
- **Frame loop:** a unit test with a fake clock.
  - N × 33 ms of elapsed time gives N steps.
  - Large gaps are capped.
  - `canStep()==false` holds steps without losing accumulated time beyond the cap, and rendering continues.
- The existing suite plus a 200-seed sweep give no change in sim results. 03a's hashes must be unaffected.
- The game target still builds and runs; this needs a play-test, which may be pending at merge.

## Acceptance criteria

- `bitriot_sim` builds without RmlUi, SDL_ttf, SDL_mixer and SDL_image, and ideally without SDL.
- The harness needs no surfaces.
- One `frame()` with an accumulator and a step hook is used by native `main`.
- Rendering is unchanged.
- Record in the Outcome what SDL use, if any, remains in `bitriot_sim`.

## Outcome

**Status: done, play-test pending.** The simulation is its own library, `bitriot_sim`, with **no SDL at all**, and the game runs on one `frame()` with a fixed-tick accumulator. Everything automated passes on GCC and on Clang, the per-tick state hashes of 03a's pinned rounds are **byte-identical** to before, and the rendered world is pixel-identical (pinned by a test: 3 rounds x 5 ticks of the blitted world, no text). The HUD, which contains text, was compared once by hand against the pre-03b code (24 frames, a scratch test that is not committed) and is not pinned. The user should play it once (steps below), mostly for the frame loop's feel and the new controller-rumble path.

**What landed**

1. **The simulation runs with no bitmaps, surfaces or SDL.**
   - **Animation frame counts** (`src/EntityAnim.{h,cpp}`): a table, one row per `EntityType`, read by `GameEntity::incAnimFrame()`. The `ANIM_STATE` enum (idle, up, down, left, right; hashed) moved there from `EntityRenderer`. The renderer still derives its own counts from bitmap widths; `RenderSideTest` checks the two agree for every type, team, and male/female player sheet, and `EntityAnimTest` pins the table to the shipped sheet widths. A bitmap whose width changes needs its table row changed too.
   - **`Map`** is layout only: `init()` builds the border and pillars from `MAP_WIDTH` x `MAP_HEIGHT` and needs no surface; `resize(w, h)` sets the statics and rebuilds only if the size differs. The tile bitmaps and blits moved to `MapRenderer` (`src/MapRenderer.*`, core). **`Simulation::start()` now builds the Map at the config's size** (03a's follow-up), so nobody resizes it first and a config of any size just starts; the engine only resizes the *window* afterwards (`GameEngine::resizeWindowIfMapChanged()`, which also reloads the tile pictures against the new surface). The "Map must already be the config's size" refusal is gone.
   - **Drawing moved out of the sim.** `GameEntity::render(SDL_Surface*)`, `getEntityRenderer()` and `EntityManager::renderEntities()` are gone; `WorldRenderer::renderWorld(EntityManager&, SDL_Surface*)` (core) draws the map, then static entities, then moving ones, reading the simulation through `pixelX()/pixelY()/animState()/animFrame()` and never writing. (It uses its own iterator, not the `DynamicMap` member iterator the old code shared.)
   - **Every other SDL use in the sim** (found by compiling the sim with an include scan, then by the guards below): `Map.h`'s `<SDL2/SDL.h>` and `MickSDLAssets.h`; `GameEntity.h` including `EntityRendererFactory.h` (SDL via `EntityRenderer.h`/`SDLHelper.h`); `PlayerCharacterEntity.h` including `MickSDLUtil.h` for the rumble call; `EntityRenderer::IDLE...` used for the anim state. There was no `SDL_Rect` in an entity header, no `SDL_GetTicks` and no SDL logging in sim code.
   - **Sound.** `MickBaseSound::getInstance()` returns the installed backend or a built-in silent one (never null), so the sim plays sounds without checking and without SDL_mixer; a backend that is destroyed now unregisters itself. `MickSDLSound` still registers itself; the harness's `NullSound` still records.
   - **Rumble.** The one device call left in the sim (`onCollision` rumbling the attached controller) goes through `RumbleSink` (`src/RumbleSink.h`): `rumble(controllerId, strengthPermille, durationMs)`, integer strength, a discarding default. `main.cpp` installs `MickSDLRumbleSink` (`MickSDLUtil.h`), which converts to SDL's float. This also removed the last float literal from a sim file, so the guard's one excused line is gone. Brief 04b still replaces the call with a per-tick `hitThisTick` record; this is only the seam.
2. **CMake targets.** `bitriot_sim` (the `SIM_SOURCES` list: entities, AI, `Map`, `Simulation`, `MatchConfig`, `GameSettings`, `DataReader`, RNG, state hash, `GameSound`, `MickBaseSound`, `RumbleSink`, `EntityAnim`, logging and a little utility code) with include dirs for the project only; `bitriot_core` (everything that draws, plays, reads a device or shows a menu, now including `FrameLoop`, `TickPacer`, `MapRenderer`, `WorldRenderer`) links it PUBLIC; `BitRiot` is `main.cpp` + core. C++17, extensions off, `-Wall -Wno-comment -Wfloat-conversion` for all of it. Tests: **`bitriot_sim_tests`** links `bitriot_sim` and GoogleTest only (no SDL, no RmlUi, no OpenGL) and **`bitriot_tests`** links the game; the simulation tests are one object library linked into both, so `bitriot_tests` still runs every test it ran before. CMake writes the `bitriot_sim` source list to `build/bitriot_sim_sources.txt` for the guards (`file(GENERATE)` from the target's `SOURCES`, so it cannot drift).
3. **Frame loop** (design below): `FrameLoop`, `TickPacer`, `GameEngine`'s `pumpEvents` / `stepTick` / `renderFrame` / `simulating`, `InputHandler::pumpEvents` / `applyInput`, and `frame()` in `main.cpp`.
4. **Guards that now point at the right thing** (02b's follow-ups). The no-float text guard reads the `bitriot_sim` target's sources plus every project header they include, with **no allowlist** (a file that needs a float belongs in core). New object-level check: `SimLibraryTest` runs `objdump -d` on `libbitriot_sim.a` and fails on any floating-point instruction (x86-64: scalar/packed SSE and AVX arithmetic, conversions, compares and x87; aarch64: the `f*` family and `scvtf`/`ucvtf`). A new source scan fails on any SDL, RmlUi or OpenGL `#include`, any of the project's own render/device/UI headers, or any SDL type or function in the sim. And a runtime check that `bitriot_sim_tests` has no SDL library loaded (Linux).
5. **`Map::staticTileAt` bounds** (brief 01's follow-up): a read outside the built map is WALL, a write outside it is ignored, and the row stride is the *built* height rather than the `MAP_HEIGHT` static (so a static changed without a rebuild no longer misindexes).

**What SDL use remains in `bitriot_sim`: none.** Not SDL core headers, not `SDL_Rect`, not types like `Uint32`. 05b and 09 therefore need **no Emscripten `sdl2` port and no `EMCC_LOCAL_PORTS`** for the simulation or the headless binary; only the game (09's local play) needs SDL. What the sim does still touch outside the process, for anyone building it somewhere new:
- **Files, relative to the working directory:** `data/entity.txt`, `data/player.txt` and `data/audio.txt` (`DataReader`; a missing key silently returns 1). `GameSound` also checks that the `assets/audio/*.wav` names in `data/audio.txt` exist and prints to `cerr` if not (it does not throw, and nothing is read from them). It reads no bitmaps. **Correction (review fix round):** `MickAssets::getPortraitBitmapFilename`, which `GameSettings` calls when it builds the roster (before `main`), used to `assert` that `assets/bitmaps/player<N><gender>face.bmp` exists, so a Debug `bitriot_sim` aborted in a directory with no `assets/`. It now only warns. A wasm or headless build needs `data/` (`--preload-file data`), and nothing from `assets/`; `NoAssetsTest` runs a round in a directory that holds only a `data/` link.
- **OS calls:** `MickUtil::CheckFileExists` uses POSIX `access()` (and `PathFileExists`/`shlwapi` on Windows, which `bitriot_sim` now links on MinGW); `std::cout`/`cerr` logging (`MickLogger`); `libstdc++` only otherwise.

**Frame-loop design** (for 04a, 07, 09; the contract is the comment at the top of `src/FrameLoop.h`)

- `FrameLoop(Hooks, tickMs = 33, maxTicksPerFrame = 5)`. Hooks (all `std::function`): `nowMs` (a free-running ms clock), `simulating()`, `step()`, `render()` (returns false to quit) are required; `canStep()` and `pumpEvents()` are optional. `frame()` returns a `FrameResult{keepRunning, ticksStepped}` and **never blocks or sleeps**.
- A frame, in order: `pumpEvents()` -> advance the `TickPacer` by the time since the last frame (or restart it, if `simulating()` is false) -> `while (a whole tick is owed && budget left && simulating() && canStep()) step()` -> (optional `extraTicks()`: up to 2 more steps, same gates; fix round) -> `render()` exactly once. Rendering never steps and stepping never renders. A frame that started outside a round and ends inside one restarts the pacer after `render()`, so round set-up time is not owed as ticks (fix round).
- **`TickPacer`** is pure arithmetic on a ms counter (`restart`, `advance`, `tickDue`, `consumeTick`, `msUntilTickDue`). Sub-tick time is kept. The backlog is capped at `maxBacklogTicks` ticks' worth, which is both the per-frame cap and the most a stall can owe: **a slow frame catches up, up to 5 ticks, instead of slowing the game; a longer stall (window drag, debugger, a netplay wait) is forgiven, not fast-forwarded.** The counter may wrap 2^32. Outside a round the pacer restarts every frame, so time in a menu is never owed afterwards.
- **`canStep()` is the netplay veto.** False holds ticks back without blocking events or drawing; the owed time keeps building up to the cap and is paid back (at most the cap's worth) when it returns true. It is asked before *every* step, so 07 can allow exactly as many ticks as it holds inputs for. "Step up to two extra ticks to catch up" (07) is already what the accumulator does for any owed time within the cap; 07 only needs `canStep()` to say yes. `simulating()` is also re-checked after every step: a step that ends the round stops the frame's remaining steps.
- **`sleepHintMs()`** is what the caller sleeps between frames: until the next tick is due, or 2 ms while a tick is owed but held (so a stall polls rather than spins). Native `main()` does `frame()`, present, then `SDL_Delay(sleepHintMs())`. The web build (09) calls `frame()` from `emscripten_set_main_loop` and drops the sleep; `frame()` is a plain `FrameResult frame()` in `main.cpp`, so 09 wraps it in a `void` callback.
- **The clock** is `clockMs()` in `main.cpp`: SDL's performance counter scaled to ms. `SDL_GetTicks()` on Linux steps 1 ms back and forth at second boundaries (its seconds and nanoseconds are scaled separately), which showed up in the manual run as one frame in thirty arriving a millisecond early with no tick due (86 of 3,914 frames; 0 with the performance counter). An Emscripten build can pass `emscripten_get_now()`.
- **`GameEngine`** (no more `runEngine()`): `pumpEvents()` (during a round: `InputHandler::pumpEvents()`, the SDL poll), `stepTick()` (`InputHandler::applyInput()`, `Simulation::step()`, notes the round end or ESC in `m_roundEnding`), `renderFrame()` (the menu, `GAME_INIT`'s `resetGame()`, or the world and HUD; on the round's last frame it loads the scoreboard first and then draws the final world, as before; the menu takes over next frame), `simulating()` (`GAME_RUNNING` and not ending). `frameHooks(clock)` wires them. ESC seen on a frame whose tick was held back by `canStep()` ends the round in `renderFrame()` without a step (so a stalled game can still be quit); when a tick does run, ESC lets that tick finish, as it always did.
- **Input:** `processKeyboardInput()` was split into `pumpEvents()` (once per frame: reads devices, handles the join selector and hotplug) and `applyInput()` (once per *stepped* tick, throws on ESC). The held-key state persists, so a catch-up frame applies it on each of its ticks, and a sticky release is consumed by the first. Sampling is otherwise where it was; **04a** replaces `applyInput()` with a sampled bitmask handed to `step(inputs)`.
- **Behaviour at 33 ms per tick is the same**, and **one change is intended**: a slow frame now catches up (up to 5 ticks) rather than slowing the game, and the tick rate no longer drifts slow by the sleep's overshoot (the old loop took 33 ms plus overshoot per tick; the pacer keeps the remainder).

**Tests** (`bitriot_tests` 246 -> **296**, `bitriot_sim_tests` **257**; the same on GCC and Clang, Debug and Release; **306 and 258 after the review fix round**, see below)

New (53; the suite also lost three, below):
- `SimLibraryTest` (5 + `SimLibraryRoundTest` 1 + `SoundSeamTest` 1 + `RumbleSinkTest` 3 + `EntityAnimTest` 4, simulation executable too): the SDL/UI source scan and its own positives and negatives; the instruction scan's own cases (x86-64, aarch64, an unsupported architecture is refused) and the real `objdump` of `libbitriot_sim.a`; no SDL library loaded; a 13x11 round with blocks played to its end; sound with no backend, and a destroyed backend unregistering; rumble through the sink (sent for a human with a controller, not for a bot or a player with none, harmless with no sink installed); the anim table against the shipped sheet widths, a frame wrapping at its type's count for every type (a qualified call to the base `incAnimFrame`, the player override separately).
- `MapTest` (6, both): layout, reads outside are WALL, writes outside change nothing, stride and bounds follow the built map, `resize()` rebuilds only on a size change, `clearBlocks()`.
- `SimulationTest` (2 new, one changed): `StartBuildsTheMapAtTheConfigsSize` (a different size, back down, the same size twice) and `StartedOnAMapOfAnotherSizeTheRoundIsTheSameAsOnAPreparedOne` (same config, same 300 hashes whether the Map was pre-sized or `start()` resized it).
- `FixedPointSourceGuardTest.TheGuardReadsTheSimulationAndNotTheRenderer` (simulation files in, renderer files out).
- Core executable only: `TickPacerTest` (8) and `FrameLoopTest` (16) on a fake clock; `RenderSideTest` (5): table against every loaded bitmap, every frame drawn and the next not, drawing twice gives the same pixels and leaves the hash alone, and `TheWorldIsDrawnPixelForPixelAsItWasBeforeTheSplit` (3 pinned rounds x 5 ticks; **the digests were taken from the pre-03b code and the new path reproduces them**).

**Changed tests, and why** (no assertion was loosened; two tests were removed, one moved)
- **Harness** (`tests/support/HeadlessGame.*`, inherited by every `HeadlessGameTest`): no SDL, no surface, no bitmaps. `SetUp()` installs `NullSound` and builds the Map with `Map::init()`; `HeadlessEnvironment::surface()` is gone; `NullSound` gained `makeCurrent()`. Needed because the sim no longer has a surface to be given. `setMapSize()` is unchanged.
- `RoundHygieneTest.GameOverTimerIgnoresWallClockTime`: `SDL_Delay(50)` became `std::this_thread::sleep_for(50ms)` (no SDL in a sim test). Same assertions.
- `SimulationTest`: `startConfig()` and `playHashes()` no longer call `setMapSize()` first, so every test that starts a config now goes through `start()`'s own resize (stricter). `playHashes`'s `resizeMap` parameter went with it (its one `false` caller ran a same-size second start). `StartAppliesTheConfigIntoGameSettings`: only a comment changed. `ARefusedConfigLeavesTheRunningRoundAndTheSettingsAlone`: its refused config is now `makeConfig(5, 1, 21, 15, 30)` (invalid *and* a different size) and it also asserts the Map was not resized; the "wrong Map size" sub-case is gone because that refusal no longer exists.
- **Removed:** `SimulationTest.StartRefusesAMapThatWasNotResizedFirst` (it pinned the contract this brief deliberately removed; `StartBuildsTheMapAtTheConfigsSize` replaces it) and `FixedPointSourceGuardTest.AnAllowlistedLineIsExcusedOnlyInItsOwnFile` (it tested the allowlist's per-line exception, which has no entries and no mechanism now).
- **Moved:** `SimRoundTest.TheScoreboardWinTextDrawsOneCosmeticValueAndNoSimValue` is now `ScoreboardRandomTest.TheScoreboardWinTextDrawsOneCosmeticValueAndNoSimValue`, body unchanged, in the core executable: it calls `ScoreBoardBinder`, which is UI, and `SimRandomTest.cpp` is built into the simulation-only executable.
- `FixedPointSourceGuardTest.NoFloatingPointInSimulationSources`: rewritten to read the `bitriot_sim` source list and header closure. It is **stricter on what it reads in sim code** (no allowlist at all) but **no longer default-deny for a file that is in neither library**: a new `.cpp` is held to the rule only once it is in `SIM_SOURCES`. The object-level check, the SDL scan and the link of `bitriot_sim_tests` cover a sim file mistakenly put in core only partly (a core file may use floats and SDL by design).
- `RmlLayoutHarness.cpp`: `RmlEnvironment` now calls `SDL_Init(0)`/`SDL_Quit()` itself (the harness used to).
- `test_main.cpp` is shared by both executables (`BITRIOT_TESTS_WITH_CORE` adds the RmlUi environment).

**What was shown to fail without the change.** The new APIs do not exist before the change, so the new tests cannot be compiled against the old code; instead each piece was broken in turn on the committed code, the tests rebuilt and run, and the file restored (about 50 mutations).
- **Frame loop** (all fail): backlog cap removed (3 tests), sub-tick time dropped on consume (8), a wrap-unsafe elapsed (2), restart keeping the backlog (1), `canStep` ignored (4), `simulating()` not re-checked after a step (1), the pacer not restarted outside a round (1), render called twice (6), events pumped after the steps (1), no held-tick poll in the sleep hint (1), a one-step budget (6).
- **Anim table:** hard-coded wrap (`AnEntitysFrameWrapsAtItsTypesFrameCount`, and the pixel golden in the core executable), BOMB 4->3 and DRONE 16->8 and two swapped rows (the table tests in both executables, `RenderSideTest`'s three bitmap tests, and for DRONE and the swap the pixel golden). The pixel golden did not see BOMB (no bomb on screen at its sampled ticks); the table-against-bitmap test did.
- **Map:** no bounds check on a write (`AWriteOutsideTheMapChangesNothingInsideIt`), none on a read (`AnythingOutsideTheMapReadsAsWall`, and a second test in the sim executable), `resize()` always rebuilding (2) or never rebuilding (4, including `PinnedRoundsKeepTheirHashes`), the stride taken from the static (1), `start()` not resizing (3, including the golden digests) or resizing before it validates (`ARefusedConfig...`).
- **Seams:** no silent sound default, a destroyed backend left registered, rumble strength changed, rumbling a bot or a player with no controller, the rumble call removed, a null default sink (each fails its `SoundSeamTest`/`RumbleSinkTest`).
- **Guards:** `<SDL2/SDL.h>` added to `Map.h` (`TheSimulationSourcesNameNoSdlRmlUiOrOpenGL`); an SDL *type* in a sim header with no include of its own (that fails to compile: the sim target has no SDL include directory, so `Uint32` is unknown) - but note that an `#include <SDL2/SDL.h>` **does** compile on Linux, because `/usr/include` resolves it; the source scan is the guard for that, not the build; a sim source calling `SDL_GetTicks` (**`bitriot_sim_tests` fails to link**, which is the "links no SDL" proof); a `float` function in a sim source (`NoFloatingPointInSimulationSources` **and** the instruction scan); **a float hidden from the text scan by token pasting (`BITRIOT_CAT(flo, at)`): only the object-level check fails, as it was meant to**; SDL linked into the sim-only executable and called (`TheSimulationOnlyExecutableHasNoSdlOrUiLibraryLoaded`); the scanners' own regexes broken (their unit tests); the include closure returning nothing (3).
- **Render side:** entities drawn in the wrong order, drawing advancing the animation (the second test too: the picture changes between draws), the map not drawn, the player-slot colour instead of the team colour, a sprite one pixel off, the wall and empty tile pictures swapped (the pixel golden each time; the "draw twice" test only for the animation one).
- `ScoreboardRandomTest`: the win text drawing from the sim stream fails it.
- **Cannot fail by construction, or only by luck:**
  - `TheInstructionScan...` and `TheSourceScan...` unit tests only check the scanners, which is their job; the *negative* half of `TheGuardReadsTheSimulationAndNotTheRenderer` (a core file leaking into the list) cannot fail without breaking the build, since core files need SDL headers the sim target does not have.
  - `AnythingOutsideTheMapReadsAsWall`: without the check a read lands on whatever is in memory, which is usually 0 (WALL). It failed in both executables in practice, but the deterministic proof is the write test (a stray write aliases a border wall).
  - Moving `Map::resize` ahead of `tearDown()` in `start()` fails nothing, and need not: brief 01's generation guard already makes the order harmless (an equivalent mutant).
  - The `budget` bound in `FrameLoop::frame()` repeats what the pacer's cap already guarantees (belt and braces; removing it changes nothing).
  - Nothing unit-tests `GameEngine`'s own hooks (the ESC-while-held path, `m_roundEnding`); they were checked by hand (below).

**Golden digests and the sweep**
- **03a's digests are unchanged.** `BITRIOT_HASH_DUMP=all` of the four pinned rounds (13,211 per-tick hashes) is byte-for-byte identical to the dump from `claude`, on GCC Debug, GCC Release, Clang Debug and Clang Release; `PinnedRoundsKeepTheirHashes` passes on all four, with the digests in `GOLDEN_ROUNDS` untouched. The real game reproduces them too (manual check).
- **Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, from the build directory, each against a clean build of its own tree):
  - before (`claude`, 246 tests): **0 failing seeds** (a clean build of `claude`)
  - after, `bitriot_tests` (296): **0 failing seeds**
  - after, `bitriot_sim_tests` (257): **0 failing seeds**
  - The first after-sweep found seeds 41, 45 and 52 failing: my own `SimLibraryRoundTest` asserted more than 20 blocks on a 13x11 map at 55%, which those seeds do not give (19). It now asserts more than 5; the sweep above is from the fixed test. The known bot livelock (`AISeekEnergyTest.HealthyBotWithAnEggUnder...`, seed 4652 alone) is untouched and still in `TODO`: `BITRIOT_TEST_SEED=4652` fails it identically ("bot never got out, stuck at (1,1)") on `claude` and on this branch.
- AddressSanitizer + UBSan build of both executables (the recipe in `tests/README.md` with `,undefined` added): clean, 296 and 257 pass, no reports.
- GCC 13.3 and Clang 18.1, Debug and Release: 296 / 257 pass on all four (the object-level float scan passes on every build; the test binary was run from the source root).

**Manual check of the real game** (`GameEngine` cannot run headlessly). A scratch copy of the tree (not committed) was built with an env-var hook in `GameEngine`/`main.cpp` (`BITRIOT_AUTOPLAY=<seeds>` starts all-AI 2-player 13x11 55%-block rounds from the menu state with the listed pinned seeds, logging every tick's state hash and the scoreboard length; `BITRIOT_ESC_AT`, and `BITRIOT_VETO` to make `canStep()` return false for a number of frames) and run under `xvfb-run` with `SDL_AUDIODRIVER=dummy`.
- **Menu -> round -> scoreboard -> next round works.** Seeds `4242,4242,0`: rounds 1 and 2 each ended after exactly **1146 ticks**; **all 1,147 hashes of each (tick 0 to 1146) are identical to the harness's** for the same config (the `two bots, 13x11` golden round, which is also identical to `claude`'s), and to each other. The first round resized the window and the Map from 17x13 to 13x11 through the new path (`Simulation::start()` builds the Map, then the engine resizes the window). The scoreboard length passed to `loadScoreBoard` was 37 s (1146 x 33 / 1000). Round 3 (a fresh seed) ended after 1613 ticks (53 s). Exit status 0. A screenshot of the running world (map, blocks, players, HUD) and of the real RmlUi scoreboard looked right.
- **The frame loop holds the tick rate.** 300 ticks took 9,900 ms in every 300-tick window (33.00 ms per tick, from `SDL_GetPerformanceCounter`); under Xvfb's software renderer a few frames ran 2 or 3 ticks to catch up. (With `SDL_GetTicks()` as the clock, 86 of 3,914 frames arrived a millisecond early with no tick due; with the performance counter, none: the reason for `clockMs()`.)
- **ESC mid-round:** pushed after tick 200, seen on tick 201, the tick still ran, the scoreboard showed 6 s (201 x 33 / 1000), and the next round (the same seed) then ran its full 1146 ticks with the same hashes, so a quit round leaves nothing behind.
- **A held tick:** with `canStep()` false for 40 frames (about 240 ms) at tick 300, the world kept drawing (40 frames with no step), then the loop ran **exactly 5 ticks** in one frame (the cap) and was back on pace; the round still ended at 1146 ticks with the same hashes and a 37 s scoreboard, so pacing never touches the simulation. ESC pushed while `canStep()` held the tick (veto never released): the round ended on that frame with the scoreboard at tick 300, no extra step.
- Not exercised: clicking through the real menu (the hook starts rounds from the menu state), controller rumble and hotplug, the join selector, the Options map-size change by hand. Those are the play-test.

**Deviations from the brief**
- The simulation has **no SDL at all** (the brief's stretch goal), so there is no "SDL core headers only" fallback to record.
- `FrameLoop` and `TickPacer` are in `bitriot_core`, not `bitriot_sim` (they are not simulation); they are SDL-free and unit-tested with a fake clock. The brief said "a `canStep()` hook (or similar)": it is a `std::function` in `FrameLoop::Hooks`.
- `GameEngine::runEngine()` is gone rather than kept as a wrapper; `main.cpp` calls `frame()`, which is the shared entry point.
- The frame loop uses SDL's performance counter, not `SDL_GetTicks()` (reason above).
- The test executable was split in two (`bitriot_sim_tests`, `bitriot_tests`) rather than moving only some tests to `bitriot_sim`: the brief's "move sim-only tests where easy" turned out to be *all* of them, so both executables run them and the old binary name and sweep command keep working.
- `Map::staticTileAt` bounds (brief 01's follow-up) and rumble through `RumbleSink` (the rumble line was the last SDL call in the sim) were done here though not listed in 03b's scope; each is a few lines. `InputHandler::processKeyboardInput()` was split into `pumpEvents()`/`applyInput()`, which the brief's "input sampling stays where it is" allows (the sampling is where it was; only the poll moved to once per frame).
- `FixedPointSourceGuardTest` lost its allowlist (see changed tests) and two tests were removed or moved.
- The README's D6 note, `tests/README.md` and code-facts were updated (§1 loop rewritten; §2, §4, §9, §10, §12 corrected). code-facts had no factual errors about the code that blocked this work; its statement that the harness "still needs EntityRendererFactory surfaces" is now history.

**Review fix round** (the Opus review said "merge after fixes"; golden digests untouched: `BITRIOT_HASH_DUMP=all` is still byte-identical to `claude`'s)

1. **`BITRIOT_BUILD_GAME` (default ON).** OFF skips `find_package` for SDL2, SDL2_mixer/ttf/image, OpenGL and RmlUi and the `bitriot_core`, `BitRiot` and `bitriot_tests` targets, and still builds `bitriot_sim` and `bitriot_sim_tests` (GoogleTest is still needed). Checked in an environment where none of those packages can be found: configured with `-DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON` (and `_SDL2_mixer`, `_SDL2_ttf`, `_SDL2_image`, `_RmlUi`, `_OpenGL`) the default (game ON) configure **fails** at `find_package`, and the same flags with `-DBITRIOT_BUILD_GAME=OFF` configure and build (`libbitriot_sim.a`, `bitriot_sim_tests`), `compile_commands.json` has no SDL, RmlUi or GL path, `bitriot_sim_tests` has no SDL library in `ldd`, and its 258 tests pass (`ctest` too). The 05 and 05b follow-ups above say how to use it.
2. **`extraTicks()`** (`FrameLoop::Hooks`, `FrameResult::extraTicksStepped`): see the 07 follow-up. Nine unit tests (bounded at 2, asked once after the owed ticks, runs when the clock owes nothing, does not touch the backlog, gated by `canStep()` and `simulating()`, absent/negative/zero/disabled mean none).
3. **No burst after a slow round set-up.** A frame that starts outside a round and ends inside one (`GAME_INIT`: window resize, bitmaps, HUD) now restarts the pacer after `render()`, so the set-up time is not owed as 2-5 ticks on the first running frame. A frame that stays a menu is untouched. Two tests.
4. **No world redraw when nothing changed.** `GameEngine::renderFrame()` draws the world and HUD onto the CPU surface only if a tick ran, the round is ending, an event was read this frame (`InputHandler::pumpEvents()` now says so), or the surface is new; the surface is still pushed to the texture every frame (the renderer's back buffers alternate and each needs the picture). In the manual run (below) 60 held frames did no redraw.
5. **Per-frame counters** (`m_ticksThisFrame`, the quiet-pump flag) are cleared at the end of `renderFrame()`, not in `pumpEvents()`; a frame whose pump did not run counts as not quiet. So replacing the pump hook cannot leave a stale count behind (it loses the redraw skip and ESC-while-held, which the 07 follow-up says to avoid by wrapping).
6. **The float text guard also reads `InputHandler.{h,cpp}` and `GameEngine.{h,cpp}`** until 04a (`coreFilesThatStillMutateTheSimulation()`); the 04a follow-up says to remove the list.
7. **aarch64 pattern narrowed**: the f-family is listed (arithmetic, compare, convert, round), and `fmov` is flagged only with an immediate operand, because GCC uses `fmov dN, xM` for integer vector moves. Explained in the test's comment; **untested against real GCC aarch64 output**.
8. **Overclaims fixed** (the Status paragraph's HUD claim, the "fails the build" claim for an SDL include, code-facts' stale `GameEngine.cpp:166-172` reference), and the Outcome's "nothing from `assets/`" claim, which was false for Debug builds (the portrait `assert`, removed; `NoAssetsTest`).
9. **Test executables find `data/` themselves** (the `DataReader` failure check runs only in `HeadlessGameTest` fixtures, not in plain `TEST`s). `data/` and `assets/` links into the build directory are now a target (`bitriot_runtime_links`) that `BitRiot` and both test executables depend on, so building only `bitriot_sim_tests` leaves a build directory it runs from (before: it died on the portrait `assert`, because the links came from `BitRiot`'s POST_BUILD). The harness refuses to start if `data/entity.txt`, `player.txt` or `audio.txt` cannot be read, and fails any test during which a `DataReader` lookup failed (`DataReader::errorCount()`; a missing file or constant used to answer 1 silently). Shown by running from a directory with no `data/` (a clear message, exit 2), and from one with `BOMB_FUSE` deleted from `data/entity.txt` (the failing tests say "Constant: BOMB_FUSE not found in data/entity.txt!").
10. **04a note** on the input quirks over a longer window: in the 04a follow-up.

Tests: `bitriot_tests` **306**, `bitriot_sim_tests` **258** (up from 296 and 257: nine `FrameLoopTest`s and `NoAssetsTest`). Changed setups: the harness (the data check above, so a test that now makes a failed `DataReader` lookup fails; none did), `FixedPointSourceGuardTest.NoFloatingPointInSimulationSources` (reads four more core files), `TheInstructionScanFlagsFloatingPointAndLeavesIntegerCodeAlone` (the aarch64 half has more cases; its x86-64 half is unchanged), `TheGuardReadsTheSimulationAndNotTheRenderer` (checks the extra files are in the guarded set and not in the simulation's own list; `GameEngine.cpp` moved from "not read" to "read"). Shown to fail without each change: the pacer restart after a menu-to-round frame, the unbounded/ungated/clock-consuming extras, a guard list that is empty or an `InputHandler.cpp` or `GameEngine.cpp` with a `float`, the old any-`f`-mnemonic and never-`fmov` patterns, the portrait `assert` back (the Debug test binary aborts). The engine's own change (4, 5) has no unit test; it was checked in the real game under xvfb (below).

Sweep (200 seeds, `BITRIOT_TEST_SEED=1..200`, from a clean GCC Debug build of the fix-round code): `bitriot_tests` (306) **0 failing seeds**, `bitriot_sim_tests` (258) **0 failing seeds**. GCC and Clang Debug both pass 306 / 258 and reproduce the golden dump byte for byte; so do a `BITRIOT_BUILD_GAME=OFF` GCC and Clang build (258 tests, golden dump from `bitriot_sim_tests` identical).

Manual check of the fix round (the same scratch hook as before, plus `BITRIOT_EXTRA` to feed `extraTicks`; under xvfb, four runs in parallel):
- A normal round (seed 4242): 1,146 ticks, every frame stepped, 1,146 redraws, scoreboard 37 s, **all 1,147 hashes equal the harness's** (and so `claude`'s).
- A held tick (`canStep()` false for 60 frames at tick 300): 60 frames with no step, **0 world redraws** on them, then catch-up, 1,146 ticks, **hashes equal the harness's**.
- ESC while held (veto never released, ESC pushed after 30 held frames): the scoreboard at tick 300, no extra step; those 30 frames did no redraw, and the final frame did.
- `extraTicks()` returning 2 for five frames: ten ticks earlier than the clock, the round took 1,136 frames instead of 1,146, **same 1,146 ticks, same hashes**.

**Follow-ups**
- **04a (input sampling and `step(inputs)`):**
  - `applyInput()` is the thing to replace: sample once per *stepped* tick from the held/released state `pumpEvents()` accumulates (that is already how a catch-up frame behaves). Keep `pumpEvents()` where it is, at the top of the frame.
  - `GameEngine::stepTick()` is where `step(inputs)` goes, in place of `applyInput()` then `m_sim.step()`.
  - The ESC path (`InputException(GOT_QUIT)` out of `applyInput()`) is local and should stay in the engine; `renderFrame()` also treats a seen ESC as a quit when no tick ran, so keep that in sync if the quit flag moves.
  - Moving input into the sim changes the hash and the golden digests, as 03a said; the pixel golden digests are unaffected.
  - **Remove `coreFilesThatStillMutateTheSimulation()`** (`tests/support/SimSources.h`) when input moves: until then `FixedPointSourceGuardTest` also scans `InputHandler.{h,cpp}` and `GameEngine.{h,cpp}` for floating point, because they are in `bitriot_core` but still move players and drive the tick. Once input is a sampled bitmask they are presentation code again.
  - **Two input quirks that a longer sampling window makes more visible** (code-facts §6 has the one-tick version): (1) on a catch-up frame (several ticks owed) a key *release* seen in the window applies on the first tick, so that tick's last held step is lost; (2) a tap pressed *and* released between two applies loses the press but keeps the release pending. 04a's sampler should decide both deliberately (a per-tick edge record, not held/released flags).
  - `GameEngine::pumpEvents()` is also what makes ESC work while `canStep()` holds the tick back, and what lets `renderFrame()` skip a redraw: keep it called every frame.
- **04b (control events, rumble):**
  - `onCollision` still calls `RumbleSink::getInstance()->rumble(attachedController, 500, 200)`. Replace it with the per-tick `hitThisTick[slot]` and rumble from the local layer after `stepTick()`; then `RumbleSink` and `MickSDLRumbleSink` can go (and `attachedController` out of the entity).
  - The join selector and hotplug run inside `pumpEvents()` (once per frame, possibly with no tick), and mutate `m_controlledByAI`/`attachedController` directly; 04b's `ControlEvent`s must be applied at a tick, not at the pump.
- **05 (replay, `bitriot_headless`):**
  - The headless binary links `bitriot_sim` only, builds the `MatchConfig`, and loops `Simulation::step()`; it never needs `FrameLoop`. `bitriot_sim_tests` is its template (the harness's `NullSound` is optional: the default is silent).
  - It needs a working directory with `data/` (see "What SDL use remains"), and nothing from `assets/`. Run without `assets/` it logs about a dozen "Portrait bitmap not found" warnings at start (`GameSettings` builds the portrait names at static initialisation); harmless.
  - **Configure it with `-DBITRIOT_BUILD_GAME=OFF`** (review fix round): that skips `find_package` for SDL2, SDL2_mixer/ttf/image, OpenGL and RmlUi and the `bitriot_core`, `BitRiot` and `bitriot_tests` targets, and still builds `bitriot_sim` and `bitriot_sim_tests` (GoogleTest is still required for those). The `bitriot_headless` target should be added under the same guard. Checked in an environment where those packages cannot be found (see the fix round).
  - `Simulation::start()` builds the Map itself, so no setup beyond the config.
  - `bitriot_sim_sources.txt` lists the library's files if 05 wants to assert its own link line.
- **05b (cross-platform gate):**
  - `bitriot_sim` needs **no SDL and no Emscripten `sdl2` port**; build it with `-fexceptions` or `-fwasm-exceptions` (the sim throws `std::logic_error`/`invalid_argument`). **Configure with `-DBITRIOT_BUILD_GAME=OFF`** (`emcmake cmake ... -DBITRIOT_BUILD_GAME=OFF`, or an aarch64 sysroot without SDL/RmlUi): without it the top-level `find_package(SDL2|SDL2_mixer|SDL2_ttf|SDL2_image|OpenGL|RmlUi REQUIRED)` fails at configure before `bitriot_sim` is reached. (The first version of this brief's Outcome said the sim needed no SDL but did not mention that CMake still asked for it.)
  - `SimLibraryTest`'s object-level float check supports x86-64 and aarch64; on any other architecture it **fails, not skips**, with a message to add patterns. For wasm the check would have to read `wasm-objdump` output (f32/f64 opcodes); that gate is yours, or run the check on the x86-64 and aarch64 builds and trust the text guard for wasm. The aarch64 patterns were narrowed in the fix round (an `fmov` between a general and a SIMD register is not flagged; `fmov` with an immediate is) and are **untested against real GCC aarch64 output**: the first aarch64 run may need them adjusted. If you build only the library under qemu, `BITRIOT_SIM_LIBRARY`/`BITRIOT_COMPILER_TARGET_ARCH` come from CMake, so the test binary must be built for the same arch as the archive it reads (or the archive is read with a cross `objdump`; the test calls plain `objdump`).
  - Run `BITRIOT_HASH_DUMP=all` on each target and compare with the dump from `claude` byte for byte; the four builds checked here (GCC and Clang, Debug and Release, x86-64) all match.
- **07 (lockstep):** set `FrameLoop::Hooks::canStep` (via `GameEngine::frameHooks()`'s returned hooks, before constructing the `FrameLoop` in `main.cpp`) to "I hold the host's inputs for the next tick". It is asked before every step, so it may allow fewer ticks than are owed; owed time beyond 5 ticks is forgiven, so a long stall does not turn into a burst. **To catch up (review fix round), set `FrameLoop::Hooks::extraTicks`**: it returns how many steps beyond the clock's you want this frame (a peer holding more than D+2 confirmed ticks: ask for 2), is asked once per frame after the owed ticks, is clamped to `maxExtraTicksPerFrame` (2, a constructor argument), and each extra step still needs `simulating()` and `canStep()`. Extras do not touch the pacer's backlog, so a frame runs at most 5 + 2 steps. Without it, a stall of more than the 5-tick cap could never be recovered, because the clock forgives the rest. Pump the network in `pumpEvents()` or in `canStep()` itself, not in `step()`; if you replace `GameEngine::pumpEvents()` rather than wrapping it, ESC stops working while the tick is held and `renderFrame()` redraws every frame (it treats a frame whose pump did not run as not quiet), so **wrap it, don't replace it**. While a tick is held `frame()` runs about every 2 ms (`sleepHintMs()`), but `renderFrame()` no longer redraws the world or HUD on those frames (it only re-pushes the CPU surface to the texture, which the renderer's alternating back buffers need), so a stall costs a texture upload per frame, not a world render. A "waiting for <name>" overlay drawn into the CPU surface must call `GameEngine::requestRedraw()` whenever it appears, changes or goes away (it forces the next `renderFrame()` to redraw the world and HUD under it); or draw the overlay with the SDL renderer after `pushCpuBufferToHardwareBuffer()`, which needs no redraw. (a) While `canStep()` holds, `sleepHintMs()` is 2 ms and the renderer has no `PRESENTVSYNC`, so a stall still re-uploads the texture and presents at up to about 500 Hz: when 07 lands, use a longer held-poll interval (for instance 8-16 ms) or skip the push and present on quiet frames, which would need `frame()` to tell `main.cpp` that nothing was drawn (the back buffers only alternate if something is presented). (b) `extraTicks()` is asked once per frame, so catch-up speed scales with the frame rate: 09's 60 Hz `requestAnimationFrame` catches up about twice as fast as the native loop's 30 Hz frames; ask for fewer extras there if that matters. (12, browser netplay, inherits both.) The scoreboard length is ticks x 33 ms, so it needs no change.
- **09 (WASM):**
  - `main.cpp`'s `frame()` is the function to hand to `emscripten_set_main_loop`, via a `void` wrapper that ignores `FrameResult` except `keepRunning`. Drop the `SDL_Delay(sleepHintMs())` in the loop.
  - A clock from `emscripten_get_now()` replaces `clockMs()`.
  - `MickSDLRumbleSink` and the SDL controller code are core, not sim, so the wasm build can leave them out.
  - A binary run without `assets/` logs about a dozen "Portrait bitmap not found" warnings at start (`GameSettings` builds portrait names at static initialisation); harmless.
  - `bitriot_sim` builds as it is; configure with `-DBITRIOT_BUILD_GAME=OFF` for any wasm target that has no SDL port (the game itself, with the SDL port, uses the default ON).
  - `renderFrame()` skips the CPU redraw on a frame with no tick, no event and no round change, so a 60 Hz `requestAnimationFrame` between 30 Hz ticks pushes the unchanged surface rather than redrawing it.
  - `frame()` has no blocking call; `GameEngine::renderFrame()` calls `menuSystem->showMenu()`, which already returns each frame.
- **Not done, found on the way:**
  - *(Done in the fix round.)* A frame with no tick used to redraw the whole world; `renderFrame()` now skips the CPU redraw when nothing changed (see below).
  - `EntityRendererFactory::initSurfaces()` does not check that the loaded bitmap widths agree with `EntityAnim`'s table (only the test does). A modder who changes a sheet's width gets a wrapped or hidden frame, not an error; a warning at load would be a one-liner.
  - The scoreboard window on the Xvfb run is cropped at the right on a 13x11 map (the menu background is a fixed size); it looks the same before this change as far as the code goes, but it was not compared.

**Play-test** (pending; needs a display, so it was not run in the cloud container). The frame loop, the Map/window resize path and the rumble path changed, so the user should play it. The commands are untested on Arch (the container is Ubuntu); they are 03a's with this branch. RmlUi 4.3 is not packaged for Arch, so it is built from source as `scripts/cloud-setup.sh` does, with the two flags Arch's newer tools need (CMake 4.x needs a policy minimum, GCC 15 no longer pulls in `<cstdint>`).

```bash
# one-time: dependencies
sudo pacman -S --needed base-devel cmake git sdl2 sdl2_mixer sdl2_ttf sdl2_image freetype2 mesa
git clone --depth 1 --branch 4.3 https://github.com/mikke89/RmlUi.git /tmp/RmlUi
cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_SHARED_LIBS=ON -DBUILD_SAMPLES=OFF -DBUILD_TESTING=OFF -DBUILD_LUA_BINDINGS=OFF \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint"
cmake --build /tmp/RmlUi/build -j"$(nproc)" && sudo cmake --install /tmp/RmlUi/build && sudo ldconfig

# AFTER: this branch (it is on origin once pushed; use `claude` after the merge)
git clone --branch netplay/03b-sim-lib-frame-loop https://github.com/VenKamikaze/BitRiot.git ~/bitriot-after
cmake -S ~/bitriot-after -B ~/bitriot-after/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-after/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-after/build && ./BitRiot)

# BEFORE (for comparison): the commit before this branch, 2c2dc07 (03a merged)
git clone https://github.com/VenKamikaze/BitRiot.git ~/bitriot-before
git -C ~/bitriot-before checkout 2c2dc07
cmake -S ~/bitriot-before -B ~/bitriot-before/build -DCMAKE_BUILD_TYPE=Release -DBITRIOT_BUILD_TESTS=OFF
cmake --build ~/bitriot-before/build -j"$(nproc)" --target BitRiot
(cd ~/bitriot-before/build && ./BitRiot)
```

`./BitRiot -b` plays on the big 25x19 map; `-f` asks for fullscreen. Play with at least one human and several bots, with the spawner on, and compare with the BEFORE build where useful. Nothing the player sees should change except the frame-loop point in item 2. Check 03a's seven, then the new ones:

1. **Rounds start and end normally** (PvP and, if you use it, co-op).
2. **The 2 s countdown.** After the last opponent dies the game keeps running for 2 s before the scoreboard.
3. **The scoreboard.** Winner, rows, a plausible game length (about 30 ticks a second).
4. **ESC mid-round** goes to the scoreboard; the menu works afterwards.
5. **"Play again"**, twice in a row: a fresh round each time, nothing left over, the countdown the full 2 s.
6. **A map-size change between rounds** (Options: change the map size and the player count; start; check the window, panel and walls; play again; change it back). This is the path that changed most: the Map is now built by the simulation and the window follows.
7. **AI takeover via the join selector**, including during the countdown, on a controller and on the keyboard.
8. **Speed.** The game should feel exactly as fast as before (about 4 tiles a second for a fresh player along a corridor). Count it against the BEFORE build if in doubt.
9. **A stall.** During a round, drag the window by its title bar for a few seconds, or pause the process (`kill -STOP` then `-CONT`). BEFORE: the game just continues from where it was. AFTER: it should do the same, with at most a very brief extra rush (a handful of ticks) on release, never a long fast-forward.
10. **Controller rumble** (if you have a pad): get hit by an explosion or a monster as the human player on the pad; it should rumble as before (it now goes through `RumbleSink`). Unplug the pad mid-round: the player reverts to a bot, as before.

Record the result here when it comes back.

**Play-test result (29 Oct 2026, user, Arch Linux):** went fine. Two rounds, one keyboard player against three CPU players, on `claude` at or after f430398 (02b–05 and vnet 14 merged). The frame loop felt right. **Still pending:** controller rumble and hotplug, the join selector, and the Options map-size change (no controller was used).
