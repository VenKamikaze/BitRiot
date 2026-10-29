# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 29 Oct 2026, end of the third session)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a, 03b, 04a, 04b, 05, **05b** and **09**.
  - Status: 02b, 03a, 05 and 05b are `done`. 03b, 04a and 04b are `done (play-test pending)` for their controller parts only. 09 is `done (play-test pending)` (see "Waiting on the user").
  - **05b** (merge a7d6737) is the cross-platform determinism gate.
    - `scripts/determinism-gate.sh` replays the golden record plus six all-AI rounds that the reference plays itself, under 17 variants. Every one matches the GCC `-O2` reference on every tick. The variants: GCC and Clang at `-O0`/`-O2`/`-O3 -march=native -ffast-math`, libc++, ASan+UBSan, valgrind, an init-pattern build, `MALLOC_PERTURB_`, aarch64 under qemu, and wasm under Node 22 and 24 (legacy and exnref exceptions).
    - The negative control, `bitriot_headless replay --perturb-at N` (in the headless CLI, not in `bitriot_sim`), is caught at its tick on every variant. Planted faults (32-bit `size_t`, evaluation order, signed overflow, uninitialised heap) are caught by the variants that should catch them.
    - `gcc-m32` is not available here: `g++-multilib` conflicts with the aarch64 cross compiler on Ubuntu.
    - wasm replays at roughly 40k ticks/s, against 67k natively.
    - It had one review, one fix round (one blocker: a machine with no emsdk failed the gate) and one re-review, which said merge as is.
  - **09** (merge 452b742) is the WASM build of the whole game.
    - `scripts/setup-emsdk.sh [--with-ports]` installs the same pin as vnet, Emscripten 6.0.13. `--with-ports` seeds the six blocked ports from the `emscripten-ports` branch, checked by SHA-512.
    - The rest: `cmake/EmscriptenGame.cmake` (RmlUi 4.3 built from the submodule's file list), `web/shell.html` (the click to start also unlocks audio), `emscripten_set_main_loop` around 03b's `frame()`, a software-renderer fallback, the GL dependency removed, and a Playwright smoke test (`node tests/web/smoke.js build-wasm`, 41 checks).
    - **Native rendering changed:** 09 fixed three clipping bugs in the RmlUi glue (`RenderInterfaceSDL2.cpp`). The shipped menus draw the same, checked by screenshot, but the native play-test should look at the menus too.
    - It had one review and one fix round; the fixes were small, so there was no re-review.
  - **Test counts on `claude`:** `bitriot_tests` 517 and `bitriot_sim_tests` 412, on GCC Release and Clang Release. The six golden digests, `golden-round.brmr` and the build id (`0x6767612d056ba8af`) are unchanged, because neither brief changed sim behaviour.
- **vnet:** unchanged this session: 06, 10, 11 and 14 are done, and `claude` is at 579a410, tag `v0.4.0`. CI is green on `claude`: run 36, all seven jobs. Read a run's logs with the GitHub MCP tools (`actions_list`, `get_job_logs`), and check the latest run on a branch before reporting it done. BitRiot has no CI workflow.

**Next ready: 07** (native lockstep netplay), with both repos attached. It depends on 05b, 06 and 14, all done. Then 08 (lobby UI), then 12 (browser netplay) and 13 (disconnects and hot-join). 08 and 13 both depend on 07, so nothing can run in parallel with 07 unless the user adds work.
- Read 05's follow-ups for 07: `HELLO` carries the build id and data hash, a recorder runs on every peer, and `bitriot_headless --host/--join`.
- Read 05b's Outcome: run the gate after any sim change, and hash only at checkpoints.
- Read 09's follow-ups for 12: hidden tabs stop `requestAnimationFrame`, settings need IDBFS, the web test hooks must be gated before a public page, and vnet must use the same `-fwasm-exceptions` flags.

**In flight:** nothing. Every feature branch in both repos is merged.

**Decisions taken in the third session while the user was away** (they may revisit them):

- **05b's negative control is a `bitriot_headless replay --perturb-at` option**, not a flag inside the sim. This keeps `bitriot_sim` and the build id untouched. It perturbs a block's x offset by 1/65536, because a player's offset is pulled back to the tile centre by the next step.
- **05b goes beyond the brief:** sanitizer, valgrind, init-pattern, malloc-perturb and libc++ variants, plus six extra all-AI records. The brief's goal names UB, uninitialised state and libstdc++ vs libc++.
- **wasm exceptions:** `-fwasm-exceptions`, which in Emscripten 6.0.13 means the legacy instructions. Standard exnref (`-sWASM_LEGACY_EXCEPTIONS=0`) also matches, under Node 22 and 24.
- **09's deviations from the brief:**
  - SDL_image is built with BMP as well as PNG, because the player-select faces are BMP.
  - RmlUi is compiled from its file list, not its own CMakeLists, which CMake 4 on Arch refuses.
  - The loop callback is a wrapper (`webLoopIteration`) around the unchanged `frame()`.
  - The game starts from the click (`callMain`), so audio opens inside the gesture.
  - Audio failing to open is non-fatal on the web only.
  - `?autostart=ai5`/`ai6` raise the map height to fit the portraits.
- **No RmlUi upgrade.** 09 didn't need one. It stays a user decision and would be its own brief.

**Waiting on the user** (ask about these; don't block on them):

1. **Controller play-test, native.** 02b and 03a passed on 29 Oct 2026.
   - Still pending, all needing a pad: 03b (rumble, hotplug, join selector, Options map size), 04a (pad feel, and the same-tick tap fix on pad and keyboard) and 04b (pad join, keyboard takeover of a CPU player, pad unplug, rumble).
   - Also look at the native menus after 09's RmlUi clipping fix: the player-select page with 5 or 6 players, Game Options, and the scoreboard.
   - One session with a pad and a keyboard covers all of it. The Arch commands are in each brief's Outcome. Record the result in each Outcome.
2. **Browser play-test (09)** in Chrome and Firefox on Arch: menus, keyboard and pad play, CPU players, sound after the click, Game Options, and pacing at 60 and 144 Hz. The steps are at the end of 09's Outcome. Expect two quirks:
   - a real pad's first press only wakes it, so press again;
   - Ctrl+W (Player 1's keys) asks before closing the tab.

   Rebinding the web keys is the user's call.
3. **vnet `claude` → `main`** merge: the user's call.
4. **Tags:** the cloud session can't push tags (HTTP 403), so the user tags any future vnet release from a PC. The releasing brief's Outcome gives the commands. This session tagged nothing.

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare (e.g. test-alone seed 4652) and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.
- **`RuntimeException::what()`** returns a pointer into a local string. It is pre-existing and in `bitriot_sim`; Clang warns `-Wreturn-stack-address`. Fix it in a brief that owns the sim, and rerun the gate.

**Lessons so far:**

- **The container can restart mid-run** and kill background work. Push feature branches as soon as an implementer reports, not only at merge. If a reviewer was interrupted, resume it (it keeps its context) rather than starting a new one.
- **Sweeps:** run them from the build dir *after* building the `BitRiot` target (or `bitriot_runtime_links`), which creates the `data/` and `assets/` links. Also sweep `bitriot_sim_tests`, and build with Clang too.
- **Parallel briefs:** run independent briefs in separate worktrees (`git worktree add`). Merge whichever finishes first, and have the other merge `claude` in before its review.
  - Share one emsdk install between worktrees: `EMSDK_DIR=<path>`, or install it once with `scripts/setup-emsdk.sh --with-ports`.
  - The container has 4 cores, so cap each implementer at `-j4`.
- **Reviews find real bugs** even when every test passes: 04's join-indicator recursion, 05's allocation bomb, 14's `vnet_wait` loop, 09's smoke test that could not see a blank canvas, and 05b's gate failing on a machine with no emsdk. Tests that pass `nullptr` or skip the real glue hide them, so ask reviewers to probe the untested glue and try to break the tests.
- **Golden digests:** the six pinned rounds in `SimulationTest` and the golden match record `tests/data/golden-round.brmr` (brief 05) must stay byte-identical across refactors. A legitimate sim change moves them, and the commit must say so (see `tests/README.md`).
- **Any edit to a `bitriot_sim` source changes the build id,** even a logging tweak, because the id is a hash of the sim's source text. Keep non-sim changes out of `bitriot_sim` files.
- **After any sim change, run `scripts/determinism-gate.sh`.** It takes about 6 minutes with warm builds and needs emsdk for the wasm rows.

## 1. Set up

1. **Attach both repos** to the session if they aren't already. In both repos, work from the **`claude`** branch: it is the base for all work, not `main`/`master`.
2. **Install dependencies.** If the environment has no setup script configured, run `scripts/cloud-setup.sh` from BitRiot. It installs SDL2 and GTest, and builds RmlUi 4.3 into `/usr/local`.
3. **Build and test** BitRiot to confirm a green baseline:
   ```bash
   cmake -S . -B build && cmake --build build -j8 && ./build/tests/bitriot_tests
   ```
4. **Check the network.** Run the check in the "Network limits" section of [`README.md`](README.md). Note whether GitHub archive downloads work in this session.

## 2. Read the plan

Read these in full before doing anything else:

- [`doco/netplay/README.md`](README.md): the master index. It covers order, dependencies, **settled decisions D1–D9**, open questions, workflow, play-test policy and network notes.
- [`doco/netplay/research/code-facts.md`](research/code-facts.md): what the code actually does, with file:line references.
- vnet's [`doco/README.md`](https://github.com/VenKamikaze/vnet/blob/claude/doco/README.md): the status table for briefs 06, 10, 11 and 14 lives there. Briefs 07 and 12 also need vnet's [`doco/integration.md`](https://github.com/VenKamikaze/vnet/blob/claude/doco/integration.md).

Then read the brief(s) you are about to start, plus the **Outcome** sections of every brief they depend on.

## 3. Pick the next brief(s)

1. **Find ready briefs.** From the status tables in both repos, list every brief whose dependencies are all done.
2. **Tracks that can run in parallel now:**
   - BitRiot netplay: 07 (01–05b, 06 and 14 are done). It is the only ready brief.
   - BitRiot WASM: 09 is done; 12 needs 07 and 08 first.
   - vnet: no briefed work left (06, 10, 11 and 14 are done; `v0.4.0`).

   Later briefs (08, then 12 and 13) follow the dependency table.
3. **Check the prerequisites.** Some briefs depend on **open questions** (`Qn` in the table). If the user hasn't answered one, **ask the user before starting that brief**. Implementer subagents cannot ask.
   - All four are answered: Q1 (countdown 2 s / 60 ticks), Q2 (fixed-point up front, brief 02b), Q3 (vnet: MIT, public) and Q4 (online cap stays 6). Check the README in case new questions have been added.
4. **In-flight work:** none (see §0).
5. **Confirm the plan** with the user in a few lines: which brief(s) you'll run and in what order. Then proceed.

## 4. Run each brief

This is the user's standard process:

1. **Branch** from `claude`: `netplay/NN-short-name` in BitRiot, or `vnet/NN-short-name` in vnet.
2. **Implementer.** Spawn a **Sonnet** subagent, giving it:
   - the brief path, the repo, and the branch name;
   - "read `doco/netplay/README.md` (or vnet's `doco/README.md`), the brief, code-facts, and the dependency briefs' Outcomes first";
   - the commit-message trailer lines this session requires;
   - "commit at each milestone and push the feature branch (only that branch) after each commit, so a container restart loses nothing; never merge, never touch `claude`, never open a PR".

   Ask it to report back: commit hashes, what changed, test results with counts, proof that the new tests fail without the change, sweep numbers before and after (for sim changes), and any concerns.
3. **Review.** Spawn an **Opus** subagent for a **read-only** review. Tell it to:
   - build in a throwaway `git worktree` with its own build directory;
   - check the brief's acceptance criteria, correctness and determinism, and that the tests really fail without the change;
   - rank findings as blocking, should-fix or nit, each with file:line and a concrete failure scenario, and give a verdict.

   Give it the brief and the diff command, not your own conclusions.
4. **Fix round.** Send the worthwhile findings back to the **same** implementer: resume it, don't start a new one. If the fixes are non-trivial, have the reviewer re-review.

   **Exception: 04a, 04b and 05 use a reduced review.** See "Reduced review" in [`README.md`](README.md):
   - 04a and 04b are implemented and merged without review.
   - After 04b, both get one joint review and one fix round.
   - 05 gets one review and one fix round.
   - A second round is allowed only for a major, complex finding.
5. **Verify yourself.**
   - Rebuild and run the full suite.
   - For sim changes, run a seed sweep (`BITRIOT_TEST_SEED=1..200`).
   - Read the diff of the fix round.
6. **Merge.** Merge with `--no-ff` into `claude`, and push both `claude` and the feature branch. Never commit directly to `claude`. Don't open pull requests unless the user asks.
7. **Record.**
   - Set the brief's status in its repo's table.
   - Make sure the brief's **Outcome** section is filled in.
   - If the brief needs a user play-test, mark it `done (play-test pending)`, and give the user the exact commands.
8. **Report** to the user in a few lines: what landed, numbers, anything pending, and what's next.

While a long subagent runs, you can review or plan the parallel track. Don't do the same work it's doing.

## 5. Rules of the road

- **Settled decisions (D1–D9) are not to be reopened** inside a brief. If one proves wrong, stop and ask the user. If brief 05b's determinism gate fails, find and fix the cause; if that can't be done within 05b, stop and ask.
- **Tests are never skipped, loosened or quarantined** to get to green.
- **Network denials:** if a host is blocked, tell the user which one. Don't work around a denial.
- **Ambiguity:** if a brief is ambiguous or wrong about the code, fix the brief (and code-facts) in the same branch, and say so. If the scope would grow a lot, ask first.
- **The user's preferences:** Java is their primary language, they run Arch Linux, they prefer open-source tools, and they like bash and Python for quick scripts.
