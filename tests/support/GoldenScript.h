// The script behind the golden match record (tests/data/golden-round.brmr, brief 05):
// two human players that use both actions in every direction, among AI players, on a
// small real map with blocks, plus a takeover, a return and a leave.
//
// It is code, not a pile of bytes, so a reviewer can read what the round does and a
// deliberate regeneration (tests/README.md) can replay the same intent against a
// changed simulation. The record itself is the artefact the test replays.
//
// The generator is the tests' own (ScriptRng, ScriptedInput.h), not SimRandom: the
// same script on every platform.

#ifndef BITRIOT_TESTS_GOLDENSCRIPT_H_
#define BITRIOT_TESTS_GOLDENSCRIPT_H_

#include <cstdint>
#include <vector>

#include "MatchConfig.h"
#include "ScriptedInput.h"
#include "TickInputs.h"

namespace bitriot_test
{

// The round the golden record plays: four players on a 13x11 map (the smallest the
// menu offers) with 55% blocks and the NPC spawner on. Slots 0 and 1 are humans (the
// script drives them); slots 2 and 3 start as bots; each player is on its own team.
inline MatchConfig goldenConfig(uint64_t seed)
{
  MatchConfig c;
  c.seed = seed;
  c.mapWidth = 13;
  c.mapHeight = 11;
  c.blockPercentage = 55;
  c.npcSpawnerEnabled = true;
  c.slots.resize(4);
  for (int i = 0; i < 4; i++)
  {
    c.slots[i].team = i;
    c.slots[i].ai = i >= 2;
  }
  return c;
}

namespace golden
{
const int UP = 1 << PlayerInput::UP;
const int DOWN = 1 << PlayerInput::DOWN;
const int LEFT = 1 << PlayerInput::LEFT;
const int RIGHT = 1 << PlayerInput::RIGHT;
const int A1 = 1 << PlayerInput::ACTION1;
const int A2 = 1 << PlayerInput::ACTION2;

// One player's held buttons for `ticks` ticks. A routine repeats until the time is
// up: walk somewhere, then do one of the ten action gestures (ACTION1 or ACTION2,
// each with no direction, UP, DOWN, LEFT or RIGHT) in a shuffled order that visits
// all ten before it repeats, then walk away from whatever was just laid.
inline std::vector<uint8_t> heldBits(uint64_t seed, int ticks)
{
  ScriptRng rng(seed);
  std::vector<uint8_t> held;
  held.reserve(ticks + 64);

  const int directions[4] = { UP, DOWN, LEFT, RIGHT };
  // gesture g: action = g / 5 (0 = ACTION1, 1 = ACTION2), direction = g % 5 (0 = none)
  int order[10];
  int used = 10; // forces a reshuffle on the first pass
  while ((int) held.size() < ticks)
  {
    if (used == 10)
    {
      for (int i = 0; i < 10; i++)
      {
        order[i] = i;
      }
      for (int i = 9; i > 0; i--)
      {
        const int j = (int) rng.below((uint32_t) (i + 1));
        const int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
      used = 0;
    }
    const int gesture = order[used++];

    // Walk a few tiles (a tile takes about 7 ticks at full health).
    const int walkDir = directions[rng.below(4)];
    const int walk = 4 + (int) rng.below(20);
    for (int i = 0; i < walk; i++)
    {
      held.push_back((uint8_t) walkDir);
    }

    // The gesture: the action goes down alone for a tick (so the hold begins), then a
    // direction joins it for 1 to 3 ticks, then the action is let go (the tick the
    // sampler would report RELEASED on).
    const int action = gesture < 5 ? A1 : A2;
    const int dirIndex = gesture % 5;
    held.push_back((uint8_t) action);
    if (dirIndex > 0)
    {
      const int dwell = 1 + (int) rng.below(3);
      for (int i = 0; i < dwell; i++)
      {
        held.push_back((uint8_t) (action | directions[dirIndex - 1]));
      }
    }
    held.push_back(0); // let go: the release edge is on this tick

    // Get clear of it.
    const int awayDir = directions[rng.below(4)];
    const int away = 6 + (int) rng.below(14);
    for (int i = 0; i < away; i++)
    {
      held.push_back((uint8_t) awayDir);
    }
    // And sometimes stand still a moment.
    for (int i = (int) rng.below(6); i > 0; i--)
    {
      held.push_back(0);
    }
  }
  held.resize(ticks);
  return held;
}

// The inputs a sampler would have produced from held bits: the held bits as they
// are, and a RELEASED edge on the first tick an action is up after being down.
inline std::vector<PlayerInput> sampledFrom(const std::vector<uint8_t>& held)
{
  std::vector<PlayerInput> out(held.size());
  for (size_t t = 0; t < held.size(); t++)
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
} // namespace golden

// The golden round's TickInputs for ticks 1..ticks:
//   - slots 0 and 1: the gesture routine above (different seeds), all the way;
//   - slot 2 (a bot) is taken over by a human at tick 400, driven by the same kind of
//     routine until tick 700, and given back to the AI then;
//   - slot 0 leaves (LEAVE) at tick 1500 and is a bot from there on.
inline std::vector<TickInputs> goldenScript(uint64_t scriptSeed, int ticks)
{
  const std::vector<PlayerInput> p0 = golden::sampledFrom(golden::heldBits(scriptSeed * 3 + 1, ticks));
  const std::vector<PlayerInput> p1 = golden::sampledFrom(golden::heldBits(scriptSeed * 3 + 2, ticks));
  const std::vector<PlayerInput> p2 = golden::sampledFrom(golden::heldBits(scriptSeed * 3 + 3, ticks));
  std::vector<TickInputs> script(ticks);
  for (int t = 0; t < ticks; t++)
  {
    const int tick = t + 1;
    script[t].slot[0] = p0[t];
    script[t].slot[1] = p1[t];
    if (tick >= 400 && tick < 700)
    {
      script[t].slot[2] = p2[t];
    }
    if (tick == 400)
    {
      script[t].control.push_back(ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER));
    }
    if (tick == 700)
    {
      script[t].control.push_back(ControlEvent(2, ControlEvent::RETURN_TO_AI));
    }
    if (tick == 1500)
    {
      script[t].control.push_back(ControlEvent(0, ControlEvent::LEAVE));
    }
  }
  return script;
}

} // namespace bitriot_test

#endif // BITRIOT_TESTS_GOLDENSCRIPT_H_
