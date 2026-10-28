# 02 — Seeded simulation RNG and float guards

- **Repo:** BitRiot
- **Branch:** `netplay/02-sim-rng`
- **Depends on:** 01, and on `fix/ai-seek-energy` being merged first, since it touches the bot AI's `rand()` calls.
- **Status:** todo

## Goal

Every random decision the simulation makes comes from a game-owned PRNG, seeded per round from a value that can be shared with peers. Cosmetic and UI randomness must never consume that stream. The build must also be configured so float results are bit-identical across x86-64, aarch64 and wasm.

## Read first

- [`README.md`](README.md): decision D6.
- [`research/code-facts.md`](research/code-facts.md): §2 (every `rand()` site with file:line), §4 (floats, flags), §10 (harness seeding).
- `tests/test_main.cpp` (how `BITRIOT_TEST_SEED` seeds today) and `tests/README.md` (determinism notes).

## In scope

1. **Add a small PRNG** in `src/`, for example `SimRandom`:
   - PCG32 or xorshift-family.
   - All `uint32_t` arithmetic, with no platform-dependent types or libc calls.
   - API along the lines of `seed(uint64_t)`, `nextU32()`, and `nextInt(n)` (returns `[0,n)`).
   - Document the exact algorithm in a header comment. Other implementations, such as a future JS or Go tool, may need to reproduce it.
   - Plain modulo for `nextInt` is fine and matches today's `rand() % n` distribution. Being identical everywhere matters more than avoiding modulo bias.
2. **Two streams:**
   - The **sim stream**, which every peer advances identically.
   - A **cosmetic stream**, which may differ per machine. It covers the damage-effect animation frame (`EffectEntity.cpp:23`) and the scoreboard win text (`ScoreBoardBinder.cpp:114`).
   - Decide for each site in code-facts §2 which stream it uses. Default to the sim stream when unsure, and list your choice for every site in the Outcome section.
   - Watch for any "cosmetic" value that feeds back into sim logic, such as an animation frame used for timing.
3. **Replace every `rand()`/`srand()` in `src/`.** Afterwards, `grep -rn "rand(" src` should find only the PRNG implementation. Keep each site's semantics: `rand() % n` becomes `nextInt(n)`. SimpleBrain.cpp:70 calls in a loop, so keep the call count the same.
4. **Seed per round:**
   - The match/round config, in `GameSettings` or a new `MatchConfig` struct (brief 03 formalises this; keep it simple here), gets a `uint64_t seed`.
   - Local play picks a fresh seed per round from the clock or `std::random_device`, at the point where the round is set up.
   - Reset the sim stream from that seed in `resetGame()`.
   - Log the seed at info level, so a bad round can be reproduced.
   - Remove `srand(SDL_GetTicks())` from `MickSDLRenderer.cpp`.
5. **Harness:**
   - `BITRIOT_TEST_SEED` now seeds the round seed. A test can set the seed per round, so two rounds with the same seed are identical.
   - Update `tests/README.md` (the determinism section and the seed-sweep recipe).
6. **Float guards in CMake:**
   - Add `-ffp-contract=off` for GCC/Clang, on all targets including tests.
   - Fail configuration if `-ffast-math` or `-Ofast` is present in the flags.
   - On 32-bit x86, add `-msse2 -mfpmath=sse`.
   - Add a short comment pointing to D6.

## Out of scope

- A state hash and replay (briefs 03 and 05).
- Fixed-point (D6 contingency only).
- Changing gameplay probabilities.

## Tests

- **PRNG known-answer test:** fixed seed → the first N outputs match hard-coded values, so a platform or compiler change that alters the sequence is caught.
- **Same seed, same round:** run a full AI-only round twice in one process with the same round seed, using the existing harness round runner. Assert identical outcomes: winner, frame count, and entity count per frame, or a cheap ad-hoc digest until brief 03 adds the real hash. This fails today without a reseed.
- **Different seeds diverge:** sanity check that the seed is actually used.
- **Cosmetic isolation:** consuming the cosmetic stream (e.g. calling the win-text function) between two runs does not change the sim outcome.
- Run the full suite plus a 200-seed sweep. Results will change from today, because the random sequence is different. Report failures, not a comparison of seed numbers.

## Acceptance criteria

- No libc `rand`/`srand` in `src/`.
- Each round's seed is explicit, logged and settable.
- Two rounds with the same seed in one process produce identical simulations.
- CMake enforces the float guards.
- `tests/README.md` is updated.

## Outcome

*(fill in when done, including the stream chosen for each rand site)*
