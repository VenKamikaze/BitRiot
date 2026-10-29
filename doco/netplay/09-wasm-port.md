# 09 — WASM port (local play)

- **Repo:** BitRiot
- **Branch:** `netplay/09-wasm`
- **Depends on:** 03b (shared `frame()` loop and the `bitriot_sim` split). Independent of 04–08.
- **Status:** todo

## Goal

Bit Riot builds with Emscripten and runs in a desktop browser: menus, local play with keyboard and gamepads, sound and AI. No networking yet (that is brief 12). The native build is unaffected.

## Read first

- [`research/code-facts.md`](research/code-facts.md). Note the corrections it makes: the `#error` guard tests `SDL_VIDEO_OPENGL`, and the RmlUi glue does **not** use GL.
  - §1: loop, and exceptions used as control flow.
  - §9: rendering, RmlUi glue, audio, filesystem.
  - §11: debug logging cost.
  - §12: `throw new runtime_error`.
- [`README.md`](README.md): the cloud network limits.
- Brief 03b's Outcome: `frame()`, and what SDL use remains in the sim. If 05b has landed, also its Outcome, which covers emsdk setup.
- `main.cpp`, `src/MickSDLRenderer.cpp`, `src/MenuSDLRenderer.cpp`, `src/RmlUI/glue/*`, `src/MickSDLSound.cpp`, `CMakeLists.txt`, `cmake/` (FindRmlUi), `scripts/`.

## In scope

### 1. Toolchain

- Use emsdk, pinned to a version, set up through `scripts/setup-emsdk.sh`. Reuse it if 05b created it.
- **Reuse vnet's pin.** vnet already pins emsdk in its `scripts/setup-emsdk.sh` (emsdk revision `d7b4130688bfa348392b921ab04015aa15c4f154`, Emscripten **6.0.13**; installed with git, binaries from storage.googleapis.com). Use the same revision and version here so BitRiot and vnet are built by one compiler.
- Build with `emcmake cmake -S . -B build-wasm && cmake --build build-wasm`.

### 2. Ports

Use `-sUSE_SDL=2`, `-sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS='["png"]'`, `-sUSE_SDL_TTF=2`, `-sUSE_SDL_MIXER=2` with WAV only, and `-sUSE_FREETYPE=1`.

- **For SDL_mixer**, set `-sSDL2_MIXER_FORMATS=[]` or the equivalent, so a WAV-only build doesn't pull in ogg and vorbis.
- **Port downloads.** Emscripten fetches its ports from GitHub archive URLs. Those may be blocked; check as the README describes.
  - If they are blocked, git-clone each port's source at the exact tag or commit that your emsdk version's `tools/ports/*.py` expects, and set `EMCC_LOCAL_PORTS=sdl2=<path>,sdl2_image=<path>,…`.
  - Cover the whole dependency set: SDL2, SDL2_image (with libpng and zlib), SDL2_ttf (with FreeType and HarfBuzz), SDL2_mixer, and FreeType for RmlUi.
  - Script this in `setup-emsdk.sh`.
- **Port sources are provided (verified Oct 2026).** In the cloud container, GitHub `/archive/` URLs return 403, so six of Emscripten 6.0.13's port downloads are blocked:

  | Port | URL Emscripten fetches | File on branch `emscripten-ports` |
  |---|---|---|
  | sdl2 | `https://github.com/libsdl-org/SDL/archive/release-2.32.10.zip` | `release-2.32.10.zip` |
  | sdl2_image | `https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-2.6.0.zip` | `SDL_image-release-2.6.0.zip` |
  | sdl2_ttf | `https://github.com/libsdl-org/SDL_ttf/archive/release-2.20.2.zip` | `SDL_ttf-release-2.20.2.zip` |
  | sdl2_mixer | `https://github.com/libsdl-org/SDL_mixer/archive/release-2.8.0.zip` | `SDL_mixer-release-2.8.0.zip` |
  | freetype | `https://github.com/freetype/freetype/archive/VER-2-14-3.zip` | `freetype-VER-2-14-3.zip` |
  | zlib | `https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz` | `zlib-1.3.2.tar.gz` |

  harfbuzz (a GitHub *release* download) and libpng (storage.googleapis.com) download fine.

  - **Where the files are.** The user pushed these six archives to the orphan branch **`emscripten-ports`** of this repo (ab564ce, with a `SHA512SUMS` file). Every one was checked against the `HASH` in Emscripten 6.0.13's `tools/ports/<name>.py` and matches byte for byte.
  - **How to make Emscripten use them.** Placing the archive in the cache is **not** enough: `fetch_project` deletes it and downloads again. Instead, for each port:
    1. Unpack the archive into `$(em-config CACHE)/ports/<name>/`, exactly as `shutil.unpack_archive` would. Each archive has one top-level directory.
    2. Write `$(em-config CACHE)/ports/<name>/.emscripten_url` containing the exact URL from the table, plus a newline.

    Emscripten's `up_to_date()` check then passes and nothing is downloaded. This is preferable to `EMCC_LOCAL_PORTS`, which skips the hash check and uses git checkouts that can differ from the release archives.
  - **Script it** in `scripts/setup-emsdk.sh`, e.g. `--with-ports`:
    1. `git fetch origin emscripten-ports`, then `git archive` it to a temp dir.
    2. Verify each file's SHA-512 against `tools/ports/<name>.py` and fail on any mismatch.
    3. Unpack each archive and write its marker.
  - **If the emsdk pin changes**, the port versions change too. Refresh the branch and the table.

### 3. Compiler and link flags

- Exceptions: `-fwasm-exceptions`, matching 05b. ESC no longer throws (04a), but 17 other `throw` sites remain (code-facts §1).
- `-sALLOW_MEMORY_GROWTH=1`.
- No pthreads, so no COOP/COEP headers are needed.

### 4. RmlUi

- Cross-build RmlUi 4.3 from the `ext/RmlUi/RmlUi` submodule as a static library, using the FreeType port.
- Make the wasm build Release, or drop RmlDebugger, since it is only used when `!NDEBUG`.
- An RmlUi upgrade (the TODO's top item) is a user decision. Propose it if it would clearly help; don't do it unilaterally.

### 5. Remove the GL dependency

- Remove both `#error` guards and `find_package(OpenGL)`/`OPENGL_LIBRARIES`.
- Check the Y-flip in `SetScissorRegion` (RenderInterfaceSDL2.cpp ~:102) and fix it if the clipping is wrong. Native uses the same code.

### 6. Renderer errors

- `MickSDLRenderer` does `throw new runtime_error` (code-facts §12). Change it to throw by value, so init failures are caught and reported. On web, WebGL init can fail.
- If no accelerated renderer can be created, fall back to `SDL_HINT_RENDER_DRIVER=software`.

### 7. Main loop

- On Emscripten, `main()` sets up, then calls `emscripten_set_main_loop(frame, 0, 1)` with brief 03b's `frame()` unchanged.
- Quitting calls a teardown function plus `emscripten_cancel_main_loop()`.
- There must be no blocking calls on the web path.

### 8. Files

- Preload with `--preload-file data@/data --preload-file assets@/assets`, and run with the working directory at `/`.
- Nothing is written to disk, so IDBFS isn't needed yet. Brief 12 handles settings persistence.

### 9. Audio

Browsers block audio until the user interacts with the page.

- Open or resume audio on the first input.
- Nothing may break before that point.

### 10. Input

- **Keyboard:** works through SDL.
- **Gamepads:** come through SDL's Emscripten backend, using the Gamepad API.
  - Pads only appear after a button press. Check that the "press START to join" flow copes with that.
  - Rumble must fail quietly.

### 11. Logging

Default to info level on web.

### 12. Shell page

- `web/shell.html`: a canvas, a loading indicator, and a "click to start" overlay that also unlocks audio.
- The build outputs `.html`, `.js`, `.wasm` and `.data` files into `build-wasm/`.
- Document how to serve them locally, for example `python3 -m http.server`.

## Out of scope

- Networking (12).
- Hosting and deployment (ask the user later).
- Mobile and touch input.
- Persistent settings.
- The sim determinism gate (05b already covers wasm under Node).

## Tests

- **Native is unchanged.** Run the existing suite and a 200-seed sweep. The sweep covers the sim only and doesn't exercise `main`, so the `main.cpp` and renderer changes need the play-test.
- **Playwright smoke test**, committed under `tests/web/`. Chromium is pre-installed (`require('playwright')`; never run `playwright install`).
  - Headless Chromium no longer falls back to SwiftShader for WebGL automatically, so launch it with `--enable-unsafe-swiftshader` or `--use-angle=swiftshader`.
  - Judge rendering from `page.screenshot()`, not canvas readback. A WebGL canvas reads back blank unless `preserveDrawingBuffer` is set.
  - Steps:
    1. Serve `build-wasm/` and load the page.
    2. Click start.
    3. Assert the main-menu screenshot is not blank.
    4. Start a local all-AI match through a test hook, for example `?autostart=ai4`.
    5. Assert the sim advanced N ticks with no exceptions in the console. Expose the tick count to JS for this.

## Acceptance criteria

- `build-wasm/` serves a playable game: menus, local multiplayer, AI, and sound after a click. This needs a user play-test in a desktop browser, which may still be pending at merge.
- The native build and tests are unaffected.
- The Playwright smoke test is committed and passing.
- The build steps, including the port fallback, are documented in the README.

## Outcome

*(fill in when done)*
