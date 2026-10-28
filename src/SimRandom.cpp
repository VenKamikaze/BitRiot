#include "SimRandom.h"

#include <chrono>
#include <random>

namespace
{
const uint64_t PCG_MULT = 6364136223846793005ULL;
const uint64_t PCG_SEQUENCE = 54ULL;
}

SimRandom::SimRandom()
  : m_state(0), m_inc(0)
{
  seed(0);
}

SimRandom::SimRandom(uint64_t seedValue)
  : m_state(0), m_inc(0)
{
  seed(seedValue);
}

void SimRandom::step()
{
  m_state = m_state * PCG_MULT + m_inc;
}

void SimRandom::seed(uint64_t seedValue)
{
  m_state = 0;
  m_inc = (PCG_SEQUENCE << 1) | 1ULL;
  step();
  m_state += seedValue;
  step();
}

uint32_t SimRandom::nextU32()
{
  const uint64_t old = m_state;
  step();
  const uint32_t xorshifted = (uint32_t) (((old >> 18) ^ old) >> 27);
  const uint32_t rot = (uint32_t) (old >> 59);
  return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
}

int SimRandom::nextInt(int n)
{
  const uint32_t v = nextU32();
  if (n <= 1)
  {
    return 0;
  }
  return (int) (v % (uint32_t) n);
}

uint64_t SimRandom::freshSeed()
{
  uint64_t value = (uint64_t) std::chrono::high_resolution_clock::now().time_since_epoch().count();
  try
  {
    std::random_device rd;
    value ^= ((uint64_t) rd() << 32) | (uint64_t) rd();
  }
  catch (...)
  {
    // No entropy source: the clock alone is enough for a local round seed.
  }
  return value;
}

SimRandom& SimRandom::sim()
{
  static SimRandom stream(0);
  return stream;
}

SimRandom& SimRandom::cosmetic()
{
  static SimRandom stream(freshSeed());
  return stream;
}
