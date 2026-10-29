// Brief 04a: the local half of input. InputSampler turns button events into one
// PlayerInput per slot per stepped tick; InputHandler maps keys and pad buttons to
// (slot, button). None of this needs a device: the tests feed the events by hand.
//
// (Core executable only: InputSampler and InputHandler are bitriot_core, not the
// simulation. The simulation half is InputApplyTest.cpp.)

#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "FrameLoop.h"
#include "GameSettings.h"
#include "InputHandler.h"
#include "InputSampler.h"
#include "TickInputs.h"

namespace bitriot_test
{
namespace
{

using Button = PlayerInput::Button;

// ---------------------------------------------------------------------------
// InputSampler
// ---------------------------------------------------------------------------

TEST(InputSamplerTest, NothingHappenedMeansNoBits)
{
  InputSampler s;
  EXPECT_EQ(s.sample(0).bits, 0);
  EXPECT_EQ(s.sampleTick().slot[3].bits, 0);
}

TEST(InputSamplerTest, AHeldButtonIsHeldOnEverySampleUntilItIsReleased)
{
  InputSampler s;
  s.setButton(2, PlayerInput::RIGHT, true);
  s.setButton(2, PlayerInput::ACTION1, true);
  for (int i = 0; i < 3; i++)
  {
    const PlayerInput in = s.sample(2);
    EXPECT_TRUE(in.held(PlayerInput::RIGHT));
    EXPECT_TRUE(in.held(PlayerInput::ACTION1));
    EXPECT_FALSE(in.released(PlayerInput::ACTION1));
    EXPECT_FALSE(in.held(PlayerInput::LEFT));
  }
  EXPECT_EQ(s.sample(1).bits, 0) << "another slot is untouched";
}

TEST(InputSamplerTest, AReleaseIsReportedOnceAndThenForgotten)
{
  InputSampler s;
  s.setButton(0, PlayerInput::ACTION2, true);
  EXPECT_TRUE(s.sample(0).held(PlayerInput::ACTION2));
  s.setButton(0, PlayerInput::ACTION2, false);
  const PlayerInput released = s.sample(0);
  EXPECT_FALSE(released.held(PlayerInput::ACTION2));
  EXPECT_TRUE(released.released(PlayerInput::ACTION2));
  EXPECT_EQ(s.sample(0).bits, 0) << "the edge belongs to one sample, it is never sticky";
}

TEST(InputSamplerTest, APressAndReleaseBetweenTwoSamplesIsATapReportedAsReleasedAlone)
{
  // The unambiguous encoding of a tap: RELEASED set, HELD clear. The press itself is
  // never seen as held (the key is up at the sample).
  InputSampler s;
  s.setButton(1, PlayerInput::ACTION1, true);
  s.setButton(1, PlayerInput::ACTION1, false);
  const PlayerInput in = s.sample(1);
  EXPECT_FALSE(in.held(PlayerInput::ACTION1));
  EXPECT_TRUE(in.released(PlayerInput::ACTION1));
  EXPECT_EQ(in.bits, PlayerInput::ACTION1_RELEASED);
  EXPECT_EQ(s.sample(1).bits, 0);
}

TEST(InputSamplerTest, AReleaseAndARepressBetweenTwoSamplesIsHeldAndReleased)
{
  InputSampler s;
  s.setButton(0, PlayerInput::ACTION2, true);
  s.sample(0);
  s.setButton(0, PlayerInput::ACTION2, false);
  s.setButton(0, PlayerInput::ACTION2, true);
  const PlayerInput in = s.sample(0);
  EXPECT_TRUE(in.held(PlayerInput::ACTION2));
  EXPECT_TRUE(in.released(PlayerInput::ACTION2));
  const PlayerInput next = s.sample(0);
  EXPECT_TRUE(next.held(PlayerInput::ACTION2));
  EXPECT_FALSE(next.released(PlayerInput::ACTION2));
}

TEST(InputSamplerTest, TwoTapsInOneWindowAreOneReleasedBit)
{
  InputSampler s;
  for (int i = 0; i < 2; i++)
  {
    s.setButton(0, PlayerInput::ACTION1, true);
    s.setButton(0, PlayerInput::ACTION1, false);
  }
  EXPECT_EQ(s.sample(0).bits, PlayerInput::ACTION1_RELEASED);
  EXPECT_EQ(s.sample(0).bits, 0);
}

TEST(InputSamplerTest, KeyRepeatAndStrayReleasesChangeNothing)
{
  InputSampler s;
  s.setButton(0, PlayerInput::ACTION1, false); // released while up: no edge
  EXPECT_EQ(s.sample(0).bits, 0);
  s.setButton(0, PlayerInput::ACTION1, true);
  s.setButton(0, PlayerInput::ACTION1, true); // auto-repeat
  s.setButton(0, PlayerInput::ACTION1, true);
  EXPECT_EQ(s.sample(0).bits, PlayerInput::heldBit(PlayerInput::ACTION1));
  s.setButton(0, PlayerInput::ACTION1, false);
  s.setButton(0, PlayerInput::ACTION1, false); // twice
  EXPECT_EQ(s.sample(0).bits, PlayerInput::ACTION1_RELEASED);
}

TEST(InputSamplerTest, DirectionsAreHeldBitsOnlyAndLeaveNoEdge)
{
  InputSampler s;
  s.setButton(0, PlayerInput::UP, true);
  s.setButton(0, PlayerInput::UP, false);
  EXPECT_EQ(s.sample(0).bits, 0) << "a direction tapped inside one window is not seen (as before)";
  s.setButton(0, PlayerInput::LEFT, true);
  s.sample(0);
  s.setButton(0, PlayerInput::LEFT, false);
  EXPECT_EQ(s.sample(0).bits, 0) << "and a direction release is not an edge";
}

TEST(InputSamplerTest, SlotsAreIndependentAndOutOfRangeSlotsAreIgnored)
{
  InputSampler s;
  s.setButton(0, PlayerInput::UP, true);
  s.setButton(5, PlayerInput::DOWN, true);
  s.setButton(-1, PlayerInput::LEFT, true);
  s.setButton(6, PlayerInput::RIGHT, true);
  const TickInputs t = s.sampleTick();
  EXPECT_EQ(t.slot[0].bits, PlayerInput::heldBit(PlayerInput::UP));
  EXPECT_EQ(t.slot[5].bits, PlayerInput::heldBit(PlayerInput::DOWN));
  for (int slot = 1; slot < 5; slot++)
  {
    EXPECT_EQ(t.slot[slot].bits, 0) << slot;
  }
  EXPECT_TRUE(t.control.empty());
  EXPECT_EQ(s.sample(-1).bits, 0);
  EXPECT_EQ(s.sample(6).bits, 0);
}

TEST(InputSamplerTest, OnlyTheLocalSlotsAreSampledAndTheOthersLoseTheirEdges)
{
  InputSampler s;
  for (int slot = 0; slot < 4; slot++)
  {
    s.setButton(slot, PlayerInput::ACTION1, true);
    s.setButton(slot, PlayerInput::ACTION1, false); // an edge pending on every slot
    s.setButton(slot, PlayerInput::UP, true);
  }
  const uint32_t localSlots = (1u << 1) | (1u << 3);
  const TickInputs t = s.sampleTick(localSlots);
  EXPECT_EQ(t.slot[1].bits, PlayerInput::heldBit(PlayerInput::UP) | PlayerInput::ACTION1_RELEASED);
  EXPECT_EQ(t.slot[3].bits, PlayerInput::heldBit(PlayerInput::UP) | PlayerInput::ACTION1_RELEASED);
  EXPECT_EQ(t.slot[0].bits, 0) << "not local: nothing reported";
  EXPECT_EQ(t.slot[2].bits, 0);

  // The skipped slots' release edges were dropped, not kept for later: when slot 0
  // becomes local the stale release must not fire. Its held UP is still down.
  const TickInputs later = s.sampleTick();
  EXPECT_EQ(later.slot[0].bits, PlayerInput::heldBit(PlayerInput::UP));
  EXPECT_EQ(later.slot[2].bits, PlayerInput::heldBit(PlayerInput::UP));
  EXPECT_EQ(later.slot[1].bits, PlayerInput::heldBit(PlayerInput::UP));
}

TEST(InputSamplerTest, ClearingASlotOrEverythingForgetsHeldButtonsAndEdges)
{
  InputSampler s;
  s.setButton(0, PlayerInput::ACTION1, true);
  s.setButton(0, PlayerInput::ACTION1, false);
  s.setButton(0, PlayerInput::UP, true);
  s.setButton(1, PlayerInput::UP, true);
  s.clearSlot(0);
  EXPECT_EQ(s.sample(0).bits, 0);
  EXPECT_EQ(s.sample(1).bits, PlayerInput::heldBit(PlayerInput::UP));
  s.clear();
  EXPECT_EQ(s.sample(1).bits, 0);
  s.clearSlot(99); // harmless
}

TEST(InputSamplerTest, PeekDoesNotConsumeAnything)
{
  InputSampler s;
  s.setButton(0, PlayerInput::ACTION2, true);
  s.setButton(0, PlayerInput::ACTION2, false);
  EXPECT_EQ(s.peek(0).bits, PlayerInput::ACTION2_RELEASED);
  EXPECT_EQ(s.peek(0).bits, PlayerInput::ACTION2_RELEASED);
  EXPECT_EQ(s.sample(0).bits, PlayerInput::ACTION2_RELEASED);
  EXPECT_EQ(s.peek(0).bits, 0);
}

TEST(PlayerInputTest, TheBitsAreWhatTheHeaderSays)
{
  EXPECT_EQ(sizeof(PlayerInput), 1u);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::UP), 1);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::DOWN), 2);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::LEFT), 4);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::RIGHT), 8);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::ACTION1), 16);
  EXPECT_EQ(PlayerInput::heldBit(PlayerInput::ACTION2), 32);
  EXPECT_EQ(PlayerInput::ACTION1_RELEASED, 64);
  EXPECT_EQ(PlayerInput::ACTION2_RELEASED, 128);
  PlayerInput in;
  in.setHeld(PlayerInput::LEFT).setReleased(PlayerInput::ACTION2);
  EXPECT_EQ(in.bits, 4 | 128);
  EXPECT_TRUE(in.held(PlayerInput::LEFT));
  EXPECT_TRUE(in.released(PlayerInput::ACTION2));
  EXPECT_FALSE(in.released(PlayerInput::UP)) << "directions have no released bit";
  in.setHeld(PlayerInput::LEFT, false).setReleased(PlayerInput::ACTION2, false);
  EXPECT_EQ(in.bits, 0);
  EXPECT_EQ((int) std::GameSettings::MAX_PLAYERS, (int) TickInputs().slot.size());
}

// ---------------------------------------------------------------------------
// The cadence: events every frame, a sample once per stepped tick
// ---------------------------------------------------------------------------

// A frame loop with a fake clock whose pump feeds scripted button events to a sampler
// and whose step takes a sample, the way GameEngine's hooks do.
struct SamplingGame
{
  uint32_t now = 0;
  bool canStep = true;
  InputSampler sampler;
  std::vector<std::function<void()>> eventsByFrame;
  size_t frame = 0;
  std::vector<PlayerInput> samples; // one per step() that ran

  FrameLoop::Hooks hooks()
  {
    FrameLoop::Hooks h;
    h.nowMs = [this] { return now; };
    h.simulating = [] { return true; };
    h.canStep = [this] { return canStep; };
    h.pumpEvents = [this]
    {
      if (frame < eventsByFrame.size() && eventsByFrame[frame])
      {
        eventsByFrame[frame]();
      }
      frame++;
    };
    h.step = [this] { samples.push_back(sampler.sample(0)); };
    h.render = [] { return true; };
    return h;
  }
};

TEST(InputSamplingCadenceTest, ATickHeldBackByCanStepLosesNoKeyPressOrRelease)
{
  SamplingGame game;
  FrameLoop loop(game.hooks(), 33);
  // frame 0: the clock starts; frame 1: A1 pressed and released while canStep() holds
  // the tick back; frame 2: still held back; frame 3: allowed again.
  game.eventsByFrame = {
    nullptr,
    [&] { game.sampler.setButton(0, PlayerInput::ACTION1, true); game.sampler.setButton(0, PlayerInput::ACTION1, false); },
    [&] { game.sampler.setButton(0, PlayerInput::UP, true); },
    nullptr,
  };
  loop.frame();
  game.now = 40;
  game.canStep = false;
  EXPECT_EQ(loop.frame().ticksStepped, 0);
  game.now = 80;
  EXPECT_EQ(loop.frame().ticksStepped, 0);
  EXPECT_TRUE(game.samples.empty());

  game.now = 120;
  game.canStep = true;
  const FrameResult result = loop.frame();
  ASSERT_GE(result.ticksStepped, 1);
  ASSERT_EQ(game.samples.size(), (size_t) result.ticksStepped);
  EXPECT_TRUE(game.samples[0].released(PlayerInput::ACTION1)) << "the tap made while the tick was held back is reported";
  EXPECT_FALSE(game.samples[0].held(PlayerInput::ACTION1));
  EXPECT_TRUE(game.samples[0].held(PlayerInput::UP)) << "and the direction pressed meanwhile is held";
}

TEST(InputSamplingCadenceTest, ACatchUpFrameReportsAReleaseOnItsFirstTickOnly)
{
  SamplingGame game;
  FrameLoop loop(game.hooks(), 33);
  game.eventsByFrame = {
    [&] { game.sampler.setButton(0, PlayerInput::ACTION2, true); },
    nullptr,
    [&] { game.sampler.setButton(0, PlayerInput::ACTION2, false); },
  };
  loop.frame();
  game.now = 33;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
  ASSERT_EQ(game.samples.size(), 1u);
  EXPECT_TRUE(game.samples[0].held(PlayerInput::ACTION2));

  // A slow frame: three ticks owed, the pump runs once (the release arrives in it).
  game.now = 33 + 99;
  EXPECT_EQ(loop.frame().ticksStepped, 3);
  ASSERT_EQ(game.samples.size(), 4u);
  EXPECT_TRUE(game.samples[1].released(PlayerInput::ACTION2));
  EXPECT_FALSE(game.samples[1].held(PlayerInput::ACTION2));
  EXPECT_EQ(game.samples[2].bits, 0) << "the release is not repeated on the frame's other ticks";
  EXPECT_EQ(game.samples[3].bits, 0);
}

TEST(InputSamplingCadenceTest, AHeldKeySamplesAsHeldOnEveryTickOfACatchUpFrame)
{
  SamplingGame game;
  FrameLoop loop(game.hooks(), 33);
  game.eventsByFrame = { [&] { game.sampler.setButton(0, PlayerInput::RIGHT, true); } };
  loop.frame();
  game.now = 33 * 4;
  EXPECT_EQ(loop.frame().ticksStepped, 4);
  ASSERT_EQ(game.samples.size(), 4u);
  for (const PlayerInput& in : game.samples)
  {
    EXPECT_TRUE(in.held(PlayerInput::RIGHT));
  }
}

// ---------------------------------------------------------------------------
// InputHandler: keys and pad buttons to (slot, button)
// ---------------------------------------------------------------------------

class InputHandlerRoutingTest : public HeadlessGameTest
{
  protected:
    // Three players: slots 0 and 1 human on keyboard sets (WASD and arrows), slot 2 a bot.
    void startRoundWithTwoKeyboardPlayers(int padOnSlot1 = -1)
    {
      configurePlayers(3);
      std::GameSettings::getInstance()->setPlayerAI(0, false);
      std::GameSettings::getInstance()->setPlayerAI(1, false);
      std::GameSettings::getInstance()->setPlayerController(1, padOnSlot1);
      std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
      startRound();
      std::vector<std::shared_ptr<PlayerCharacterEntity>> players = {player(1), player(2), player(3)};
      m_handler = std::make_unique<InputHandler>();
      m_handler->setPointers(players, nullptr);
    }

    std::unique_ptr<InputHandler> m_handler;
};

TEST_F(InputHandlerRoutingTest, EachKeyboardSetDrivesItsOwnSlot)
{
  startRoundWithTwoKeyboardPlayers();
  m_handler->keyboardEvent(KEY_w, true);        // set 0: WASD, LCTRL, LSHIFT -> slot 0
  m_handler->keyboardEvent(KEY_LCTRL, true);
  m_handler->keyboardEvent(KEY_RIGHT, true);    // set 1: arrows, RCTRL, RSHIFT -> slot 1
  m_handler->keyboardEvent(KEY_RSHIFT, true);
  const TickInputs t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[0].bits, PlayerInput::heldBit(PlayerInput::UP) | PlayerInput::heldBit(PlayerInput::ACTION1));
  EXPECT_EQ(t.slot[1].bits, PlayerInput::heldBit(PlayerInput::RIGHT) | PlayerInput::heldBit(PlayerInput::ACTION2));
  EXPECT_EQ(t.slot[2].bits, 0);
}

TEST_F(InputHandlerRoutingTest, EveryButtonOfASetMapsToTheRightBit)
{
  startRoundWithTwoKeyboardPlayers();
  const KEY set0[] = {KEY_w, KEY_s, KEY_a, KEY_d, KEY_LCTRL, KEY_LSHIFT};
  for (int b = 0; b < PlayerInput::NUM_BUTTONS; b++)
  {
    m_handler->keyboardEvent(set0[b], true);
    EXPECT_EQ(m_handler->sampler().peek(0).bits, 1u << b) << "button " << b;
    m_handler->keyboardEvent(set0[b], false);
    m_handler->sampleTick(); // consume the edge
  }
}

TEST_F(InputHandlerRoutingTest, ATapOnAKeyBetweenTwoSamplesComesThroughAsAReleasedBit)
{
  startRoundWithTwoKeyboardPlayers();
  m_handler->keyboardEvent(KEY_RCTRL, true);
  m_handler->keyboardEvent(KEY_RCTRL, false);
  const TickInputs t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[1].bits, PlayerInput::ACTION1_RELEASED);
  EXPECT_EQ(m_handler->sampleTick().slot[1].bits, 0);
}

TEST_F(InputHandlerRoutingTest, AKeyNoSetOwnsAndAKeyOfAnUnusedSetDriveNobody)
{
  startRoundWithTwoKeyboardPlayers();
  m_handler->keyboardEvent(KEY_q, true);    // not in any set
  m_handler->keyboardEvent(KEY_KP8, true);  // set 2: nobody uses it
  m_handler->keyboardEvent(KEY_i, true);    // set 3
  const TickInputs t = m_handler->sampleTick();
  for (int slot = 0; slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    EXPECT_EQ(t.slot[slot].bits, 0) << slot;
  }
}

TEST_F(InputHandlerRoutingTest, ABotsSlotIsNotSampledEvenIfSomethingPressesItsButtons)
{
  startRoundWithTwoKeyboardPlayers();
  m_handler->setControllerButtonState(2, InputHandler::ACTION1_KEY, true); // slot 2 is a bot
  m_handler->setControllerButtonState(2, InputHandler::ACTION1_KEY, false);
  const TickInputs t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[2].bits, 0) << "the local slot mask is the human slots";
  EXPECT_EQ(m_handler->sampler().peek(2).bits, 0) << "and its edge did not wait around for later";
}

TEST_F(InputHandlerRoutingTest, APadDrivesTheSlotItIsAttachedToAndTheKeyboardStopsDrivingIt)
{
  startRoundWithTwoKeyboardPlayers(/*padOnSlot1=*/4); // slot 1 has a pad
  m_handler->keyboardEvent(KEY_UP, true);
  EXPECT_EQ(m_handler->sampleTick().slot[1].bits, 0) << "a player with a pad does not answer to the keyboard set";
  m_handler->keyboardEvent(KEY_UP, false);

  m_handler->setControllerButtonState(1, InputHandler::LEFT_KEY, true);
  m_handler->setControllerButtonState(1, InputHandler::ACTION2_KEY, true);
  TickInputs t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[1].bits, PlayerInput::heldBit(PlayerInput::LEFT) | PlayerInput::heldBit(PlayerInput::ACTION2));
  m_handler->setControllerButtonState(1, InputHandler::ACTION2_KEY, false);
  t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[1].bits, PlayerInput::heldBit(PlayerInput::LEFT) | PlayerInput::ACTION2_RELEASED);
  EXPECT_EQ(t.slot[0].bits, 0);
}

TEST_F(InputHandlerRoutingTest, ANewRoundStartsWithEveryButtonUp)
{
  startRoundWithTwoKeyboardPlayers();
  m_handler->keyboardEvent(KEY_w, true);
  m_handler->keyboardEvent(KEY_LCTRL, true);
  m_handler->keyboardEvent(KEY_LCTRL, false);
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players = {player(1), player(2), player(3)};
  m_handler->setPointers(players, nullptr);
  for (int slot = 0; slot < std::GameSettings::MAX_PLAYERS; slot++)
  {
    EXPECT_EQ(m_handler->sampleTick().slot[slot].bits, 0) << slot;
  }
}

TEST_F(InputHandlerRoutingTest, KeysBecomeMovementAndAPlacedItemThroughTheSimulation)
{
  // The whole path, no SDL: key events -> sampler -> TickInputs -> Simulation::step().
  startRoundWithTwoKeyboardPlayers();
  const int x0 = player(1)->pixelX();
  m_handler->keyboardEvent(KEY_d, true);
  for (int i = 0; i < 5; i++)
  {
    sim().step(m_handler->sampleTick());
  }
  m_handler->keyboardEvent(KEY_d, false);
  sim().step(m_handler->sampleTick());
  EXPECT_GT(player(1)->pixelX(), x0);

  // A tap on A2, pressed and released between two ticks: an egg, the tap rule.
  m_handler->keyboardEvent(KEY_LSHIFT, true);
  m_handler->keyboardEvent(KEY_LSHIFT, false);
  sim().step(m_handler->sampleTick());
  int eggs = 0;
  for (const auto& entry : em().getDynamicMap()->entityList)
  {
    eggs += entry.second->getType() == EGG1 ? 1 : 0;
  }
  EXPECT_EQ(eggs, 1);
}

} // namespace
} // namespace bitriot_test
