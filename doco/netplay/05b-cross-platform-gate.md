# 05b — Cross-platform determinism gate

- **Repo:** BitRiot
- **Branch:** `netplay/05b-cross-platform-gate`
- **Depends on:** 05
- **Status:** done

## Goal

Prove, before any netplay is built on top of it, that the simulation produces bit-identical per-tick hashes across compilers, optimisation flags, CPU architectures and WebAssembly, by replaying the golden record from brief 05 under each one.

After 02b the sim is integer-only (D6), so float maths is no longer the risk. What this gate catches is everything else: undefined behaviour that optimisers treat differently, libstdc++ vs libc++ differences, argument evaluation order, uninitialised state, and 32-bit `long` on wasm32.

If any variant diverges, find the first diverging tick and fix the cause. If the fix is outside this brief's scope, stop and ask the user.

## Read first

- [`README.md`](README.md): D6, and the cloud network limits.
- [`research/code-facts.md`](research/code-facts.md) §4 and §12 (libc++ notes).
- The Outcome sections of briefs 02b (fixed-point), 03b (what SDL use, if any, remains in `bitriot_sim`) and 05 (`bitriot_headless` and the golden record).

## In scope

Each item below is a build of `bitriot_headless` that replays the golden record and must match its checkpoints. Wrap them all in one script, `scripts/determinism-gate.sh`, that prints a pass/fail table.

1. **GCC versus Clang.** Both are installed in the container.
2. **An aggressive-flags build:** `-O3 -march=native -ffast-math`. The sim has no floats, so it must match. A divergence points at UB or at a float that crept back into the sim.
3. **aarch64 under qemu-user (optional).**
   - Use the `g++-aarch64-linux-gnu` and `qemu-user` apt packages, if they're available.
   - This is realistic only because `bitriot_sim` needs no SDL (03b).
   - If the packages aren't available, say so in the Outcome.
4. **wasm under Node.**
   - Install emsdk pinned to a specific version. Its binaries download from storage.googleapis.com, which is reachable.
   - **Reuse vnet's pin.** vnet already pins emsdk in its `scripts/setup-emsdk.sh` (emsdk revision `d7b4130688bfa348392b921ab04015aa15c4f154`, Emscripten **6.0.13**; installed with git, binaries from storage.googleapis.com). Use the same revision and version here so BitRiot and vnet are built by one compiler.
   - Configure the wasm (and aarch64) builds with **`-DBITRIOT_BUILD_GAME=OFF`** (added by 03b): it skips the SDL/RmlUi/GL `find_package` calls, so only `bitriot_sim` and its tests are built. `bitriot_sim` has no SDL at all, so no Emscripten ports are needed for the gate.
   - Build `bitriot_headless` with `emcmake`, using `-sNODERAWFS=1` so it can read the record and data files directly, and with the same exceptions mode the game will use: `-fwasm-exceptions`.
     - If the Node version in the container lacks support for that mode, use `-sWASM_LEGACY_EXCEPTIONS` or `-fexceptions`, and say which.
   - If `bitriot_sim` still needs SDL core headers (per 03b's Outcome), provide the Emscripten `sdl2` port by git-cloning its source at the tag Emscripten expects and setting `EMCC_LOCAL_PORTS=sdl2=<path>`.
     - GitHub archive downloads are blocked in this container; see the README.
   - Add an emsdk setup step to the scripts: either `scripts/setup-emsdk.sh`, or a flag on `cloud-setup.sh`.
5. **Record the results** as a table in the Outcome, and in `tests/README.md`.

## Out of scope

- The full game's wasm build (09).
- CI for the gate. Running the script locally is enough, and note how to run it.

## Tests

- The gate script itself.
- A **negative control:** a test-only flag that perturbs one sim value (for example adding 1 to one entity's fixed-point offset at tick 100) makes the gate fail at the right tick. This proves the gate can catch a difference.

## Acceptance criteria

- `scripts/determinism-gate.sh` runs every available variant and reports pass/fail per variant.
- The wasm-under-Node replay matches native.
- Every available variant matches, with evidence.

## Outcome

**Status: done.** `scripts/determinism-gate.sh` builds `bitriot_headless` under 18 variants and replays the golden record plus six more rounds (a hash on every tick) with each. **17 variants ran and all 17 match the reference, on every tick of all seven records; one (`gcc-m32`) is not available in this container.** The negative control is caught at exactly tick 100 on all 17. No simulation source changed: the six golden digests in `SimulationTest` and `tests/data/golden-round.brmr` are byte-identical, and the build id is still `0x6767612d056ba8af` on every variant. No divergence was found, so nothing in the simulation needed fixing.

**How to run it**

```bash
scripts/setup-emsdk.sh                       # once, for the wasm variants (pinned emsdk d7b4130, Emscripten 6.0.13); the gate never installs it
source .emsdk/emsdk_env.sh                   # or: EMSDK_DIR=/path/to/emsdk scripts/determinism-gate.sh
scripts/determinism-gate.sh                  # all available variants, a pass/fail table, exit 0 only if all match and the control is caught
scripts/determinism-gate.sh --list           # variants and why one is unavailable
scripts/determinism-gate.sh --only gcc-O2,clang-O2,wasm-O2
scripts/determinism-gate.sh --strict         # an unavailable variant counts as a failure
```

About three minutes with warm builds (valgrind and qemu dominate), about ten from scratch on the shared 4 cores. A missing toolchain gives a "not available" row with the reason; with no emsdk (neither `$EMSDK_DIR`, default `./.emsdk`, nor `emcc` on `PATH`) the four wasm rows say so and the rest run. The how-to, the table and the rules are also in `tests/README.md` ("Cross-platform determinism gate").

**Results** (GCC 13.3, Clang 18.1, Emscripten 6.0.13, Node 22.22.0, emsdk's Node 24.19.0, qemu-user 8.2.2; pass = `replay --hashes` exits 0 and its stdout is byte-identical to the reference's for all 7 records, `version` is identical, four exception probes behave identically, and the negative control is caught at tick 100)

| Variant | Toolchain and flags | Result | Evidence |
| --- | --- | --- | --- |
| `gcc-O2` (reference) | GCC `-O2 -DNDEBUG` | pass | exit 0, 2,769 checkpoints of the golden record verified |
| `gcc-O0` | GCC `-O0 -g`, asserts on | pass | 7 records identical |
| `gcc-O3-native-fastmath` | GCC `-O3 -march=native -ffast-math` (sapphirerapids, AVX-512) | pass | 7 records identical |
| `gcc-initpattern` | GCC `-O2 -ftrivial-auto-var-init=pattern` | pass | 7 records identical (uninitialised locals) |
| `gcc-asan-ubsan` | GCC `-O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined` | pass | no report; any UB would abort |
| `gcc-O0-valgrind` | the `gcc-O0` build under memcheck (`--error-exitcode=99`) | pass | no error; golden and one small round only (about 300x slower) |
| `gcc-O2-malloc-perturb` | the `gcc-O2` build, `MALLOC_PERTURB_=165` | pass | 7 records identical (uninitialised heap) |
| `clang-O2` | Clang `-O2`, libstdc++ | pass | 7 records identical |
| `clang-O0` | Clang `-O0 -g`, asserts on | pass | 7 records identical |
| `clang-O3-native-fastmath` | Clang `-O3 -march=native -ffast-math` | pass | 7 records identical |
| `clang-libc++` | Clang `-O2 -stdlib=libc++` (LLVM libc++ 18, installed with apt) | pass | 7 records identical: libstdc++ vs libc++ settled |
| `aarch64-O2` | `aarch64-linux-gnu-g++` `-O2`, `qemu-aarch64 -L /usr/aarch64-linux-gnu` | pass | 7 records identical |
| `aarch64-O3-fastmath` | the same, `-O3 -ffast-math` | pass | 7 records identical |
| `wasm-O2` | Emscripten `-O2 -fwasm-exceptions` (legacy EH), `-sNODERAWFS=1`, Node 22.22.0 | pass | 7 records identical; `version` identical, so the same build id and data hash |
| `wasm-O3-fastmath` | Emscripten `-O3 -ffast-math -fwasm-exceptions`, Node 22.22.0 | pass | 7 records identical |
| `wasm-exnref-O2` | Emscripten `-O2 -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=0` (standard EH), Node 22.22.0 | pass | 7 records identical |
| `wasm-O2-emsdk-node` | the `wasm-O2` build on emsdk's Node 24.19.0 | pass | 7 records identical |
| `gcc-m32` | GCC `-m32` (ILP32) | not available | `g++-multilib` and `g++-aarch64-linux-gnu` conflict on Ubuntu (my first apt install of multilib removed the cross compiler; I reinstalled it). The wasm32 variants cover 32-bit `long` and `size_t` |

The seven records: the golden round (2,768 ticks, 4 players, humans scripted) and six all-AI rounds the reference plays at the start of every run (seeds fixed, a checkpoint on every tick): 17x13 2 players (2,812 ticks), 13x11 6 players (2,278), 21x15 two teams (2,464), 15x11 3 players no blocks no spawner (1,266), 33x25 4 players 70% blocks (6,000, the cap: the round did not end) and 25x19 5 players solid blocks (6,000, the cap). About 23,600 hashed ticks per variant, all identical to GCC `-O2`.

**Proof that the gate fails at the right tick**

1. *Negative control, on every run, on every variant.* `bitriot_headless replay --perturb-at 100 golden-round.brmr` adds one unit to one block's x offset after tick 99. The gate runs it through the same comparison and requires a failure with exit 1 whose first differing line is `hash 100 ...`. Result: "caught, fails at tick 100 (expected 100), exit 1" on all 17 variants, wasm and qemu included. If it were not caught the gate would exit 1 itself.
2. *A real, compiler-dependent fault* (a scratch copy of the tree, not committed): `Simulation::step` got, from tick 150, `firstWasEvaluatedFirst(next(), next())` (two calls in one argument list) with an RNG draw when it is true. Result: `gcc-O2` passed (GCC on x86-64 evaluates right to left, so the draw never happened and the golden record still held), while `clang-O2`, `aarch64-O2` and `wasm-O2` each reported "golden: tick 150: reference 0xaa7abd4a93b3621a, got 0xd9778121a8bd59f1" and the gate exited 1. Tick 150 is the first tick the added code runs.
3. *The exception probes bite.* A wasm build with no exception mode (Emscripten's default) replays the golden record perfectly and then aborts on the first thrown exception; the probes (a missing file, a bad option, `replay` with no file, a `throw` inside `Simulation::step` caught in the replayer) fail it.
4. The comparison helpers were also tried on a changed hash, a truncated output, an extra line, a different header and an empty output.

**Exceptions mode for wasm: `-fwasm-exceptions` (legacy wasm EH, Emscripten 6.0.13's default for that flag), and it works in Node 22.22.0.** It is the mode the brief names and the one 09 will use (the current browsers all run legacy wasm EH). Nothing was lacking in the container's Node, so no fallback to `-fexceptions` was needed. Also tested: the standard (exnref) EH (`-sWASM_LEGACY_EXCEPTIONS=0`), which Node 22.22.0 and Node 24.19.0 both run without a flag, with identical results, so 09 can pick either on determinism grounds. A build with **no** exception mode is not an option: the simulation's refusals are caught C++ exceptions (code-facts §12).

**Wasm replay speed** (what 05's follow-up asked for; `bitriot_headless replay`, median of 7 runs, Node 22.22.0, `-O2`, shared 4-core container, so rough):

| Workload | Native GCC `-O2` | wasm `-O2` | Ratio |
| --- | --- | --- | --- |
| process start-up (`version`) | 4 ms | 54 ms (loading and compiling the module) | |
| golden round, 2,768 ticks, a hash every tick | 45 ms, so about 67,000 ticks/s | 122 ms, so about 41,000 ticks/s | 1.7x |
| 6,000 ticks, 25x19, 5 players, only the first and last hash | 60 ms (about 107,000 ticks/s) | 166 ms (about 54,000 ticks/s) | 2.0x |
| 6,000 ticks, 33x25, 4 players, only the first and last hash | 76 ms (about 83,000 ticks/s) | 208 ms (about 39,000 ticks/s) | 2.2x |
| the same 25x19 round, a hash every tick | 402 ms (about 15,000 ticks/s) | 806 ms (about 8,000 ticks/s) | 2.0x |

So wasm runs the simulation at roughly 40,000 to 55,000 ticks a second, one half to one third of native. A joiner's fast-forward of 9,000 ticks (5 minutes at 30 ticks a second) is about 0.2 to 0.25 s, plus about 50 ms to load, even on the biggest maps; **hashing every tick costs more than stepping on a big map** (about 60 microseconds a tick natively, 110 in wasm), so a fast-forward should hash at checkpoints only (every 30 ticks, as the recorder does). The `-O3 -ffast-math` and standard-EH builds are within noise of `-O2`. Chrome's V8 is the engine Node uses, so a browser should be similar, but that was not measured. aarch64 under qemu is correctness only (about 1.8 s for the golden round).

**What landed**

1. `scripts/determinism-gate.sh` (variants, availability probes, per-variant builds in `build-gate/`, records, comparison, exception probes, negative control, table, exit status; options above). Variants are defined in one table at its top.
2. `scripts/setup-emsdk.sh`: a copy of vnet's (same pins: emsdk `d7b4130688bfa348392b921ab04015aa15c4f154`, Emscripten 6.0.13; `EMSDK_DIR` defaulting to `<repo>/.emsdk`; only the header comments differ), no port handling. After merging `claude`, the file is 09's version (the same base plus `--with-ports`, which seeds the Emscripten ports; the gate does not use it). `.emsdk` is in `.gitignore`.
3. `bitriot_headless replay --perturb-at TICK` (`src/headless/HeadlessCli.cpp`, **not** a `bitriot_sim` source) and four `HeadlessCliTest` cases (three, plus one from the fix round). Tests: `bitriot_sim_tests` 408 to **412**, `bitriot_tests` 513 to **517**, GCC Release, all pass. With the first version of the option (it nudged a player, whose offset the next step restores) the two failing tests showed the control would not have been caught; the block target fixed that.
4. code-facts §12 (Emscripten facts, evaluation order, libstdc++ vs libc++ now checked) and this Outcome; `tests/README.md` has the section and the table.

**Review fix round** (one Opus review, "merge after fixes"; it reproduced the table and the break tests; no simulation source, digest or record changed)

1. *Blocking:* with no emsdk, `wasm-O2-emsdk-node` stayed "available", failed because `wasm-O2` was missing, and the gate exited 1. Now any variant that runs another variant's binary is unavailable when its base is ("needs wasm-O2: ..."). Run with `PATH` without emsdk and `EMSDK_DIR=/nonexistent`: all four wasm rows read "not available", exit 0.
2. `--no-extra` aborted (the Valgrind variant named a record that does not exist then); a variant's record list is now filtered to the records that exist.
3. A crash or sanitizer abort was reported as a wrong tick (buffered stdout was lost). `replay --hashes` now flushes after every line (stdout unchanged), and the gate reports exit codes other than a mismatch, or output without the closing `replay:` line (UBSan exits 1), as "not a hash mismatch" with the first `runtime error`/`ERROR:`/`==pid==` line and the last tick hashed. Checked on a scratch tree with a signed overflow added at tick 150: "runtime error: signed integer overflow ...; last hash printed: 149".
4. The pinned `$EMSDK_DIR` (default `./.emsdk`) now wins over an `emcc` on `PATH`; a version other than the pin fails under `--strict`.
5. Nits: a replay that diverges before `--perturb-at`'s tick reports the divergence (exit 1) (new test); help text and a comment say the block must survive the tick; a reference that fails its own record is a failure (exit 1, message), not a setup error; Valgrind is probed by running it; Arch package names alongside apt; the CMake generator of an existing build directory is reused; `--clean` refuses an empty value, `/`, `$HOME`, the repository and its parent; the hard-coded `EMSDK_DIR` is gone from `tests/README.md`.

**Sweep.** No simulation source changed (the build id did not move), so no 200-seed sweep of the two suites was needed. The new `HeadlessCliTest` cases alone were run over seeds 1 to 200: 0 failing seeds.

**Deviations and decisions**

- **The negative control is a CLI option, not a flag inside the simulation.** The brief suggested a test-only flag perturbing one sim value at tick 100. `replay --perturb-at` does that from outside `bitriot_sim` (it uses the `onTick` hook and the public entity API), so the simulation, its build id and the digests are untouched, and it can run on every variant from the one binary (wasm and qemu included). It perturbs a block, not a player (above).
- **More variants than the brief lists**, because the brief's goal names UB, uninitialised state and libstdc++ vs libc++: `-O0` builds (asserts on), `-ftrivial-auto-var-init=pattern`, `MALLOC_PERTURB_`, ASan+UBSan, Valgrind, Clang with libc++, aarch64 and wasm at `-O3 -ffast-math`, wasm standard EH, wasm on two Node versions. Seven records, not only the golden one, so a divergence has more rounds to show up in (map sizes 13x11 to 33x25, 2 to 6 players, teams, no blocks, solid blocks, spawner off).
- **Build type `Gate`.** The gate configures with `-DCMAKE_BUILD_TYPE=Gate` and the flags in `CMAKE_CXX_FLAGS`, because the CMakeLists adds `-O2` for `Release` after `CMAKE_CXX_FLAGS` and would silently override `-O3`. The compile commands were checked for each flag set.
- **libc++ and cross compilers were installed with apt in this container** (`libc++-18-dev`, `libc++abi-18-dev`; `g++-aarch64-linux-gnu` and `qemu-user` by the coordinator). `gcc-m32` is not available (the conflict above).
- **Emscripten ports and `EMCC_LOCAL_PORTS` were not needed**: `bitriot_sim` has no SDL (03b), so nothing is downloaded beyond emsdk itself (the gate does not call `--with-ports`; that is for 09's game build).
- **The exception probes compare stdout, the exit code and stderr's `error:`/`warning:`/`note:` lines only**, because builds without `NDEBUG` print DEBUG log lines on stderr.
- The emsdk install is the coordinator's (`/home/user/.emsdk`, set with `EMSDK_DIR`); the gate never installs one.
- `PROMPT.md` still says "four pinned rounds" (there are six); not touched here.

**Follow-ups**

- **07 (lockstep):** the cross-platform risk is now measured, not assumed: peers on GCC x86-64, Clang, aarch64 and wasm will agree if they run the same source and data. `HELLO`'s build id and data hash are identical across all those builds (checked by the gate's `version` comparison), which is what makes the check meaningful. A desync dump is still `Simulation::dumpState()`. Keep to the `SimRandom.h` rule of no two draws in one expression: argument order differs between x86-64 GCC and everything else, and the gate shows it as a first-tick divergence.
- **09 (WASM port):** build the game with `-fwasm-exceptions` (default legacy EH); Emscripten's default (no exceptions) aborts on the first `throw` the simulation makes. 09 has merged and `scripts/setup-emsdk.sh` is its version. The headless binary's link flags that worked are `-sNODERAWFS=1 -sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1` (the game needs its own set); the 64 KB default stack was enough for the simulation. Re-run the gate after any change to flags for the game's wasm build.
- **12 (browser netplay):** the wasm simulation matches native tick for tick, so a browser peer and a native peer can share a lockstep session. The browser engine's V8 is the same one Node runs, but the gate runs under Node, not a browser; 12's own test should still replay a record in a browser once.
- **13 (hot-join):** use the numbers above (about 40,000 to 55,000 ticks a second in wasm, hash at checkpoints only).
- **CI.** The gate is a local script. A CI job would run `scripts/setup-emsdk.sh` then `scripts/determinism-gate.sh --strict`; Valgrind and the aarch64 variants make it about three minutes of runs after the builds.
- A `-m32` run would be a cheap extra on a machine with `g++-multilib` and no aarch64 cross compiler.
