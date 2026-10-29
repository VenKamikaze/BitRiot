// Scripted input for the tests of brief 04a: sequences of TickInputs produced from a
// seed by a tiny generator of the tests' own (not libc rand, not SimRandom: the same
// seed gives the same script on every platform and compiler, and the simulation's
// RNG is never touched).

#ifndef BITRIOT_TESTS_SCRIPTEDINPUT_H_
#define BITRIOT_TESTS_SCRIPTEDINPUT_H_

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "TickInputs.h"

namespace bitriot_test
{

// xorshift64*: small, portable, deterministic.
class ScriptRng
{
  public:
    explicit ScriptRng(uint64_t seed) : m_state(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL)
    {
      if (m_state == 0) { m_state = 0x2545F4914F6CDD1DULL; }
      next();
    }
    uint32_t next()
    {
      m_state ^= m_state >> 12;
      m_state ^= m_state << 25;
      m_state ^= m_state >> 27;
      return (uint32_t) ((m_state * 0x2545F4914F6CDD1DULL) >> 32);
    }
    // 0 .. n-1
    uint32_t below(uint32_t n) { return next() % n; }

  private:
    uint64_t m_state;
};

// Anything a player could do: buttons that flip at random and stay a while, taps
// (RELEASED with nothing held), bounces (HELD and RELEASED), the other action pressed
// during a hold. The released bits obey the sampler's rule that a release edge is
// only reported for a button that was down at the previous tick, plus the odd tap
// from a press-and-release inside one tick. For determinism tests and the pinned
// digest, where any input is as good as any other.
inline std::vector<TickInputs> randomInputs(uint64_t seed, int ticks, std::initializer_list<int> slots)
{
  ScriptRng rng(seed);
  std::vector<TickInputs> script(ticks);
  uint8_t held[std::GameSettings::MAX_PLAYERS] = {0};
  for (int t = 0; t < ticks; t++)
  {
    for (int slot : slots)
    {
      const uint8_t before = held[slot];
      uint8_t now = before;
      for (int b = 0; b < PlayerInput::NUM_BUTTONS; b++)
      {
        const uint32_t oneIn = b >= PlayerInput::ACTION1 ? 7 : 5;
        if (rng.below(oneIn) == 0)
        {
          now = (uint8_t) (now ^ (1u << b));
        }
      }
      PlayerInput in(now);
      for (int a = PlayerInput::ACTION1; a <= PlayerInput::ACTION2; a++)
      {
        const PlayerInput::Button button = (PlayerInput::Button) a;
        const bool wasDown = (before & PlayerInput::heldBit(button)) != 0;
        const bool isDown = (now & PlayerInput::heldBit(button)) != 0;
        if ((wasDown && !isDown) || (!wasDown && rng.below(12) == 0))
        {
          in.setReleased(button); // a release, or a tap inside the tick
        }
      }
      held[slot] = now;
      script[t].slot[slot] = in;
    }
  }
  return script;
}

// Gestures a player makes on one slot, one at a time: a few ticks of walking, then
// one action (A1 or A2) held for 1 to 6 ticks with a direction changing under it,
// then released, and the other action sometimes already pressed on the release tick.
// Never an action pressed AND released while the other action's hold is on, never a
// tap: the only inputs on which the old InputHandler (sticky releases) and the new
// apply rules agree. The held bits of tick t and the release edges (held at t-1,
// not at t) are what a sampler would have produced.
inline std::vector<PlayerInput> gestureInputs(uint64_t seed, int ticks)
{
  ScriptRng rng(seed);
  std::vector<uint8_t> held;
  held.reserve(ticks + 16);
  while ((int) held.size() < ticks + 1)
  {
    const uint32_t walk = rng.below(4);
    for (uint32_t i = 0; i < walk; i++)
    {
      held.push_back(rng.below(3) == 0 ? 0 : (uint8_t) (1u << rng.below(4)));
    }
    const PlayerInput::Button action = rng.below(2) == 0 ? PlayerInput::ACTION1 : PlayerInput::ACTION2;
    const uint32_t hold = 1 + rng.below(6);
    for (uint32_t i = 0; i < hold; i++)
    {
      const uint8_t dir = rng.below(3) == 0 ? 0 : (uint8_t) (1u << rng.below(4));
      held.push_back((uint8_t) (dir | PlayerInput::heldBit(action)));
    }
    // The tick the action is let go of. Sometimes the other action is already down,
    // which starts its hold on the next tick, in both implementations.
    const PlayerInput::Button other = action == PlayerInput::ACTION1 ? PlayerInput::ACTION2 : PlayerInput::ACTION1;
    const uint8_t handoff = rng.below(5) == 0 ? PlayerInput::heldBit(other) : 0;
    held.push_back((uint8_t) ((rng.below(2) == 0 ? 0 : (1u << rng.below(4))) | handoff));
    if (handoff != 0)
    {
      // keep the other action down for a while, then let it go (its own gesture ends)
      const uint32_t more = 1 + rng.below(4); // at least one tick, or its release would be a stray tap
      for (uint32_t i = 0; i < more; i++)
      {
        held.push_back((uint8_t) ((rng.below(2) == 0 ? 0 : (1u << rng.below(4))) | handoff));
      }
      held.push_back(0);
    }
  }
  std::vector<PlayerInput> out(ticks);
  for (int t = 0; t < ticks; t++)
  {
    PlayerInput in(held[t]);
    const uint8_t before = t > 0 ? held[t - 1] : 0;
    for (int a = PlayerInput::ACTION1; a <= PlayerInput::ACTION2; a++)
    {
      const PlayerInput::Button button = (PlayerInput::Button) a;
      if ((before & PlayerInput::heldBit(button)) && !(held[t] & PlayerInput::heldBit(button)))
      {
        in.setReleased(button);
      }
    }
    out[t] = in;
  }
  return out;
}

} // namespace bitriot_test

#endif // BITRIOT_TESTS_SCRIPTEDINPUT_H_
