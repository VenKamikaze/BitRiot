# 03b — Surface-free sim library and frame loop

- **Repo:** BitRiot
- **Branch:** `netplay/03b-sim-lib-frame-loop`
- **Depends on:** 03a
- **Status:** todo

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
   - Keep C++14 for the game targets.
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

*(fill in when done)*
