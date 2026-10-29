# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 29 Oct 2026, second session)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a, 03b, 04a, 04b and 05. 02b, 03a, 03b, 04a and 04b are `done (play-test pending)`; 05 is `done`.
  - 04a (merge d899b57) and 04b (84f19b1) were merged unreviewed, then reviewed together. The review found one blocking bug, `InputHandler::setJoinIndicator` calling itself (a crash or hang at round start with a keyboard player, or on any join). It was fixed with a test on `netplay/04-review-fixes` (merge 0557976).
  - 05 (merge 0c5c956) had one review and one fix round. That round bounded what a hostile record can make a reader allocate and range-checked the `--config` spec.
  - Test counts on `claude`: `bitriot_tests` 513 and `bitriot_sim_tests` 408. Both pass on GCC Release, GCC Debug and Clang. The 200-seed sweep has 0 failures on both binaries.
- **vnet:** 06, 10 and 11. The whole vnet track is complete: code merged at 26f4b0f, version 0.3.0, released as tag `v0.3.0` on b302029 (later commits are docs only).
  - A design review is on vnet branch `docs/design-review` (5fea984), in `doco/design-review.md`. It makes 15 recommendations, none breaking. **R1 (high, before 12):** the relay client times out after a gap in `vnet_poll()` (a hidden tab or a sleeping laptop). It is not merged or acted on yet; that is the user's call.

**Next ready, in parallel:**

- **05b** (cross-platform determinism gate): normal review flow. It uses `bitriot_headless` and the golden record from 05.
- **09** (WASM port): needs only 03b. Archive downloads from github.com still return 403 in the cloud session, so use the user's `emscripten-ports` branch (brief 09, "Ports").

They touch different areas, but both may touch `CMakeLists.txt`. Give each its own worktree, merge one, then merge `claude` into the other before review. After those: 07 → 08, then 12 and 13.

**In flight:** nothing. No branch has unmerged work, except vnet `docs/design-review` (docs only, awaiting the user).

**Decisions taken while the user was away** (the coordinator's recommendations; the user may revisit them):

- **`hitThisTick` is not in `stateHash()`.** The 04b brief asks for new per-slot state to be hashed. This flag is cleared each step and nothing in the sim reads it, so it cannot affect later ticks. The joint review agreed. Hashing it would move every digest for no gain.
- **`Simulation::step()` throws on a malformed control event** (an out-of-range slot or an unknown kind). The netplay host must validate peers' events before it rebroadcasts them; this is in 04b's 07 follow-ups.
- **The 05 build id is a hash of the sim source, not the git commit.** It is reproducible across checkout paths and line endings. `replay` enforces the data hash but only notes a build-id mismatch, so the golden record survives source edits that don't change behaviour. 07 should reject a build-id mismatch in `HELLO`.
- **The record format's `MAX_TICKS` was lowered** from 4,194,304 to 1,048,576 ticks (about 9.6 hours at 33 ms per tick). A record also holds at most 24 control events per tick and 65,536 in total. This bounds the worst legal record at about 33 MB.
- **05 ran in parallel with the 04 fix round,** then merged `claude` in before its review, to save wall time.

**Waiting on the user** (ask about these; don't block on them):

1. **Play-test** of 02b (fixed-point movement feel), 03a (round flow, scoreboard), 03b (frame loop, catch-up, rumble), 04a (keyboard and controller feel, the same-tick tap fix) and 04b (join, keyboard takeover, controller unplug, rumble). One session on `claude` covers all five; the Arch commands are in the Outcomes of 02b, 03b, 04a and 04b. Record the result in each Outcome.
2. **vnet tags.** The cloud session can't push tags (HTTP 403), so the user tags from a PC. `v0.1.0` (5847e44) and `v0.2.0` (c8be4df) are pushed. `v0.3.0` is meant for b302029. Check with `git ls-remote --tags https://github.com/VenKamikaze/vnet`, and ask the user if it's missing. vnet's README already pins `v0.3.0`.
3. **vnet `claude` → `main`** merge: the user's call.
4. **Brief 09's blocked Emscripten ports are solved.** The user pushed the six GitHub archives to branch `emscripten-ports`. They are verified against Emscripten 6.0.13's hashes, and brief 09 ("Ports") explains how to install them into the Emscripten cache.
5. **CI has never been observed.** Neither repo's GitHub Actions runs could be watched from the cloud session. Ask the user to check vnet's Actions tab (Linux, macOS, ASan, MinGW/Wine, Go, fuzz, Docker, the browser job).

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare, e.g. test-alone seed 4652, and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.

**Lessons from the first session:**

- **The container can restart mid-run** and kill background work. Push feature branches as soon as an implementer reports, not only at merge. If a reviewer was interrupted, resume it (it keeps its context) rather than starting a new one.
- **Sweeps:** run them from the build dir *after* building the `BitRiot` target (or `bitriot_runtime_links`), which creates the `data/` and `assets/` links. Also sweep `bitriot_sim_tests`, and build with Clang too.
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
- vnet's [`doco/README.md`](https://github.com/VenKamikaze/vnet/blob/claude/doco/README.md): the status table for briefs 06, 10 and 11 lives there.

Then read the brief(s) you are about to start, plus the **Outcome** sections of every brief they depend on.

## 3. Pick the next brief(s)

1. **Find ready briefs.** From the status tables in both repos, list every brief whose dependencies are all done.
2. **Tracks that can run in parallel now:**
   - BitRiot's determinism track: 05b (01–05 are done).
   - BitRiot WASM: 09.
   - vnet is complete (06, 10, 11).

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
   - "commit on the branch; don't push or merge".

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
