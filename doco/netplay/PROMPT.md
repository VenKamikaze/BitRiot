# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 5 Nov 2026, after brief 07)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a, 03b, 04a, 04b, 05, 05b, 09 and **07**.
  - `done`: 02b, 03a, 05 and 05b.
  - `done (play-test pending)`: 03b, 04a and 04b (controller parts only), 09 (browser) and 07 (two real windows). See "Waiting on the user".
- **07, native lockstep netplay** (merge 0ccdb66):
  - **vnet `v0.4.0` is a git submodule at `ext/vnet`.** In an existing checkout run `git submodule update --init ext/vnet`. `BITRIOT_NETPLAY` is AUTO/ON/OFF: AUTO turns netplay on when the submodule is present and is re-evaluated at every configure. It is OFF for Emscripten and MinGW and in the 05b gate.
  - **`bitriot_net`** (`src/net/`, outside `bitriot_sim`): the wire protocol, `LockstepSession` (host and peer, no SDL, no `Simulation`) and `NetMatch` (drives the `Simulation`, records final inputs, keeps a ring of 16 state dumps).
  - **The game:** `--host [port]`, `--join host:port`, `--name`, `--local-players`, `--players`, with a minimal RmlUi lobby page (`assets/menu/netlobby.rml`).
  - **`bitriot_headless host|join`**.
  - **[`protocol.md`](protocol.md)** documents every message.
  - **Rules:** only the host starts a round; ESC leaves (the peer's slots become bots); host loss shows a "Disconnected" page; a desync writes dumps and the record.
  - **Review:** one Opus review, then a fix round, which closed an undetected desync in a round's last ticks, a peer able to freeze the session with an over-full tick, and netplay silently building OFF. Then one re-review and a small second pass, in which a pad can now bind this machine's own slot with START.
  - **Not changed:** no `bitriot_sim` source, so the build id is still `0x6767612d056ba8af` and the golden digests are untouched.
- **Earlier this session:**
  - **05b** (merge a7d6737): `scripts/determinism-gate.sh`, 17 variants identical on every tick.
  - **09** (merge 452b742): the WASM build, `scripts/setup-emsdk.sh [--with-ports]`, `tests/web/smoke.js`.
  - **The user's first web build failed** on a fresh emsdk (SDL2_ttf's port wants plain FreeType under the parent's cache lock). It is fixed in 76f516d: configure runs `embuilder build freetype`.
- **Test counts on `claude`:**
  - `bitriot_tests` 599 and `bitriot_sim_tests` 485, on GCC Release and Clang Release.
  - `ctest` 3/3. The multi-process netplay test has 7 cases and takes about 70 s.
  - The 200-seed sweep has 0 failures on both binaries.
  - The determinism gate passes: 17 variants, with `gcc-m32` not available.
  - The web smoke test passes 41/41.
- **vnet:** unchanged. `claude` is at 579a410, tag `v0.4.0`, CI green (run 36). Read run logs with the GitHub MCP tools (`actions_list`, `get_job_logs`). BitRiot has no CI workflow.

**Next ready: 08** (lobby UI). It depends on 07 only. Its brief was refreshed after 07 merged: "Where 07 left things" lists the APIs to build on and what the protocol lacks (ready flags, back to lobby). Read 07's Outcome first.
- After 08: 12 (browser netplay, which needs 07, 08, 09, 10 and 14) and 13 (disconnects and hot-join, which needs 05, 07 and 08). 12 and 13 can run in parallel in separate worktrees.
- Nothing can run in parallel with 08.

**In flight:** nothing. Every feature branch in both repos is merged.

**Decisions taken while the user was away** (they may revisit them; the details are in each brief's Outcome):

- **07:**
  - **vnet is a submodule, not FetchContent:** it matches RmlUi and needs no network at configure time.
  - **`host`/`join` are `bitriot_headless` commands**; `--host`/`--join` are accepted as aliases.
  - **The game's lobby is a minimal RmlUi page,** not a text-mode one.
  - **D is chosen once per round** from the host's pings. On localhost it comes out as 2 or 3.
  - **Pacing:** nobody is more than D ticks ahead by construction, because a record exists only after stepping. The brief's catch-up rule is implemented but cannot fire in this design; it is kept for 13.
  - **Control events ride in the record of their tick:**
    - the host validates them;
    - the lower peer id wins a same-tick claim, and the loser is told and revokes the slot;
    - the last event per slot in a record wins;
    - at most 18 peer events per tick, plus the host's leaver events.
  - **A checksum is sent every 30 ticks and on the round's last tick.** The next round waits for every final hash.
  - **Closing the window or SIGTERM mid-round leaves the round** but keeps the process, like ESC.
  - **In a network game a pad's START first binds the pad to this machine's own slot,** then to a bot. This is local only and nothing goes on the wire.
- **05b:**
  - **The negative control is `bitriot_headless replay --perturb-at`,** not a flag in the sim.
  - **Extra variants:** sanitizers, valgrind, libc++, init-pattern and malloc-perturb.
  - **wasm uses `-fwasm-exceptions`**, which means the legacy instructions in Emscripten 6.0.13.
- **09:**
  - **SDL_image is built with BMP as well as PNG.**
  - **RmlUi is built from its file list,** because CMake 4 refuses its own CMakeLists.
  - **The game starts from the click,** for audio.
  - **No RmlUi upgrade.** That is the user's call.

**Waiting on the user** (ask about these; don't block on them):

1. **Controller play-test, native.** 02b and 03a passed on 29 Oct 2026.
   - Still pending, all needing a pad: 03b (rumble, hotplug, join selector, Options map size), 04a (pad feel, and the same-tick tap fix on pad and keyboard) and 04b (pad join, keyboard takeover of a CPU player, pad unplug, rumble).
   - Also look at the native menus after 09's RmlUi clipping fix: the player-select page with 5 or 6 players, Game Options, and the scoreboard.
   - One session with a pad and a keyboard covers all of it. The Arch commands are in each brief's Outcome. Record the result in each Outcome.
2. **Browser play-test (09)** in Chrome and Firefox on Arch: menus, keyboard and pad play, CPU players, sound after the click, Game Options, and pacing at 60 and 144 Hz. The steps are at the end of 09's Outcome. Expect two quirks:
   - a real pad's first press only wakes it, so press again;
   - Ctrl+W (Player 1's keys) asks before closing the tab.

   Rebinding the web keys is the user's call.
3. **Netplay play-test (07):** two real windows on one machine, `./BitRiot --host 7777 --players 4` and `./BitRiot --join 127.0.0.1:7777`. Check:
   - a full match, then the scoreboard and a second round started by the host;
   - a peer's ESC turning its player into a bot;
   - the host's ESC and `kill -9` (the peer is told "Disconnected");
   - a pad's START binding your own slot;
   - a takeover of a bot;
   - identical `--record` files on both;
   - optionally, a second machine.

   The steps are at the end of 07's Outcome.
4. **vnet `claude` → `main`** merge: the user's call.
5. **Tags:** the cloud session can't push tags (HTTP 403), so the user tags any future vnet release from a PC. The releasing brief's Outcome gives the commands. No tag was needed in this session.

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare (e.g. test-alone seed 4652) and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.
- **Netplay limits (07 follow-ups):**
  - no authentication or encryption, and no NAT traversal;
  - `canStartRound()` waits for every peer's final hash with no time limit (unreachable today; 13 should bound it);
  - a debugger pause over 10 s drops a machine;
  - over a relay (12) D may need more than 8.
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
- **Never run a seed sweep in a build tree that is being rebuilt.** 07's first sweep showed phantom failures that way.
- **A fix round can open a new gap.** 07's device-assignment fix left pad-only players unable to play, and the re-review caught it, so keep the re-review whenever a fix changes behaviour.
- **Build-environment bugs hide behind a warm cache.** The web build passed every check here and failed on the user's fresh emsdk. When a brief adds a toolchain step, have someone try it on a clean cache.
- **After any sim change, run `scripts/determinism-gate.sh`.** It takes about 6 minutes with warm builds and needs emsdk for the wasm rows.

## 1. Set up

1. **Attach both repos** to the session if they aren't already. In both repos, work from the **`claude`** branch: it is the base for all work, not `main`/`master`.
2. **Install dependencies.** If the environment has no setup script configured, run `scripts/cloud-setup.sh` from BitRiot. It installs SDL2 and GTest, and builds RmlUi 4.3 into `/usr/local`.
   - Also run `git submodule update --init ext/vnet` (07's netplay; without it `BITRIOT_NETPLAY` falls back to OFF with a warning).
   - Run `git submodule update --init ext/RmlUi/RmlUi` too, if you build the web version.
   - For the web build and the determinism gate's wasm rows, run `scripts/setup-emsdk.sh --with-ports`.
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
   - BitRiot netplay: 08 (07 is done). It is the only ready brief.
   - After 08, 12 (browser netplay) and 13 (disconnects and hot-join) can run in parallel.
   - vnet: no briefed work left (06, 10, 11 and 14 are done; `v0.4.0`). 12 may need vnet changes; brief them in vnet's `doco/` if so.
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
