// Fixed-point numbers for the simulation.
//
// The simulation uses no floating point (netplay brief 02b): every position
// offset and velocity is an integer in Q16.16, so a tick gives bit-identical
// results on any compiler, optimisation level and CPU. Floating point is for
// presentation only (rendering, rumble, UI, audio), and
// tests/FixedPointSimTest.cpp fails if a float or double appears in a sim file.
//
// A `fixed_t` is an int32_t holding value * 65536. An entity's offset from the
// middle of its tile runs from -1 to +1 (so one tile is a change of 2.0), and
// velocities are in the same units per tick: a velocity of 0.275 is 18022.
//
// Plain int32_t with named helpers, rather than a wrapper class, so the movement
// code stays what it always was: adds, subtracts and compares. Addition,
// subtraction and comparison of two `fixed_t` values are exact, and no value the
// simulation holds is anywhere near the +-32768 limit.
//
// Rounding rules (each helper is defined for negative values):
//
//   fixedFromInt(n)        exact: n * 65536.
//   fixedFromRatio(n, d)   n / d rounded to the NEAREST representable value;
//                          exact ties (the true quotient lies exactly half way
//                          between two fixed-point values) round AWAY from zero, so
//                          fixedFromRatio(-n, d) == -fixedFromRatio(n, d).
//                          A zero or negative d gives 0 (rather than undefined
//                          behaviour), and a result outside the int32 range
//                          saturates.
//   fixedAbs(v)            |v|; the one value without a positive counterpart
//                          (INT32_MIN) saturates to INT32_MAX.
//   fixedToPixels(v, ppu)  v * ppu, TRUNCATED toward zero to a whole number.
//                          This is what the renderer's old (int)(offset * n)
//                          cast did, so sprites do not move. Truncation is not
//                          flooring: -0.5 pixel is 0, not -1. (Note that `>>`
//                          on a negative value floors, so it is not used for
//                          this.)

#ifndef _FIXED_H
#define _FIXED_H

#include <cstdint>

typedef int32_t fixed_t;

const int FIXED_SHIFT = 16;
const fixed_t FIXED_ONE = 1 << FIXED_SHIFT;

// n * 1.0, for any int n whose result fits. Multiplies rather than shifting, as
// a left shift of a negative value is not defined.
inline fixed_t fixedFromInt(int n)
{
  return (fixed_t)(n * FIXED_ONE);
}

// numerator / denominator as a fixed_t value; see the rounding rules above.
inline fixed_t fixedFromRatio(int64_t numerator, int64_t denominator)
{
  if (denominator <= 0)
  {
    return 0;
  }
  const bool negative = numerator < 0;
  const uint64_t absNumerator = negative ? (uint64_t)0 - (uint64_t)numerator : (uint64_t)numerator;
  uint64_t quotient;
  if (absNumerator > (UINT64_MAX >> FIXED_SHIFT))
  {
    quotient = (uint64_t)INT32_MAX; // too big to scale: saturate
  }
  else
  {
    const uint64_t magnitude = absNumerator << FIXED_SHIFT;
    const uint64_t den = (uint64_t)denominator;
    quotient = magnitude / den;
    if (2 * (magnitude % den) >= den)
    {
      ++quotient; // nearest, ties away from zero
    }
  }
  if (quotient > (uint64_t)INT32_MAX)
  {
    quotient = (uint64_t)INT32_MAX;
  }
  return negative ? -(fixed_t)quotient : (fixed_t)quotient;
}

inline fixed_t fixedAbs(fixed_t v)
{
  if (v >= 0)
  {
    return v;
  }
  return (v == INT32_MIN) ? INT32_MAX : -v;
}

// v * pixelsPerUnit as a whole number of pixels, truncated toward zero.
inline int fixedToPixels(fixed_t v, int pixelsPerUnit)
{
  return (int)(((int64_t)v * pixelsPerUnit) / FIXED_ONE);
}

#endif
