# 02 — Seeded simulation RNG

- **Repo:** BitRiot
- **Branch:** `netplay/02-sim-rng`
- **Depends on:**
  - brief 01
  - `fix/ai-seek-energy`, which changed the bot AI's `rand()` calls (merged into `claude` in 7d6cf6a)
- **Status:** done

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

**What landed**

1. **`SimRandom`** (`src/SimRandom.h/.cpp`): PCG32 (PCG-XSH-RR, 64-bit state, fixed stream constant 54), `seed(uint64_t)`, `nextU32()`, `nextInt(n)`. The exact algorithm and the "one sim draw per full-expression" rule are in the header comment. `nextInt(n)` is plain modulo, returns 0 for `n <= 1` (including 0 and negatives), and **always consumes exactly one draw** (so the stream position never depends on `n`; the old `rand() % 0` in SpawningPool/RobotEntity is now defined). Seeded with 42 it reproduces the PCG reference implementation's output (`0xa15c02b7, 0x7b47f409, ...`), which the known-answer test checks.
2. **Two process-lifetime streams**, `SimRandom::sim()` and `SimRandom::cosmetic()` (static accessors; 03a moves the sim stream into `Simulation`). The cosmetic stream is seeded from `std::random_device` and the clock on first use, and is never hashed, recorded or sent. `SimRandom::freshSeed()` makes a local-round seed the same way.
3. **Every `rand()`/`srand()` in `src/` is gone** (table below). `srand(SDL_GetTicks())` is removed from `MickSDLRenderer.cpp`. A test fails if libc `rand`/`srand`/`random` or a std random engine reappears in `src/`.
4. **Seed per round.** `GameSettings` holds `roundSeed` (`getRoundSeed()`, `setRoundSeed()` which pins the next round, `isRoundSeedPinned()`, `clearRoundSeedPin()`) and `beginRoundSeed()`, which picks the pinned seed or a fresh one, releases the pin, reseeds `SimRandom::sim()` and returns the seed. `GameEngine::resetGame()` calls it as its very first statement (before the entity manager, pool and players are built) and logs `Round seed=<n>` at info level. A pin lasts one round, so "Play again" gets a fresh seed again; netplay (07) sets the pin from the host's config before the round starts.
5. **Harness.** `startRound()` calls the same production `beginRoundSeed()` before building the `EntityManager` and `SpawningPool`. `BITRIOT_TEST_SEED` is now the *base* seed (`HeadlessEnvironment::setBaseSeed`); each round's seed is derived from base seed, the test's full name and the test's round count, so a test plays the same rounds under any order or filter, and consecutive rounds in one test differ. `setRoundSeed(n)` on the fixture pins the next `startRound()`. `SetUp()` also seeds the sim stream. `test_main.cpp` no longer calls `srand`.

**`rand()` site to stream** (re-found by grep; line numbers are as of this branch)

| Site | Draws | Stream |
|---|---|---|
| `SpawningPool.cpp:20`, `:40` | spawn delay (ctor, `update`) | sim |
| `SpawningPool.cpp:130`, `:161` | type rolls in `getRandomType` | sim |
| `SpawningPool.cpp:198-199` | spawn tile x, y | sim |
| `SpawningPool.cpp:234`, `:241` | robot flags | sim |
| `GameEngine.cpp:276` | block seeding per tile | sim |
| `PlayerCharacterEntity.cpp:56`, `:103` | `m_checkTimer` 37/67 (ctor, `update`; also runs for humans) | sim |
| `PlayerCharacterEntity.cpp:63`, `:69` | bot initial wait, initial EGG2 goal | sim |
| `PlayerCharacterEntity.cpp:290-316` (7 draws) | `determineGoals` | sim |
| `PlayerCharacterEntity.cpp:651` | energy goal egg count | sim |
| `PlayerCharacterEntity.cpp:752` | per-create wait | sim |
| `RobotEntity.cpp:35`, `:58` | action counter (ctor, `update`) | sim |
| `SimpleBrain.cpp:17` | initial heading | sim |
| `SimpleBrain.cpp:71` | heading roll in the `while` loop (draw count depends on data; kept as is) | sim |
| `EffectEntity.cpp:24` | damage-effect anim frame | **cosmetic** |
| `ScoreBoardBinder.cpp:115` | `randomWinText` | **cosmetic** |

That is 27 `rand()` sites (25 sim, 2 cosmetic) plus the one `srand`. code-facts called it "27 sim sites and 1 UI site"; it is 27 in all. The `EffectEntity` frame is render-only: the effect's lifetime runs off `m_health` and `m_animCounter`, and `m_currentAnimFrame` is only read by `render()` and wrapped by `incAnimFrame()`. No expression holds two draws, so the "one draw per full-expression" rule held without any rewriting. Call counts per site are unchanged.

**Tests** (`tests/SimRandomTest.cpp`, 17 new; suite 138 to 155): PCG reference output (seed 42), three more known-answer sequences (from an independent implementation of the documented algorithm), `nextInt` as modulo, `nextInt(0/1/negative)` returns 0 and consumes one draw, range and coverage, reseed restarts the sequence, the two streams are independent, `beginRoundSeed` uses a pin and seeds the stream, a pin is single-use, `startRound` honours a pinned seed, same seed plays the same full 2-player round twice (winner, frames, per-tick entity-count digest, final stream position), same seed plays the same 4-player 1500-tick round on the default map twice, 8 different seeds give 8 different rounds, cosmetic draws (a burst plus one per tick) do not change a round, a damage effect and the scoreboard win text each draw exactly one cosmetic value and no sim value, and the libc-random source guard. `AISeekEnergyTest.SameSeedGivesTheSamePickupTiming` used `std::srand(1234)`; it now uses `setRoundSeed(1234)` (same intent, and `srand` would no longer do anything).

**What was shown to fail without the change.** The new API does not exist before the change, so the tests cannot be compiled against the old code. Instead each piece of the change was reverted in turn on this branch, the tests rebuilt and run (filter `SimRandom*:RoundSeed*:SimRound*:AISeekEnergyTest.SameSeed*`, 18 tests):

- **No per-round reseed** (`beginRoundSeed()` stops seeding the sim stream): 6 fail: `RoundSeedTest.BeginRoundSeedUsesAPinnedSeedAndSeedsTheSimStream`, `RoundSeedTest.APinLastsForOneRoundOnly`, `SimRoundTest.SameSeedPlaysTheSameFullRoundTwice`, `SimRoundTest.SameSeedPlaysTheSameFourPlayerRoundOnTheDefaultMap`, `SimRoundTest.CosmeticDrawsDoNotChangeTheSimulation`, `AISeekEnergyTest.SameSeedGivesTheSamePickupTiming`. This is the "same seed, same round" requirement; the old code had no seeding at all, which is the same condition.
- **Cosmetic stream aliased to the sim stream:** 4 fail: `TheTwoStreamsAreIndependent`, `CosmeticDrawsDoNotChangeTheSimulation`, `ADamageEffectDrawsOneCosmeticValueAndNoSimValue`, `TheScoreboardWinTextDrawsOneCosmeticValueAndNoSimValue`.
- **One SpawningPool site put back on libc `rand()`:** 3 fail: `SimRandomSourceGuardTest.NoLibcRandomNumbersInSrc`, `SimRoundTest.CosmeticDrawsDoNotChangeTheSimulation`, `SimRoundTest.SameSeedPlaysTheSameFourPlayerRoundOnTheDefaultMap`. (`SameSeedPlaysTheSameFullRoundTwice` still passes: the 2-player 7x7 round does not reach that site.)
- **`nextInt(n <= 1)` returns without drawing:** `NextIntOfZeroOneOrNegativeIsZeroAndStillConsumesOneDraw` fails.
- **`EffectEntity` draws from the sim stream:** `ADamageEffectDrawsOneCosmeticValueAndNoSimValue` fails.
- **Cannot fail by construction:** the PRNG known-answer tests (they pin the algorithm and have no earlier version to revert to), and `DifferentSeedsPlayDifferentRounds` (it guards against a stuck or ignored seed, and passes on a libc stream too). `StartRoundSeedsFromThePinnedValue` and `SeedingRestartsTheSequenceAndSeedsDiffer` are plumbing checks of the same kind.

**Sweep** (200 seeds, `BITRIOT_TEST_SEED=1..200`, run from the build directory). Because the base seed now feeds a per-test hash, "seed N" means a different set of rounds than before.

- Before (`claude`, 138 tests): 1 failing seed, **62**, `FullRoundTest.ThreeConsecutiveCoopRoundsAllComplete` (the known co-op stalemate in TODO).
- After (155 tests): 1 failing seed, **6**, `AISeekEnergyTest.HealthyBotWithAnEggUnderItselfStillGetsOutToAPickupInTime`. The co-op stalemate did not show up in these 200 seeds (it is the same latent bug; it just needs a different sequence).
- Seed 6 is a **real latent bot bug that the new sequence exposed**, not caused by the change and not the co-op stalemate. A bot boxed in at (1,1) by two blocks never gets out in 4000 frames: it keeps laying and collecting its own EGG1/EGG2 eggs (its health climbs from 60 to 84 on self-made energy), and no bomb is ever laid (a frame trace shows 0 bombs throughout). The likely cause, inferred from the trace and not root-caused further, is the chain-blast rule that makes the bomb-out route refuse a site with an egg on or beside it. Over 1500 seeds of that one test only seed 6 fails (about 0.07%), so it is rare and needs a particular run of rolls. The test was not changed or pinned, since pinning would hide the bug. It is listed as a follow-up.
- `--gtest_shuffle` ran clean three times at the default seed.

**Deviations and decisions**

- `nextInt` always draws (the brief only said it returns 0 for 0 and 1); chosen so the stream position never depends on data like a map dimension.
- The pin is single-use rather than sticky; see item 4.
- The harness seeds per round from a hash of (base seed, test name, round count) instead of one process-wide stream. This makes tests order- and filter-independent, but it means a failing seed in the sweep reproduces with `--gtest_filter` on its own.
- The source guard locates `src/` through a compile definition (`BITRIOT_SOURCE_DIR`), because the sweep runs from the build directory.
- There is no command-line or environment way to set the seed in the real game (that would touch `main.cpp`). `GameSettings::setRoundSeed()` is the hook; 07 or 05 will call it.
- code-facts §2, §10 and §12 were updated (site count, streams, the `% 0` note).

**Follow-ups**

- **Bot livelock** (above): a boxed-in bot that keeps making eggs on its own tile never bombs out. Someone should reproduce it with `BITRIOT_TEST_SEED=6 ./build/tests/bitriot_tests --gtest_filter='AISeekEnergyTest.HealthyBotWithAnEggUnder*'` and decide the right behaviour. Not fixed here.
- **03a:** the state hash must exclude a `DAMAGE_EFFECT` entity's `m_currentAnimFrame` (cosmetic stream). `Simulation` should own the sim stream and the round seed (`MatchConfig`), and `GameSettings::beginRoundSeed()` should move with it. Keep the reseed ahead of any constructor that draws.
- **05b (cross-compiler):** the rule "at most one sim draw per full-expression" is documented in `SimRandom.h` but only checked by reading the code; nothing enforces it. A GCC-vs-Clang run is the real test.
- `GameEngine::resetGame()`'s call to `beginRoundSeed()` is not covered by a test (the engine cannot run headlessly); the production function it calls is.
