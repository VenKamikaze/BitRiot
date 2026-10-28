# 09 — WASM port (local play)

- **Repo:** BitRiot
- **Branch:** `netplay/09-wasm`
- **Depends on:** 03 is recommended (its sim/render split makes the main-loop change clean). It is independent of 04–08.
- **Status:** todo

## Goal

Bit Riot builds with Emscripten and runs in a desktop browser, with menus, local play (keyboard and gamepads), sound and AI. There is no networking yet (brief 12). The native build is unaffected.

## Read first

- [`research/code-facts.md`](research/code-facts.md):
  - §1: the loop and exceptions as control flow.
  - §9: rendering, RmlUi glue, audio, filesystem. Note the corrections: the `#error` tests `SDL_VIDEO_OPENGL`, and the RmlUi glue does **not** use GL.
  - §11: debug logging cost.
- `main.cpp`, `src/MickSDLRenderer.cpp`, `src/MenuSDLRenderer.cpp`, `src/RmlUI/glue/*`, `src/MickSDLSound.cpp`, `CMakeLists.txt`, `cmake/` (FindRmlUi), `scripts/cloud-setup.sh`.

## In scope

1. **Toolchain**
   - Add emsdk installation to `scripts/cloud-setup.sh`, either behind a flag or as a separate `scripts/setup-emsdk.sh`, and pin an emsdk version.
   - Check the cloud environment's network policy allows emsdk's downloads. If it doesn't, stop and tell the user which hosts are needed.
   - Build with `emcmake cmake -S . -B build-wasm && cmake --build build-wasm`.
2. **Ports and flags**
   - `-sUSE_SDL=2`
   - `-sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS='["png"]'`
   - `-sUSE_SDL_TTF=2`
   - `-sUSE_SDL_MIXER=2` (WAV only)
   - `-sUSE_FREETYPE=1`
   - `-fwasm-exceptions`, or `-fexceptions` if that proves more compatible. There are 16 throw sites, and ESC uses exceptions (brief 04 may have removed that use).
   - `-sALLOW_MEMORY_GROWTH=1`
   - Use no pthreads, so there are no COOP/COEP header requirements.
3. **RmlUi**
   - Cross-build RmlUi 4.3 from the `ext/RmlUi/RmlUi` submodule as a static library inside the wasm build, using its freetype port.
   - Alternatively, if upgrading RmlUi to 5.x/6.x is clearly easier (the TODO lists "bump RmlUi" as its top item), propose it to the user first and don't do it unilaterally. It touches all menu code.
   - The debugger is only included in `!NDEBUG` builds, so the wasm build should be Release or should drop RmlDebugger.
4. **Remove the GL dependency**
   - Remove both `#error` guards and the `find_package(OpenGL)`/`OPENGL_LIBRARIES` use. The glue uses `SDL_Renderer` only.
   - Check `SetScissorRegion`'s Y-flip (`RenderInterfaceSDL2.cpp` ~:102), which is probably wrong for `SDL_RenderSetClipRect`. Fix it if the menus clip wrongly; this affects native too.
5. **Main loop inversion**
   - On Emscripten, `main()` sets up, then calls `emscripten_set_main_loop(frame, 0, 1)` so frames follow requestAnimationFrame.
   - `frame()` runs a **fixed-tick accumulator**: step the sim once per elapsed 33 ms (with a cap per frame), then render once. Rendering is no longer one-to-one with sim ticks.
   - Native can keep its loop, but should share the same `frame()` and accumulator, so behaviour matches and the lockstep pacing in 12 has one code path.
   - Remove the `SDL_Delay` busy-wait on web.
   - Quitting moves teardown into a function and calls `emscripten_cancel_main_loop()`.
6. **Filesystem**
   - Preload with `--preload-file data@/data --preload-file assets@/assets` and run with cwd `/`.
   - Nothing is written, so IDBFS isn't needed. Brief 08's settings file will need IDBFS or localStorage later; note that for 12.
7. **Audio**
   - Browsers block audio until the user interacts with the page. Resume or open the audio device on first input, for example the first key press or click on the title screen, and make sure nothing breaks before then.
8. **Input**
   - Keyboard works through SDL.
   - Gamepads come through SDL's Emscripten joystick backend, which uses the browser Gamepad API. Gamepads appear only after a button press, so check the "press START to join" flow handles that.
   - Haptics are probably unsupported on web. Make sure rumble calls fail quietly.
9. **Logging:** default to info level on web. The debug-per-frame `cout` output is expensive in the browser console.
10. **Shell page**
    - A minimal `web/shell.html`: canvas, loading progress, and a "click to start" overlay that also unlocks audio.
    - Output goes to `build-wasm/` as `.html/.js/.wasm/.data`.
    - Document how to serve it locally, e.g. `python3 -m http.server`.

## Out of scope

- Networking (12).
- Hosting or deployment (GitHub Pages can come later, after asking the user).
- Mobile and touch controls.
- Persistent settings (with 08/12).

## Tests

- **CI-able build:** the wasm build succeeds, and the existing native suite still passes. Native behaviour is unchanged apart from the shared accumulator; run the 200-seed sweep.
- **Smoke test with Playwright** (Chromium is pre-installed; `require('playwright')` works and nothing needs installing):
  - serve `build-wasm/`
  - load the page and click start
  - screenshot the main menu, and assert it rendered (non-blank canvas)
  - start a local all-AI match through some test hook, such as a URL flag `?autostart=ai4` handled in `main`
  - assert that the sim advanced N ticks with no exceptions in the console

  Commit the script under `tests/web/`.
- **Golden replay in wasm (D6 gate):** if brief 05 has landed, run `bitriot_headless` built for wasm under Node and replay the golden record. It must match the native checkpoint hashes. If 05 hasn't landed, leave this to brief 12 and say so.

## Acceptance criteria

- `build-wasm/` serves a playable game: menus, local multiplayer, AI and sound after a click. The user play-tests it in a desktop browser.
- The native build and tests are unaffected.
- The Playwright smoke test is committed and passing.
- The build steps are documented in the README.

## Outcome

*(fill in when done)*
