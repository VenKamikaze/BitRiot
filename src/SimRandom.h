/*
 * SimRandom.h
 *
 * The game's own pseudo-random number generators. Nothing in src/ calls libc
 * rand()/srand(): libc's sequence differs between platforms (glibc, musl,
 * Emscripten), so it cannot be shared between peers or replayed.
 *
 * TWO STREAMS
 *
 *   SimRandom::sim()       Everything the simulation decides: spawn timing and
 *                          types, block seeding, bot AI choices, robot/brain
 *                          choices. Reseeded from the round seed at the very
 *                          start of every round (GameEngine::resetGame), so the
 *                          same seed plays the same round on every machine. It
 *                          may be hashed, recorded and sent.
 *
 *   SimRandom::cosmetic()  Presentation only: the damage-effect animation
 *                          frame and the scoreboard win text. Seeded from the
 *                          OS and may differ per machine and per run. It is
 *                          NEVER hashed, recorded or sent, and its values must
 *                          never feed back into simulation logic. Drawing from
 *                          it never moves the sim stream.
 *
 *   When in doubt a draw belongs on the sim stream.
 *
 * ALGORITHM (so tools in other languages can reproduce it exactly)
 *
 *   PCG32 (PCG-XSH-RR, 64-bit state, 32-bit output), after M.E. O'Neill's
 *   reference pcg32_random_r / pcg32_srandom_r. All arithmetic is uint64_t /
 *   uint32_t modulo 2^64 / 2^32. MULT = 6364136223846793005.
 *
 *     seed(s):              state = 0
 *                           inc   = (SEQUENCE << 1) | 1       (SEQUENCE = 54)
 *                           step(); state += s; step()
 *     step():               state = state * MULT + inc
 *     nextU32():            old = state; step()
 *                           xorshifted = (uint32_t)(((old >> 18) ^ old) >> 27)
 *                           rot        = (uint32_t)(old >> 59)
 *                           return (xorshifted >> rot) |
 *                                  (xorshifted << ((32 - rot) & 31))
 *     nextInt(n):           v = nextU32()          (always exactly one draw)
 *                           return n <= 1 ? 0 : (int)(v % (uint32_t)n)
 *
 *   So seed(42) yields 0xa15c02b7, 0x7b47f409, 0xba1d3330, ... (the reference
 *   implementation's output for initstate 42, initseq 54).
 *
 *   nextInt(n) returns a value in [0, n). Plain modulo is used on purpose: the
 *   bias is negligible for the small n used here, and being identical on every
 *   platform matters more. n <= 1 (including 0 and negative) returns 0, where
 *   the old `rand() % 0` was undefined behaviour. nextInt always consumes one
 *   draw, even for n <= 1, so the stream position never depends on n.
 *
 * RULE: AT MOST ONE SIM DRAW PER FULL-EXPRESSION
 *
 *   C++ leaves the evaluation order of function arguments, operands and
 *   initialiser lists unspecified, and GCC and Clang (which Emscripten uses)
 *   differ. `f(sim().nextInt(3), sim().nextInt(4))` would draw in a different
 *   order on each compiler and desynchronise peers. This holds directly and
 *   through helper calls: a full-expression must not contain two calls that can
 *   each draw from the sim stream (a constructor that draws counts as a call).
 *   Draw into a named local first, one statement each, in the order wanted.
 */

#ifndef SIMRANDOM_H_
#define SIMRANDOM_H_

#include <cstdint>

class SimRandom
{
  public:
    // Unseeded generators are seeded with 0, so a default-constructed one is
    // still reproducible.
    SimRandom();
    explicit SimRandom(uint64_t seedValue);

    void seed(uint64_t seedValue);

    uint32_t nextU32();

    // Uniform-ish value in [0, n). Returns 0 for n <= 1. Always one draw.
    int nextInt(int n);

    // The simulation stream. A process-lifetime instance until brief 03a moves
    // it into the Simulation object.
    static SimRandom& sim();

    // The cosmetic stream. Seeded from std::random_device (or the clock) on
    // first use. Tests may seed() it explicitly.
    static SimRandom& cosmetic();

    // A fresh 64-bit value for seeding a new local round, from
    // std::random_device or, failing that, the clock. Does not touch either
    // stream.
    static uint64_t freshSeed();

  private:
    void step();

    uint64_t m_state;
    uint64_t m_inc;
};

#endif /* SIMRANDOM_H_ */
