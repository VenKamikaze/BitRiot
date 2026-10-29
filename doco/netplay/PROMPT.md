# Kick-off prompt: netplay, WASM and vnet

You are the **coordinator** for a multi-brief project across two repos:

- [`VenKamikaze/BitRiot`](https://github.com/VenKamikaze/BitRiot), a C++ SDL2/RmlUi bomberman-style game. This file lives here.
- [`VenKamikaze/vnet`](https://github.com/VenKamikaze/vnet), a virtual-UDP networking library and Go relay for games.

The goal is deterministic lockstep online multiplayer for Bit Riot, a browser (WASM) build, and vnet underneath both.

## 0. Where things stand (hand-off, 29 Oct 2026)

**Done and merged into `claude`:**

- **BitRiot:** 01, 02, 02b, 03a and 03b. 02b, 03a and 03b are `done (play-test pending)`.
- **vnet:** 06, 10 and 11. The whole vnet track is complete: code merged at 26f4b0f, version 0.3.0, released as tag `v0.3.0` on b302029 (later commits are docs only).

**Next ready, in parallel:**

- **04a** (input sampling): starts the 04a → 04b → 05 → 05b → 07 → 08 chain.
- **09** (WASM port): needs only 03b.

They touch different areas (input/sim vs `main.cpp`, CMake and renderers) but both may touch `CMakeLists.txt` and `GameEngine`. Give each its own worktree, merge one, then merge `claude` into the other before review. After those: 04b → 05 → 05b → 07 → 08, then 12 and 13.

**In flight:** nothing. No branch has unmerged work.

**Waiting on the user** (ask about these; don't block on them):

1. **Play-test** of 02b (fixed-point movement feel), 03a (round flow, scoreboard) and 03b (frame loop, catch-up, rumble). One session on `claude` covers all three; the Arch commands are in 02b's and 03b's Outcomes. Record the result in each Outcome.
2. **vnet tags.** The cloud session can't push tags (HTTP 403), so the user tags from a PC. `v0.1.0` (5847e44) and `v0.2.0` (c8be4df) are pushed. `v0.3.0` is meant for b302029. Check with `git ls-remote --tags https://github.com/VenKamikaze/vnet`, and ask the user if it's missing. vnet's README already pins `v0.3.0`.
3. **vnet `claude` → `main`** merge: the user's call.
4. **CI has never been observed.** Neither repo's GitHub Actions runs could be watched from the cloud session. Ask the user to check vnet's Actions tab (Linux, macOS, ASan, MinGW/Wine, Go, fuzz, Docker, the browser job).

**Known open issues:**

- **Bot livelock** (in `TODO`): a boxed-in bot with its own eggs never bombs out. It is rare, e.g. test-alone seed 4652, and not root-caused.
- **Co-op stalemate** (in `TODO`).
- **vnet relay limits:** a forged-source flood can delay new handshakes while it lasts. This is documented in vnet's `relay-protocol.md`.

**Lessons from the first session:**

- **The container can restart mid-run** and kill background work. Push feature branches as soon as an implementer reports, not only at merge. If a reviewer was interrupted, resume it (it keeps its context) rather than starting a new one.
- **Sweeps:** run them from the build dir *after* building the `BitRiot` target (or `bitriot_runtime_links`), which creates the `data/` and `assets/` links. Also sweep `bitriot_sim_tests`, and build with Clang too.
- **Golden digests:** the four pinned rounds in `SimulationTest` must stay byte-identical across refactors. A legitimate sim change moves them, and the commit must say so (see `tests/README.md`).

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
   - BitRiot's determinism track: 04a → 04b → 05 → 05b (01–03b are done).
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
