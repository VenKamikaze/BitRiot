# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 29 Oct 2026, end of the second session)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a, 03b, 04a, 04b and 05. 02b, 03a and 05 are `done`; 03b, 04a and 04b are `done (play-test pending)` for their controller parts only (§0 "Waiting on the user").
  - 04a (merge d899b57) and 04b (84f19b1) were merged unreviewed, then reviewed together. The review found one blocking bug, `InputHandler::setJoinIndicator` calling itself (a crash or hang at round start with a keyboard player, or on any join). It was fixed with a test on `netplay/04-review-fixes` (merge 0557976).
  - 05 (merge 0c5c956) had one review and one fix round. That round bounded what a hostile record can make a reader allocate and range-checked the `--config` spec.
  - Test counts on `claude`: `bitriot_tests` 513 and `bitriot_sim_tests` 408. Both pass on GCC Release, GCC Debug and Clang. The 200-seed sweep has 0 failures on both binaries.
- **vnet:** 06, 10, 11 and **14**, all merged into vnet's `claude`. Release tags `v0.1.0`, `v0.2.0`, `v0.3.0` and **`v0.4.0`** (14, merge eb1feeb) are all pushed.
  - 14 came from a design review of 0.3.0 (`doco/design-review.md`, annotated with what 14 took up). It fixed the relay client dropping its session after a hidden tab or a sleep, and added `vnet_set_impair`, burst loss, a `wake` callback for browsers, native `RELAY_AUTO` with `relay_udp`, `relay_rtt_ms`/`max_members`, `vnet_wait`, `vnet_resolve_host`, `vnet_version` and `doco/integration.md`. Its review found a blocking bug (a `vnet_wait()` loop never detected a dead relay), fixed with tests. Briefs 07 and 12 pin `v0.4.0` and read `integration.md` and 14's follow-ups.
  - **vnet CI now runs and is green** on all seven jobs (Linux GCC/Clang, ASan+UBSan, macOS, Go relay, browser, MinGW/Wine). Until 29 Oct the workflow file was invalid YAML, so CI had never run. The first run's failures were test-side (a relay test overflowing the runners' smaller UDP buffer, macOS delivering loopback packets late, a NAT stand-in holding its lock while sleeping) and are fixed. CI builds pushes to `main`, `claude`, `vnet/**` and `fix/**`. Read a run's logs with the GitHub MCP tools (`actions_list`, `get_job_logs`); check the latest run on a branch before reporting it done.
  - BitRiot has no CI workflow.

**Next ready, in parallel:**

- **05b** (cross-platform determinism gate): normal review flow (implement, review, fix, re-review if needed). It replays `tests/data/golden-round.brmr` with `bitriot_headless` under each toolchain. Read 05's Outcome follow-ups for 05b first. vnet's `scripts/setup-emsdk.sh` installs the same pinned Emscripten 6.0.13 by `git fetch`.
- **09** (WASM port, local play): needs only 03b. Normal review flow. Archive downloads from github.com still return 403 in the cloud session, so use the user's `emscripten-ports` branch (brief 09, "Ports").

They touch different areas, but both may touch `CMakeLists.txt` and the emsdk setup. Give each its own worktree, merge one, then merge `claude` into the other before its review. After those: 07 → 08, then 12 and 13. vnet has no briefed work left.

**In flight:** nothing. No branch in either repo has unmerged work.

**Decisions taken in the second session while the user was away** (they may revisit them):

- **`hitThisTick` is not in `stateHash()`.** The 04b brief asks for new per-slot state to be hashed. This flag is cleared each step and nothing in the sim reads it, so it cannot affect later ticks. The joint review agreed. Hashing it would move every digest for no gain.
- **`Simulation::step()` throws on a malformed control event** (an out-of-range slot or an unknown kind). The netplay host must validate peers' events before it rebroadcasts them; this is in 04b's 07 follow-ups.
- **The 05 build id is a hash of the sim source, not the git commit.** It is reproducible across checkout paths and line endings. `replay` enforces the data hash but only notes a build-id mismatch, so the golden record survives source edits that don't change behaviour. 07 should reject a build-id mismatch in `HELLO`.
- **The record format's `MAX_TICKS` was lowered** from 4,194,304 to 1,048,576 ticks (about 9.6 hours at 33 ms per tick). A record also holds at most 24 control events per tick and 65,536 in total. This bounds the worst legal record at about 33 MB.
- **vnet 14's open choices** (in the brief): delayed datagrams survive `vnet_set_impair`; native `RELAY_AUTO` uses `relay_udp`, else a scheme-less `relay`, else `VNET_EINVAL`; protocol v1 grows only by new `*_EXT` frame types with padding kept zero; `10.0.0.254`/`.255` are reserved (the relay caps `-max-members` at 253).

**Waiting on the user** (ask about these; don't block on them):

1. **Controller play-test.** 02b and 03a passed (29 Oct 2026: two rounds, one keyboard player against three CPU players). Still pending, all needing a pad: 03b (rumble, hotplug, join selector, Options map size), 04a (pad feel, the same-tick tap fix on pad and keyboard) and 04b (pad join, keyboard takeover of a CPU player, pad unplug, rumble). One session with a pad and a keyboard covers all three; the Arch commands are in their Outcomes. Record the result in each Outcome.
2. **vnet `claude` → `main`** merge: the user's call.
3. **Tags:** the cloud session can't push tags (HTTP 403), so any future vnet release is tagged by the user from a PC; the releasing brief's Outcome gives the commands.

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare, e.g. test-alone seed 4652, and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.

**Lessons so far:**

- **The container can restart mid-run** and kill background work. Push feature branches as soon as an implementer reports, not only at merge. If a reviewer was interrupted, resume it (it keeps its context) rather than starting a new one.
- **Sweeps:** run them from the build dir *after* building the `BitRiot` target (or `bitriot_runtime_links`), which creates the `data/` and `assets/` links. Also sweep `bitriot_sim_tests`, and build with Clang too.
- **Parallel briefs:** run independent briefs in separate worktrees (`git worktree add`), and tell an implementer when `claude` has moved so it merges `claude` in before its review.
- **Reviews find real bugs** even when every test passes (04's join-indicator recursion, 05's allocation bomb, 14's `vnet_wait` loop): tests that pass `nullptr` or skip the real glue hide them. Ask reviewers to probe the untested glue.
- **Golden digests:** the six pinned rounds in `SimulationTest`, and the golden match record `tests/data/golden-round.brmr` (brief 05), must stay byte-identical across refactors. A legitimate sim change moves them, and the commit must say so (see `tests/README.md`).

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
   - BitRiot's determinism track: 05b (01–05 are done).
   - BitRiot WASM: 09.
   - vnet: no briefed work left (06, 10, 11 and 14 are done; `v0.4.0`).

   Later briefs (07, 08, 12, 13) follow the dependency table.
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
