# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 5 Nov 2026, after briefs 12 and 13: every brief is done)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a, 03b, 04a, 04b, 05, 05b, 09, 07, 08, **12** and **13**.
  - `done`: 02b, 03a, 05 and 05b.
  - `done (play-test pending)`: 03b, 04a and 04b (controller parts only), 09 (browser), 07 and 08 (two real windows), 12 (a native window and a browser tab through a relay) and 13 (three windows). See "Waiting on the user".
- **13, disconnects and hot-join** (merge 56b1a4e):
  - **Protocol version 3:** a flags byte in `HELLO` (`HELLO_SPECTATE`), `WELCOME_MIDMATCH`, `READY_AT`, `TAKEOVER_AT`. A v2 `HELLO` gets `REJECT_VERSION`.
  - **Hot-join:** the host keeps every confirmed tick of the round (`m_history`) and streams it as ordinary `TICKS` in ack-clocked windows; the joiner fast-forwards silently (`SoundMute`, time-sliced in `GameEngine::netJoinFrame`), says `READY_AT`, its checksums are compared, and the host deals it bots at a tick it chooses (the `HUMAN_TAKES_OVER` rides in the confirmed inputs, so every machine applies it on the same tick). The live match never waits for a joiner. A joiner whose round trip needs more D than the round has is told `TAKEOVER_TOO_SLOW` and watches.
  - **Reconnect:** a token from `WELCOME`, saved per host (and per relay and room after 12) in the prefs file; the old incarnation is dropped and its slots offered back. A token from another address counts only once its owner has been silent 3 s, so two live windows sharing a prefs file never evict each other.
  - **Spectators** (`--spectate`, or no slot free); a machine demoted for want of a slot is dealt one again when one frees up. `kick()` works mid-round; the final-checksum wait is bounded at 8 s (07 R3).
  - **Fast-forward:** 9,000 ticks with four bots in about 0.13 s natively and 0.23–0.34 s in wasm under Node (targets 3 s and 10 s), so no snapshots.
  - **Review:** one Opus review (a reconnect got the wrong keyboard, fast-forward played sound, six state-machine holes), a fix round, a re-review (a demoted joiner stayed a spectator for good), a second small fix round. The coordinator's own ctest then caught a real lost-acknowledgement gap (a caught-up joiner's `READY_AT` kept the running keepalive from ever firing); fixed with a test (5 of 40 failing → 0 of 40 in a stall reproduction).
- **12, browser netplay** (merge 0154499):
  - **Relay:** `RELAY_AUTO` in the browser (WebTransport, then WebSocket), relay UDP natively; the host creates a room, others join by code; direct UDP stays. An "Online game" sub-menu holds Host via relay, Join by room code and (native only) 08's two direct entries. `--relay`/`--room` on the game and `bitriot_headless`.
  - **Session:** `SessionOptions::maxDelay` (8, or 12 on a relay); the sparse-pump rule (own pumps more than 1.5 s apart time nobody out, and 13's host timers move with the gap); the connect clock holds while the transport is CONNECTING. No wire change of its own.
  - **Web:** netplay is built for Emscripten; prefs in localStorage synced to `netplay.txt`; hidden tabs step from vnet's wake callback via a `MessageChannel`, plus a 1 s timer, and `GameEngine::flushNet()` sends after every stepped frame (a hidden tab keeps ~29 ticks/s at D=2); dev URL knobs (`relay_wt`, `relay_cert`, `impair`, `hiddendriver`) and test hooks only with `?test=1`.
  - **The full game's wasm build replays the golden record identically to native** (`tests/web/netplay.js golden`, every tick).
  - **Tests:** `tests/web/netplay.js` (Playwright, a local Go relay, a native host, browsers): 16 scenarios, 226 checks, including WS/WT rounds with and without impairment, hidden and throttled tabs, a 12 s freeze, browser hot-join and reconnect. About 15–20 minutes; not in ctest.
  - **Review:** one Opus review (a hidden tab sent inputs a wake late, the web NETPLAY=OFF build broke, a local game ran in a hidden tab, invite links vs saved relay, dev knobs on deployed pages), a fix round, a merge of `claude` with 13 and its integration, a re-review (a 200-checkpoint truncation loosened the round test: replaced by a hand-back to the AI so rounds end; two timer shifts untested), a final fix round, which also fixed a regression of its own (the unclamped `delayNeededBy`).
  - **Deployment** is a written note in 12's Outcome; nothing is deployed. vnet is unchanged (`v0.4.0`), so no tag is needed.
- **08, lobby UI** (merge 6c69112):
  - **Menus:** "Host online game" and "Join online game" on the main menu (inserted by an `EventManager` hook, so the web and NETPLAY=OFF menus are unchanged), host and join forms, the host-name lookup on a worker thread (`NetResolve`).
  - **Online lobby:** six rows (host-local, remote, computer, open) with team, ready and ping; the host edits map, blocks, enemies, players and teams with new lobby buttons and can remove a peer (lobby only, no ban). Start waits for everyone's ready.
  - **Protocol version 2:** `READY`, a ready byte per peer in `LOBBY`, `LEAVE` reason 4 (removed). A v1 `HELLO` gets `REJECT_VERSION`. `SessionOptions::requireReady` is on only for a host opened from the menus, so `--host` and `bitriot_headless host` behave as in 07 (`bitriot_headless join` says ready on its own; `host --require-ready 1` waits for it).
  - **Error pages** from `LobbyModel::describeEnd` for every refusal and ending; an **RmlUi overlay** in the round (waiting for, left, desync, status line) replaces 07's SDL_ttf box and window-title status; the scoreboard has host-only "Next round" / "Back to lobby".
  - **Saved settings:** `netplay.txt` (`key=value`) under `SDL_GetPrefPath`, native only. `NetGame::pump()` now restores the player's own settings as soon as a session ends (a 07 gap).
  - **Review:** one Opus review (merge after fixes, nothing blocking), one fix round of 11 items, one re-review (merge). Its four nits are follow-ups in 08's Outcome; the one behavioural one (removing a machine still CONNECTING whose WELCOME was lost does not stick) fits 13.
  - **Not changed:** no `bitriot_sim` source; build id `0x6767612d056ba8af`.
  - **Trailers:** 9 of 08's commits lack the session trailer lines; the merge commit carries them. The user is fine with that (no rewrite).
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
  - `bitriot_tests` 766 and `bitriot_sim_tests` 606, on GCC Release and Clang Release (NETPLAY=OFF: 531 and 414).
  - `ctest` 3/3. The multi-process netplay test has 16 cases and takes about 210 s.
  - `tests/web/netplay.js` passes 226/226 (coordinator's run on the merged tree).
  - The 200-seed sweep has 0 failures on both binaries.
  - The determinism gate passes: 17 variants, with `gcc-m32` not available.
  - The web smoke test passes 41/41, with netplay and in the web NETPLAY=OFF build (`--menu-lines 3`).
- **vnet:** unchanged. `claude` is at 579a410, tag `v0.4.0`, CI green (run 36); `main` was merged up to it (12c8cec) at the user's request. Read run logs with the GitHub MCP tools (`actions_list`, `get_job_logs`). BitRiot has no CI workflow.

**Next ready:** nothing briefed. Every brief in both repos is done. What remains is the user's play-tests (below) and the follow-up lists in the Outcomes of 07, 08, 12 and 13. "Beyond Bit Riot" in [`README.md`](README.md) (jfSW, Odamex, a relay UDP gateway) is the next thing to brief, at the user's request.

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
3b. **Lobby play-test (08):** the same two windows, driven from the menus (`XDG_DATA_HOME=/tmp/alice-data ./BitRiot` and `XDG_DATA_HOME=/tmp/bob-data ./BitRiot`): host and join forms, ready, settings and teams, a match with the overlay, leaving, the scoreboard's Next round and Back to lobby, removing a peer, the error pages, saved settings, and your own settings surviving. The 12 steps are at the end of 08's Outcome; it can be done in the same sitting as 07's.
3c. **Browser netplay play-test (12):** a native window and a browser tab (Chrome and Firefox) through a relay run on your machine, joined by room code. The steps are at the end of 12's Outcome (they need the relay built with Go and the web build from 09).
3d. **Hot-join play-test (13):** three windows on one machine: join mid-round, no free bot, leave, reconnect after `kill -9`, identical records. The steps are at the end of 13's Outcome. 07, 08 and 13 can be done in one sitting.
4. ~~**vnet `claude` → `main`**~~ **Done** at the user's request: vnet `main` 12c8cec (`--no-ff`, same tree as `claude` 579a410).
5. **Tags:** the cloud session can't push tags (HTTP 403), so the user tags any future vnet release from a PC. The releasing brief's Outcome gives the commands. No tag was needed in this session.

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare (e.g. test-alone seed 4652) and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.
- **Netplay limits (07 follow-ups):**
  - no authentication or encryption, and no NAT traversal;
  - ~~`canStartRound()` waits for every peer's final hash with no time limit~~ bounded at 8 s by 13;
  - a debugger pause over 10 s drops a machine;
  - ~~over a relay (12) D may need more than 8~~ 12 allows 12 on a relay;
  - the room code has 30 bits and no authentication: anyone with the code can join until the lobby is full or the round starts;
  - a hidden tab under Chrome's intensive throttling never times out a dead host while hidden (it is detected within 10 s of becoming visible); see protocol.md.
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
   - None: every brief is done (see §0).
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
