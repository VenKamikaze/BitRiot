# 02 — Seeded simulation RNG

- **Repo:** BitRiot
- **Branch:** `netplay/02-sim-rng`
- **Depends on:**
  - brief 01
  - `fix/ai-seek-energy`, which changed the bot AI's `rand()` calls (merged into `claude` in 7d6cf6a)
- **Status:** todo

## Goal

Every random decision the simulation makes comes from a game-owned PRNG, seeded per round from a value that can be shared with peers. Cosmetic and UI randomness never consume that stream.

Floats are not this brief's concern: the user chose fixed-point maths (D6), which brief [02b](02b-fixed-point-sim.md) delivers next.

## Read first

- [`README.md`](README.md): decisions and workflow.
- [`research/code-facts.md`](research/code-facts.md):
  - §2: every `rand()` site, with file:line
  - §10: how the harness seeds today
  - §12: RNG consumed in constructors, `% 0`, evaluation order
- `tests/test_main.cpp` and `tests/README.md` (determinism notes).
- Re-find the `rand()` sites with `grep -rn "rand(" src`. `fix/ai-seek-energy` changed PlayerCharacterEntity.

## In scope

1. **A small PRNG** in `src/` (e.g. `SimRandom`).
   - PCG32 or a xorshift-family generator, using only `uint32_t`/`uint64_t` arithmetic and no libc.
   - API: `seed(uint64_t)`, `nextU32()`, `nextInt(n)` returning a value in `[0,n)`.
   - **`nextInt(0)` and `nextInt(1)` return 0.** Today `rand() % 0` is undefined behaviour when SpawningPool's min and max delay are equal.
   - Document the exact algorithm in a header comment, so other implementations (JS or Go tools) can reproduce it.
   - Plain modulo is fine. Being the same everywhere matters more than modulo bias.
2. **Two streams.**
   - **Sim stream:** every peer advances it identically.
   - **Cosmetic stream:** may differ per machine. It covers `EffectEntity.cpp:23` (damage-effect animation frame) and `ScoreBoardBinder.cpp:114` (win text).
   - **The cosmetic stream is never hashed, recorded or sent.** Its values must not feed back into sim logic. Check that the effect's animation frame doesn't drive any timing.
   - Decide the stream for every site, defaulting to sim, and list each choice in the Outcome.
3. **Replace every `rand()`/`srand()` in `src/`.**
   - `rand() % n` becomes `nextInt(n)`. Keep the call counts the same, including SimpleBrain's loop.
   - **Rule: at most one sim-RNG draw per full-expression, directly or through helpers.** GCC and Clang (which Emscripten uses) evaluate function arguments in different orders. Put the rule in the PRNG header.
4. **Seed per round.**
   - Add a `uint64_t seed` to the round configuration. Put it in `GameSettings` for now; 03a moves it into `MatchConfig`.
   - Local play picks a fresh seed each round from the clock or `std::random_device`.
   - **Reseed the sim stream as the very first thing in `resetGame()`**, before the `SpawningPool` and player constructors, which consume RNG (code-facts §12).
   - Log the seed at info level.
   - Remove `srand(SDL_GetTicks())` from `MickSDLRenderer.cpp`.
5. **Harness.** `BITRIOT_TEST_SEED` sets the round seed, and a test can also set it per round. Update `tests/README.md`.
## Out of scope

- The state hash and replay (03a, 05).
- Fixed-point maths (02b).
- Changing gameplay probabilities.

## Tests

- **PRNG known-answer test:** a fixed seed produces the first N outputs, hard-coded in the test.
- **Same seed, same round:** play a full AI-only round twice in one process with the same seed, and compare a cheap digest: winner, frame count, and entity count per frame. This test fails today.
- **Different seeds diverge.**
- **Cosmetic isolation:** consuming the cosmetic stream between two runs doesn't change the sim outcome.
- Run the full suite and a 200-seed sweep. Outcomes will change because the sequence changes, so report failures, not seed-by-seed comparisons.

## Acceptance criteria

- No libc `rand`/`srand` left in `src/`.
- The round seed is explicit, logged and settable.
- Same seed gives an identical simulation.
- `tests/README.md` is updated.

## Outcome

*(fill in when done, including the stream chosen for each rand site)*
