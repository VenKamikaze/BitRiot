# 09 — WASM port (local play)

- **Repo:** BitRiot
- **Branch:** `netplay/09-wasm`
- **Depends on:** 03b (shared `frame()` loop and the `bitriot_sim` split). Independent of 04–08.
- **Status:** done (play-test pending)

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

Use `-sUSE_SDL=2`, `-sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS='["png","bmp"]'` (BMP as well as PNG: the player-select page's faces are BMP files that RmlUi loads through SDL_image; the brief first said PNG only), `-sUSE_SDL_TTF=2`, `-sUSE_SDL_MIXER=2` with WAV only, and `-sUSE_FREETYPE=1`.

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
- Check the Y-flip in `SetScissorRegion` (RenderInterfaceSDL2.cpp ~:102) and fix it if the clipping is wrong. Native uses the same code. *(It was wrong, and so was more than the flip: see the Outcome.)*

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

**Status: done, play-test pending.** Bit Riot builds with Emscripten and runs in a desktop browser: menus (mouse, keyboard, gamepad), local play with keyboard and pads, CPU players, sound after the click, the Game Options (map size, fullscreen). The Playwright smoke test (38 checks, 5 scenarios) passes on the Release and the Debug web builds. The native build and every native test are unchanged (513 + 408 tests pass on GCC Release and Clang Release, and the 200-seed sweep is clean); the simulation was not touched, so the golden digests are byte-identical.

**What landed**

1. **`scripts/setup-emsdk.sh [--with-ports]`.** The same base as vnet's script (the same pinned emsdk revision `d7b4130688bfa348392b921ab04015aa15c4f154`, Emscripten 6.0.13, `EMSDK_DIR` defaulting to `<repo>/.emsdk`, which `.gitignore` now lists), plus `--with-ports` for networks that block GitHub archive downloads (below). Run it once, then `source .emsdk/emsdk_env.sh`.
2. **`cmake/EmscriptenGame.cmake`**, included by the top-level `CMakeLists.txt` instead of the `find_package` calls when `EMSCRIPTEN` is set (and `BITRIOT_BUILD_GAME` is on, the default). It adds the port flags and the exception/memory flags to every target, builds **RmlUi 4.3 from the submodule** as two static libraries (RmlCore, RmlDebugger) from RmlUi's own `CMake/FileList.cmake` plus the FreeType port, and defines `bitriot_configure_web_target()` (shell page, preloaded files, `index.html` copy). RmlUi's own CMakeLists is not used: it says `cmake_minimum_required(VERSION 3.1)`, which CMake 4 (current on Arch) refuses. The default build type is Release on Emscripten.
3. **`web/shell.html`**: the page (a canvas fitted to the window by CSS only, a loading bar, a "Click to start" button, a help strip, fatal-error and quit messages, a `beforeunload` guard while playing). The game does **not** start when the page loads: the button calls `Module.callMain()`, so `Mix_OpenAudio` runs inside the click (the `AudioContext` is created running; SDL's own Emscripten audio would also resume a suspended one on the first gesture) and the canvas gets the keyboard focus.
4. **`src/main.cpp`**: the loop body is `loopIteration()` and the un-init section is `shutdownGame()`; native calls them from the same `while` as before (plus `SDL_Delay(sleepHintMs())`), Emscripten hands `webLoopIteration` to `emscripten_set_main_loop(…, 0, 1)` (requestAnimationFrame pacing, no sleep, `frame()` untouched). `webLoopIteration` catches whatever a frame throws, and on quit or error calls `emscripten_cancel_main_loop()`, `shutdownGame()` and tells the page. The clock is `emscripten_get_now()` on the web. A failed renderer init is reported and shut down cleanly instead of tripping `getInstance()`'s assert (`MickSDLRenderer::getInstanceIfCreated()`).
5. **`MickSDLRenderer`**: throws by value; if `SDL_CreateRenderer` fails it makes the window again and retries with `SDL_HINT_RENDER_DRIVER=software`. (With WebGL disabled SDL already ends at its software driver by itself, which scenario 3 of the smoke test exercises; the retry covers the rest.)
6. **GL dependency gone**: both `#error` guards, `find_package(OpenGL)`, `OPENGL_INCLUDE_DIR` and `OPENGL_LIBRARIES`, from the native build too. (Confirmed in `SDL_config_emscripten.h` that `SDL_VIDEO_OPENGL` is undefined there, so the guards would have fired.)
7. **RmlUi glue clipping fixed** (`RenderInterfaceSDL2.cpp`), natively as well. The brief asked about the Y-flip; there were three bugs: the flip itself (RmlUi's and SDL's origin are both the top left), the rectangle being stored by `SetScissorRegion` but applied by `EnableScissorRegion`, which RmlUi calls *first* (so every clipped element used the previous element's rectangle), and `RenderGeometry` translating through `SDL_RenderSetViewport` while SDL takes clip rectangles relative to the viewport (and its GL and GLES2 renderers do not re-apply a clip when only the viewport changes). The shipped menus hid it because every clipped `.strip` was big enough to hold its content wherever the clip landed. Proved with a throwaway `height: 80px` on `.strip`: before, the content vanished; after, exactly the first 80 px show. Now the vertices are translated on the CPU (whole pixels, as before), the rectangle is applied when set, and the flip is gone. The shipped pages draw the same (compared by screenshot, native and web).
8. **Test hooks** (`src/web/WebHooks.*`, web only): `?autostart=ai2`..`ai6` starts a CPU-only round on the first frame (five or six players raise the map height to 15 or 19, which the player-select page would demand; an invalid count or a call while a round runs changes nothing and returns 0), and `Module._bitriot_frames()`, `_bitriot_tick()`, `_bitriot_game_state()`, `_bitriot_start_ai_match(n)`, `_bitriot_sounds_played()`, `_bitriot_joystick_count()` are exported. They sit on two small engine additions that are always compiled: `GameEngine::simTick()` and `GameEngine::startAllAiMatch(n)` (sets n CPU players, closes the menu pages with the new `MenuSDLRenderer::closeAllMenus()`, enters `GAME_INIT`; refuses unless a menu is showing), and `MickSDLSound::soundsPlayed()`.
9. **Smaller web adjustments**: the Release web build prints no debug-level log lines (`NDEBUG` already turns them off in `MickLogger.cpp`, which is in `bitriot_sim`, so it was deliberately left alone: `bitriot_headless version` prints the same build id as `claude`); `Mix_OpenAudio` failing on the web logs a warning and plays on silently (native still throws, as before); the build relinks when a preloaded `data/` or `assets/` file changes.
10. **`tests/web/smoke.js`** (below), the README's "Web build" section, code-facts §1/§9/§11/§12 and the netplay README updated.

**Port setup.** Emscripten 6.0.13's ports come from GitHub archive URLs, which the cloud container blocks (403). Six of them are stored on the orphan branch `emscripten-ports` (SHA-512 against `tools/ports/<name>.py`: sdl2 release-2.32.10, sdl2_image release-2.6.0, sdl2_ttf release-2.20.2, sdl2_mixer release-2.8.0, freetype VER-2-14-3, zlib 1.3.2); harfbuzz and libpng download normally. `scripts/setup-emsdk.sh --with-ports` fetches that branch with git, verifies each hash, unpacks each archive into `$(em-config CACHE)/ports/<name>/` the way Emscripten does and writes `.emscripten_url`, so the first `emcc` call finds every port up to date and downloads nothing (the hash check is kept, unlike `EMCC_LOCAL_PORTS`). It is idempotent, only seeds what is missing, fails on a hash mismatch (a changed emsdk pin means a refreshed branch), and works in a git worktree. Checked here: after it, `emcc t.cpp -sUSE_SDL=2 -sUSE_SDL_IMAGE=2 … -sUSE_FREETYPE=1` builds SDL2, SDL2_image, SDL2_ttf, SDL2_mixer, FreeType and harfbuzz with no GitHub archive request. No other host was blocked.

**Flags and exceptions mode.** `-fwasm-exceptions` on every compile and link, so the game's remaining `throw` sites work and the model matches 05b. In Emscripten 6.0.13 that means the *legacy* wasm exception instructions (`WASM_LEGACY_EXCEPTIONS` defaults to true, and the ports are built as `…-legacysjlj`); every current Chrome, Firefox and Safari runs them. `-sALLOW_MEMORY_GROWTH=1`, `-sSTACK_SIZE=2097152` (Emscripten's 64 KB default is small for RmlUi and the AI), no pthreads, so no COOP/COEP headers. Ports: `-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sSDL2_IMAGE_FORMATS=["png","bmp"] -sUSE_SDL_TTF=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=[] -sUSE_FREETYPE=1` (WAV only, no ogg or vorbis). The link also gets `--preload-file data@/data --preload-file assets@/assets`, `--shell-file web/shell.html` and `-sEXPORTED_RUNTIME_METHODS=callMain`.

**Output sizes** (Release): `BitRiot.wasm` 3.0 MB (1.2 MB gzipped), `BitRiot.js` 165 KB (42 KB), `BitRiot.data` 4.2 MB (1.6 MB), `index.html` 5 KB. Cold build of everything incl. building the ports: about 5 minutes on 4 cores; an incremental relink takes 16 s.

**Smoke test** (`node tests/web/smoke.js [build-wasm]`; serves the directory itself; screenshots in `build-wasm/smoke/`). Five scenarios, each in a fresh headless Chromium, 38 checks: (1) the page offers the button; a click starts the game; the main menu screenshot is not blank; a mouse click opens the player-select page; *Start Game* starts a round with a keyboard player; W/A/S/D and Shift go through and the round carries on; ESC returns to the menu; the `startAllAiMatch(4)` hook starts a CPU-only round; the tick count advances 90 ticks; the round's screenshot is not blank and differs from the menu's; the `AudioContext` is running and advancing after the click; sounds reach the mixer; about 30 ticks a second whatever the display rate; (2) `?autostart=ai6` (a round from the address, on the taller map); (3) Chromium with WebGL disabled (SDL's software renderer); (4) a fake standard-layout gamepad that appears after load (`navigator.getGamepads` stubbed, since the Gamepad API hides real pads until a button is pressed): invisible until connected, then `SDL_NumJoysticks()` is 1, A opens the player-select page from the menu, START and A in a round (an unbound pad asks to rumble, which this pad cannot do) leave the round running with no error; (5) *Exit* tears the game down, stops the loop and shows the quit message. Every scenario also requires that no page error, `console.error`, `ERROR:` log line or wasm trap occurred and that the game reported no fatal error. Judged from screenshots of the canvas element alone (a WebGL canvas reads back blank otherwise, and a whole-page screenshot would make a blank canvas look like a picture: the first version did, and was fixed after review by showing that an injected `SDL_RenderClear` before `SDL_RenderPresent` fails every not-blank check). Errors after the click show on the page again (the overlay returns with the message), and the hooks answer 0 once the game has quit.

**Deviations from the brief**

- SDL_image gets **BMP as well as PNG** (the menu's face pictures are BMP; without it the player-select page showed none and logged "Failed to load texture").
- **RmlUi is compiled from its source file list**, not with RmlUi's own CMake (CMake 4 compatibility; see above), and the glue's clipping was rewritten (three bugs, not one).
- **No `emscripten_set_main_loop(frame, 0, 1)` directly**: the callback is `webLoopIteration`, a wrapper that runs `loopIteration()` (lock, `frame()`, present, unlock), catches exceptions and does the teardown; `frame()` and `FrameLoop` are unchanged. The loop body and teardown moved into functions that native shares.
- The game starts from the click (`callMain`) rather than at page load, for audio (a design choice inside "click to start overlay that also unlocks audio").
- Audio failing to open is non-fatal on the web only.
- `GameEngine::simTick()` / `startAllAiMatch()`, `MenuSDLRenderer::closeAllMenus()` and `MickSDLSound::soundsPlayed()` exist in native builds too, though only the web hooks call them.
- `.emsdk` is in `.gitignore`; the web build directory is `build-wasm` (already ignored by `/build-*/`).

**Decisions worth knowing about**

- *Window size.* SDL reads the canvas's CSS size when it creates its window, so `shell.html` leaves the canvas at its pixel size until the game has started and only then fits it to the page by CSS (`fit()`, every 500 ms and on resize, skipped in fullscreen). The game's resolution does not change (768x416 for the default 17x13 map; a bigger map resizes the canvas and `fit()` follows), and SDL maps the mouse through the displayed size, which the smoke test's clicks confirm. The window is therefore never "resized" in SDL's sense and the menus' `handleWindowResize` is not exercised.
- *Browser shortcuts.* Player 1's default keys are W and left Ctrl, and Ctrl+W closes the tab. A page cannot stop that; the shell adds a `beforeunload` confirmation while a game is running, so the browser asks first, and the help text says so. Rebinding the keys for the web is a user decision.

**Not done, found on the way (follow-ups)**

- **For 12 (browser netplay):**
  - Build vnet's browser backend with the same flags (`-fwasm-exceptions`; no pthreads), and link its WebSocket library the usual Emscripten way; `cmake/EmscriptenGame.cmake` is where the flags live, and `bitriot_configure_web_target()` is where to add link options. `index.html`/`BitRiot.*` need to be served over HTTP(S) from the same origin as the relay or the relay needs CORS/`Origin` handling.
  - **Settings persistence** needs IDBFS (the game still writes nothing). `data/` and `assets/` are preloaded read-only at `/data` and `/assets`; mount IDBFS elsewhere (for example `/persist`) and `FS.syncfs` it from the page.
  - **Hidden tabs.** Browsers stop `requestAnimationFrame` in a background tab, so the simulation stops; the pacer forgives anything over 5 ticks, which is right for local play but means a hidden netplay peer stalls the others. 12 needs a policy (a timer-driven fallback, or a "paused" state in the lobby).
  - The frame clock is `emscripten_get_now()` truncated to 32 bits milliseconds (through `uint64_t`, so the conversion is defined); the value wraps every 49.7 days of page life, which the pacer's unsigned subtraction handles as it does natively.
  - **The test hooks ship in every web build** (`_bitriot_start_ai_match` and friends, `?autostart=`): gate them (a build option or a page-side opt-in) before 12 puts the game on a public page.
  - The test hooks (`_bitriot_start_ai_match` and friends) are a convenient place to add `_bitriot_join(url)` style hooks for a netplay smoke test; `tests/web/smoke.js` has the scenario scaffolding (`openPage`, `call`, `waitTicks`, `shot`, `notBlank`).
  - Web rumble: SDL's Emscripten joystick backend has no haptics, so `MickSDLUtil::rumbleController` finds no haptic device and returns false quietly (exercised by the smoke test with a fake pad that has no vibration actuator). Browsers do have `gamepad.vibrationActuator`; wiring it is a small follow-up if the user wants rumble on the web.
- **RmlUi upgrade** (the user's decision): the port does not need one. 4.3 compiled for wasm unchanged, and the glue, the only RmlUi-facing code that needed work, is fixed. An upgrade would be worth doing for its own sake (the TODO's top item, and newer RmlUi ships its own SDL backend that could replace the glue), but it changes the render-interface and data-binding APIs, so it is its own brief with a play-test.
- **The scoreboard and `Play Again`** after an all-AI round were checked only through the hook's menu path; the real flow is in the play-test list.
- `RuntimeException::what()` (pre-existing, in `bitriot_sim`) returns `c_str()` of a local string (Clang warns `-Wreturn-stack-address`); not touched here, worth fixing in a sim-owning brief.
- `InfoPanel::drawTextGDI` (dead code, never called) opens `assets/bitmaps/FreeMono.ttf` in an `if(!font)` that is inverted; harmless, left alone.
- The ScriptProcessorNode deprecation warning in the console is SDL's (its Emscripten audio uses ScriptProcessor; an AudioWorklet backend is newer SDL than 2.32.10 and not in this Emscripten's port).
- After `claude` moves (05b adds its own `scripts/setup-emsdk.sh`), merge `claude` into this branch and keep this branch's version of that file: it is the same base plus the `--with-ports` block, option parsing and header text.

**Test results.** Native, GCC 13 Release: `bitriot_tests` 513 passed, `bitriot_sim_tests` 408 passed; Clang 18 Release: 513 and 408; 200-seed sweep (`BITRIOT_TEST_SEED=1..200`, both binaries, GCC Release): all 200 seeds pass on both binaries (400 runs, no failures); a native Debug build also passes 513 tests. Web smoke test: Release 38 of 38, Debug 38 of 38. The golden digests did not change: nothing the simulation computes was touched (`GoldenRecordTest` and the pinned `SimulationTest` rounds pass). No file of `bitriot_sim` changed, so the build id is the same as `claude`'s (`bitriot_headless version` prints 0x6767612d056ba8af).

**Play-test (pending): Arch Linux, desktop browser**

Setup (once):

```
sudo pacman -S --needed git base-devel cmake python        # emsdk brings its own node and clang
git fetch origin && git checkout netplay/09-wasm            # or after the merge: claude
git submodule update --init ext/RmlUi/RmlUi
scripts/setup-emsdk.sh                                      # a few minutes; installs ./.emsdk
source .emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm
cmake --build build-wasm -j$(nproc)                         # first build also builds SDL2 and friends, ~5 min
python3 -m http.server -d build-wasm 8000
```

(If the machine's network blocks GitHub archives, run `scripts/setup-emsdk.sh --with-ports` instead of the plain call.) Open <http://localhost:8000/> in Chrome or Chromium and again in Firefox, press **Click to start**, and check:

1. **Loading and menus.** A progress bar, then the button; the main menu appears with the blurred board behind it. Mouse: hover and click Play, Game Options, Exit. Keyboard: Up/Down move the focus, Enter selects, ESC goes back. Player-select: change the number of players, Control (Keyboard 1/CPU), Team; the face pictures are shown.
2. **Local play with the keyboard.** Start Game with Player 1 on Keyboard 1 and the rest CPU: W A S D move, left Ctrl is the weapon key (hold it and press W for a bomb, A mine, D rocket, S block), left Shift lays an egg. Add a second keyboard player (Player 4: arrow keys, right Ctrl, right Shift) and play two humans against two CPUs. The CPU players move, bomb and pick up items. Play a round to its end: the scoreboard shows, *Play Again* and *Main Menu* work, the second round starts normally. ESC in a round returns to the scoreboard. (Ctrl+W will offer to close the tab: choose *Stay*.)
3. **Sound.** Silent before the click, then explosions, eggs and pickups are audible with no stutter or crackle; a second round still has sound. Test the browser's own mute/tab mute if you like.
4. **Gamepad.** Plug in a pad *before or after* loading the page, and press any button on it (browsers hide pads until then; SDL's Emscripten backend swallows the press that wakes a real pad, so press it again before expecting a response, and press START again in a round if the first one did nothing). In the main menu the D-pad moves, A selects and B goes back (first pad only, as natively). Start a round with Player 1 on the keyboard, press **START** on the pad to open the CPU-takeover selector, pick a CPU player with the D-pad and confirm with A: the pad now plays that character. **Rumble does nothing and must not log errors** (open the developer console: only the harmless `ScriptProcessorNode is deprecated` warning is expected, and no red lines). Unplug the pad mid-round: the character should go back to the CPU, as it does natively.
5. **Game Options.** Map width/height (the canvas resizes), Fullscreen (the browser goes fullscreen; ESC leaves it), Preserve Aspect Ratio, NPC Spawner.
6. **Feel.** The game should run at its normal speed on a 60 Hz and a 144 Hz display alike, with no visible stutter, and no burst of speed after a tab switch. Resize the browser window: the picture scales and the menus stay clickable at the right spots. Leave a CPU-only round running for a few minutes: the tab's CPU use stays modest and nothing grows.
7. **Optional: the automatic test.** `npm install playwright && npx playwright install chromium`, then `node tests/web/smoke.js build-wasm` (about 25 s; prints PASS or FAIL per check; screenshots in `build-wasm/smoke/`).

Record the result here when it comes back.
