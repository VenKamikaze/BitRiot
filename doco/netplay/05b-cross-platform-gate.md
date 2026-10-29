# 05b — Cross-platform determinism gate

- **Repo:** BitRiot
- **Branch:** `netplay/05b-cross-platform-gate`
- **Depends on:** 05
- **Status:** todo

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

*(fill in when done)*
