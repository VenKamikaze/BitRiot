/*
 * NetRandom.h
 *
 * The session's own little generator, for session ids, nonces and tokens. They only have
 * to be hard to guess and different on every machine; they never touch the simulation.
 * (The simulation's rule, enforced by SimRandomSourceGuardTest, is that nothing in src/
 * uses <random> or libc rand(): so this is splitmix64, seeded from the operating system's
 * entropy where there is one, else from the clocks and addresses of the process.)
 */
#ifndef _NETRANDOM_H
#define _NETRANDOM_H

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace netplay
{
  class NetRandom
  {
    public:
      NetRandom() { seed(0); }
      // 0 = from the system; anything else is used as given (the tests).
      void seed(uint64_t s)
      {
        if (s != 0)
        {
          m_state = s;
          return;
        }
        uint64_t mix = (uint64_t) std::chrono::steady_clock::now().time_since_epoch().count()
                     ^ ((uint64_t) std::chrono::system_clock::now().time_since_epoch().count() << 1)
                     ^ (uint64_t) (uintptr_t) &mix;
        FILE* f = std::fopen("/dev/urandom", "rb");
        if (f != nullptr)
        {
          uint64_t entropy = 0;
          if (std::fread(&entropy, sizeof(entropy), 1, f) == 1)
          {
            mix ^= entropy;
          }
          std::fclose(f);
        }
        m_state = mix;
      }
      uint64_t next()
      {
        m_state += 0x9E3779B97F4A7C15ull;
        uint64_t z = m_state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
      }
      uint64_t operator()() { return next(); }
    private:
      uint64_t m_state = 1;
  };
}

#endif
