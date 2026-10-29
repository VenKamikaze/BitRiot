// Brief 04b: the local half of control changes and of rumble. The join selector, a
// keyboard takeover and a controller unplug no longer change who controls a slot: each
// queues a ControlEvent that the next sampled tick carries, and the simulation applies
// it at the start of that tick. A hit is recorded by the simulation
// (Simulation::hitThisTick) and the input layer buzzes the pad after the step.
//
// None of this needs a device: the tests drive InputHandler's public entry points by
// hand and install a fake rumble device. (Core executable only. The simulation half
// is ControlEventTest.cpp.)
//
// SLOT is playerNumber - 1 throughout.

#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include <SDL2/SDL.h>

#include <memory>
#include <vector>

#include "GameSettings.h"
#include "InputHandler.h"
#include "MickBaseInput.h"
#include "Simulation.h"
#include "TickInputs.h"

namespace bitriot_test
{
namespace
{

class FakeRumble : public ControllerRumble
{
  public:
    struct Call { int controller; float strength; unsigned ms; };
    void rumble(int controllerId, float strength, unsigned durationMs) override
    {
      calls.push_back({controllerId, strength, durationMs});
    }
    std::vector<Call> calls;
};

// Records what the join selector asks the HUD to show (InfoPanel needs a surface).
class RecordingView : public JoinIndicatorView
{
  public:
    struct Call { int team; JoinState state; JoinDevice device; };
    void setJoinIndicator(int team, JoinState state, JoinDevice device) override
    {
      calls.push_back({team, state, device});
    }
    std::vector<Call> calls;
};

class InputControlTest : public HeadlessGameTest
{
  protected:
    static constexpr int PAD = 7;       // the pad that drives slot 1 from the start
    static constexpr int NEW_PAD = 9;   // a pad that joins mid-round
    static constexpr int OTHER_PAD = 10;

    // Four players: slot 0 human on the WASD set, slot 1 human on pad 7, slots 2 and 3
    // bots. No spawner, no blocks.
    void startRound4()
    {
      configurePlayers(4);
      std::GameSettings* settings = std::GameSettings::getInstance();
      settings->setPlayerAI(0, false);
      settings->setPlayerAI(1, false);
      settings->setPlayerController(1, PAD);
      settings->setNpcSpawnerEnabled(false);
      startRound();
      m_handler = std::make_unique<InputHandler>();
      m_handler->setRumble(&m_rumble);
      m_handler->setPointers(allPlayers(), &m_view);
    }

    std::vector<std::shared_ptr<PlayerCharacterEntity>> allPlayers()
    {
      return {player(1), player(2), player(3), player(4)};
    }

    // START opens a selector for a pad that drives nobody, START again confirms it.
    void joinWithPad(int pad)
    {
      m_handler->processJoinControllerEvent(pad, SDL_CONTROLLER_BUTTON_START);
      m_handler->processJoinControllerEvent(pad, SDL_CONTROLLER_BUTTON_START);
    }

    // The first (key set 1) keyboard action key opens a selector, a second one confirms.
    void joinWithKeyboardSet1()
    {
      m_handler->processJoinKeyboardEvent(KEY_RCTRL);
      m_handler->processJoinKeyboardEvent(KEY_RCTRL);
    }

    void hit(int playerNumber)
    {
      spawnEntity(EXPLOSION, 0, player(playerNumber)->getTileX(), player(playerNumber)->getTileY());
    }

    std::unique_ptr<InputHandler> m_handler;
    FakeRumble m_rumble;
    RecordingView m_view;
};

// ---------------------------------------------------------------------------
// Joining
// ---------------------------------------------------------------------------

TEST_F(InputControlTest, AControllerJoinQueuesATakeoverAndTheSimulationAppliesItOnTheNextStep)
{
  startRound4();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START); // opens the selector
  EXPECT_TRUE(m_handler->sampleTick().control.empty()) << "opening a selector changes nothing";

  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START); // confirms: slot 2 (the first bot)
  EXPECT_TRUE(player(3)->isControlledByAI()) << "the input layer does not touch the player";

  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0], ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER));
  EXPECT_TRUE(player(3)->isControlledByAI()) << "still the bot until the simulation applies the event";

  sim().step(t);
  EXPECT_FALSE(player(3)->isControlledByAI());
  EXPECT_TRUE(m_handler->sampleTick().control.empty()) << "an event is carried once";
}

TEST_F(InputControlTest, AControllerJoinMapsThePadAndTheSlotIsLocalFromThatMoment)
{
  startRound4();
  EXPECT_EQ(m_handler->controllerForSlot(2), -1);
  EXPECT_FALSE(m_handler->isLocalSlot(2));
  joinWithPad(NEW_PAD);
  EXPECT_EQ(m_handler->controllerForSlot(2), NEW_PAD);
  EXPECT_TRUE(m_handler->isLocalSlot(2));
  EXPECT_EQ(m_handler->getPlayerAttachedToController(NEW_PAD), player(3));
}

TEST_F(InputControlTest, TheTakeoverTickCarriesTheNewDevicesInputToo)
{
  startRound4();
  joinWithPad(NEW_PAD);
  m_handler->setControllerButtonState(2, InputHandler::DOWN_KEY, true); // pressed in the same frame

  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_TRUE(t.slot[2].held(PlayerInput::DOWN)) << "the slot is local as soon as the event is produced";

  const int y0 = player(3)->pixelY();
  sim().step(t);
  EXPECT_GT(player(3)->pixelY(), y0) << "event first, then the input: the new human moves on the tick of the takeover";
}

TEST_F(InputControlTest, AKeyboardJoinQueuesATakeoverAndTheSetDrivesTheSlotFromThen)
{
  startRound4();
  m_handler->processJoinKeyboardEvent(KEY_RCTRL); // opens
  m_handler->processJoinKeyboardEvent(KEY_RCTRL); // confirms
  EXPECT_TRUE(player(3)->isControlledByAI());
  EXPECT_TRUE(m_handler->isLocalSlot(2));
  EXPECT_EQ(m_handler->controllerForSlot(2), -1);
  EXPECT_EQ(m_handler->m_keyMap.count(2), 1u);

  m_handler->keyboardEvent(KEY_DOWN, true); // arrows are set 1
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0], ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER));
  EXPECT_TRUE(t.slot[2].held(PlayerInput::DOWN));

  sim().step(t);
  EXPECT_FALSE(player(3)->isControlledByAI());
}

TEST_F(InputControlTest, KeysDoNotDriveASlotThatHasAPadEvenIfItHasKeys)
{
  startRound4();
  m_handler->m_keyMap[1] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_RCTRL, KEY_RSHIFT}; // the defensive case
  m_handler->keyboardEvent(KEY_UP, true);
  EXPECT_EQ(m_handler->sampleTick().slot[1].bits, 0) << "the pad drives it";

  // once the pad is gone the keys do (the player has keys, so it stays human)
  m_handler->detachController(PAD);
  m_handler->keyboardEvent(KEY_UP, true);
  EXPECT_TRUE(m_handler->sampleTick().slot[1].held(PlayerInput::UP));
}

// ---------------------------------------------------------------------------
// The join indicators (the HUD side of the selector), with a panel set
// ---------------------------------------------------------------------------

TEST_F(InputControlTest, ARoundStartShowsTheKeyboardBadgeOfTheKeyboardHuman)
{
  startRound4();
  ASSERT_EQ(m_view.calls.size(), 1u) << "slot 0 is the only human on keys";
  EXPECT_EQ(m_view.calls[0].team, 1);
  EXPECT_EQ(m_view.calls[0].state, JoinIndicatorView::JOIN_READY);
  EXPECT_EQ(m_view.calls[0].device, JoinIndicatorView::JOIN_DEVICE_KEYBOARD);
}

TEST_F(InputControlTest, TheSelectorShowsOpenMoveConfirmAndCancelOnThePanel)
{
  startRound4();
  m_view.calls.clear();

  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START); // opens on team 3
  ASSERT_EQ(m_view.calls.size(), 1u);
  EXPECT_EQ(m_view.calls[0].team, 3);
  EXPECT_EQ(m_view.calls[0].state, JoinIndicatorView::JOIN_NOTREADY);
  EXPECT_EQ(m_view.calls[0].device, JoinIndicatorView::JOIN_DEVICE_CONTROLLER);

  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_DPAD_DOWN); // moves to team 4
  ASSERT_EQ(m_view.calls.size(), 3u);
  EXPECT_EQ(m_view.calls[1].team, 3);
  EXPECT_EQ(m_view.calls[1].state, JoinIndicatorView::JOIN_NONE);
  EXPECT_EQ(m_view.calls[2].team, 4);
  EXPECT_EQ(m_view.calls[2].state, JoinIndicatorView::JOIN_NOTREADY);

  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START); // confirms
  ASSERT_EQ(m_view.calls.size(), 4u);
  EXPECT_EQ(m_view.calls[3].team, 4);
  EXPECT_EQ(m_view.calls[3].state, JoinIndicatorView::JOIN_READY);

  m_view.calls.clear();
  m_handler->processJoinKeyboardEvent(KEY_RCTRL); // a keyboard joiner opens...
  m_handler->processJoinKeyboardEvent(KEY_RSHIFT); // ...and cancels
  ASSERT_EQ(m_view.calls.size(), 2u);
  EXPECT_EQ(m_view.calls[0].device, JoinIndicatorView::JOIN_DEVICE_KEYBOARD);
  EXPECT_EQ(m_view.calls[0].state, JoinIndicatorView::JOIN_NOTREADY);
  EXPECT_EQ(m_view.calls[1].state, JoinIndicatorView::JOIN_NONE);
}

TEST_F(InputControlTest, ASelectorConfirmedOnABotThatDiedClearsItsIndicator)
{
  startRound4();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  destroyPlayer(3);
  m_view.calls.clear();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  ASSERT_EQ(m_view.calls.size(), 1u);
  EXPECT_EQ(m_view.calls[0].team, 3);
  EXPECT_EQ(m_view.calls[0].state, JoinIndicatorView::JOIN_NONE);
}

TEST_F(InputControlTest, CancellingASelectorProducesNothing)
{
  startRound4();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_B);
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
  EXPECT_FALSE(m_handler->isLocalSlot(2));
  EXPECT_EQ(m_handler->controllerForSlot(2), -1);
}

TEST_F(InputControlTest, TheSelectorCanBeSteeredToAnotherBot)
{
  startRound4();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0], ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER));
}

TEST_F(InputControlTest, ATakeoverThatTheSimulationHasNotAppliedYetIsNotOfferedToAnotherJoiner)
{
  startRound4();
  joinWithPad(NEW_PAD);                       // takes slot 2; the simulation has not heard yet
  ASSERT_TRUE(player(3)->isControlledByAI());

  m_handler->processJoinControllerEvent(OTHER_PAD, SDL_CONTROLLER_BUTTON_START);
  m_handler->processJoinControllerEvent(OTHER_PAD, SDL_CONTROLLER_BUTTON_START);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 2u);
  EXPECT_EQ(t.control[0], ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER));
  EXPECT_EQ(t.control[1], ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER)) << "the second joiner got the next bot, not the same one";
}

TEST_F(InputControlTest, ASlotTakenByAKeyboardSetIsNotOfferedToAPadEither)
{
  startRound4();
  joinWithKeyboardSet1(); // slot 2, by keys: it has no pad, only the local mask says it is claimed
  ASSERT_TRUE(player(3)->isControlledByAI());

  joinWithPad(NEW_PAD);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 2u);
  EXPECT_EQ(t.control[0], ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER));
  EXPECT_EQ(t.control[1], ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER));
}

TEST_F(InputControlTest, WithNoBotLeftASelectorOffersNothingAndProducesNothing)
{
  startRound4();
  joinWithPad(NEW_PAD);
  joinWithPad(OTHER_PAD);
  sim().step(m_handler->sampleTick());
  ASSERT_FALSE(player(3)->isControlledByAI());
  ASSERT_FALSE(player(4)->isControlledByAI());

  joinWithPad(11);
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
  EXPECT_EQ(m_handler->controllerForSlot(0), -1);
}

TEST_F(InputControlTest, ADeadBotIsNotOfferedAndASelectorOnOneThatDiesProducesNothing)
{
  startRound4();
  destroyPlayer(3);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0].slot, 3) << "slot 2 is dead, so the first alive bot is slot 3";

  // A selector open on a bot that dies before the confirm: dropped, no event.
  startRound4();
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  destroyPlayer(3);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
}

// ---------------------------------------------------------------------------
// Unplugging
// ---------------------------------------------------------------------------

TEST_F(InputControlTest, UnpluggingTheOnlyDeviceOfASlotReturnsItToTheAi)
{
  startRound4();
  ASSERT_TRUE(m_handler->isLocalSlot(1));
  ASSERT_EQ(m_handler->controllerForSlot(1), PAD);
  ASSERT_FALSE(player(2)->isControlledByAI());

  m_handler->detachController(PAD);
  EXPECT_FALSE(player(2)->isControlledByAI()) << "the input layer does not touch the player";
  EXPECT_FALSE(m_handler->isLocalSlot(1));
  EXPECT_EQ(m_handler->controllerForSlot(1), -1);

  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0], ControlEvent(1, ControlEvent::RETURN_TO_AI));

  sim().step(t);
  EXPECT_TRUE(player(2)->isControlledByAI());
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
}

TEST_F(InputControlTest, UnpluggingASlotThatHasAKeymapProducesNothing)
{
  startRound4();
  joinWithPad(NEW_PAD);                       // slot 2 on a pad...
  sim().step(m_handler->sampleTick());
  m_handler->m_keyMap[2] = m_handler->m_keyMap.at(0); // ...that also has keys (defensive path)
  ASSERT_FALSE(player(3)->isControlledByAI());

  m_handler->detachController(NEW_PAD);
  const TickInputs t = m_handler->sampleTick();
  EXPECT_TRUE(t.control.empty()) << "a player with keys stays human";
  EXPECT_TRUE(m_handler->isLocalSlot(2));
  EXPECT_EQ(m_handler->controllerForSlot(2), -1);
  sim().step(t);
  EXPECT_FALSE(player(3)->isControlledByAI());
}

TEST_F(InputControlTest, UnpluggingAKeyboardPlayersNeighbourDoesNotTouchTheKeyboardPlayer)
{
  startRound4();
  m_handler->detachController(PAD);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_NE(t.control[0].slot, 0);
  EXPECT_TRUE(m_handler->isLocalSlot(0));
}

TEST_F(InputControlTest, UnpluggingAPadThatDrivesNobodyOrADeadPlayerProducesNothing)
{
  startRound4();
  m_handler->detachController(42);   // never attached
  m_handler->detachController(-1);   // not a device at all
  EXPECT_TRUE(m_handler->sampleTick().control.empty());

  destroyPlayer(2);                   // the pad's player is dead
  m_handler->detachController(PAD);
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
}

TEST_F(InputControlTest, AnUnpluggedPadLeavesNoButtonDown)
{
  startRound4();
  m_handler->setControllerButtonState(1, InputHandler::ACTION1_KEY, true);
  m_handler->setControllerButtonState(1, InputHandler::LEFT_KEY, true);
  m_handler->detachController(PAD);
  EXPECT_EQ(m_handler->sampler().peek(1).bits, 0) << "a pad that is gone holds no button down";
  const TickInputs t = m_handler->sampleTick();
  EXPECT_EQ(t.slot[1].bits, 0) << "and its slot is not local any more, so nothing is reported";
}

TEST_F(InputControlTest, AnUnplugFollowedByAJoinOnTheSameSlotEndsHumanAgain)
{
  startRound4();
  m_handler->detachController(PAD);
  // the simulation applies the unplug: the slot is a bot and may be taken over
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(player(2)->isControlledByAI());

  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  m_handler->processJoinControllerEvent(NEW_PAD, SDL_CONTROLLER_BUTTON_START);
  const TickInputs t = m_handler->sampleTick();
  ASSERT_EQ(t.control.size(), 1u);
  EXPECT_EQ(t.control[0], ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)) << "slot 1 is the first bot now";
  sim().step(t);
  EXPECT_FALSE(player(2)->isControlledByAI());
  EXPECT_EQ(m_handler->controllerForSlot(1), NEW_PAD);
}

// ---------------------------------------------------------------------------
// Rumble
// ---------------------------------------------------------------------------

TEST_F(InputControlTest, ADamagedPadPlayerRumblesItsPadAfterTheStepAndNotBefore)
{
  startRound4();
  sim().step(m_handler->sampleTick());
  hit(2);
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(sim().hitThisTick(1));
  EXPECT_TRUE(m_rumble.calls.empty()) << "the simulation rumbles nothing";

  m_handler->rumbleForHits(sim());
  ASSERT_EQ(m_rumble.calls.size(), 1u);
  EXPECT_EQ(m_rumble.calls[0].controller, PAD);
  EXPECT_FLOAT_EQ(m_rumble.calls[0].strength, 0.5f);
  EXPECT_EQ(m_rumble.calls[0].ms, 200u);
}

TEST_F(InputControlTest, NoRumbleForAKeyboardPlayerABotOrAQuietTick)
{
  startRound4();
  sim().step(m_handler->sampleTick());
  hit(1); // the keyboard human
  hit(3); // a bot
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(sim().hitThisTick(0));
  ASSERT_TRUE(sim().hitThisTick(2));
  m_handler->rumbleForHits(sim());
  EXPECT_TRUE(m_rumble.calls.empty());

  // a tick in which the pad player is not hit
  while (countAllEntities() > 4 && sim().tick() < 400) // let the explosions die out
  {
    sim().step(m_handler->sampleTick());
  }
  sim().step(m_handler->sampleTick());
  m_handler->rumbleForHits(sim());
  EXPECT_TRUE(m_rumble.calls.empty());
}

TEST_F(InputControlTest, TwoHitsInOneTickRumbleOnce)
{
  startRound4();
  sim().step(m_handler->sampleTick());
  hit(2);
  hit(2);
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(sim().hitThisTick(1));
  m_handler->rumbleForHits(sim());
  EXPECT_EQ(m_rumble.calls.size(), 1u);
}

TEST_F(InputControlTest, ASlotThatWentBackToTheAiNoLongerRumbles)
{
  startRound4();
  m_handler->detachController(PAD);
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(player(2)->isControlledByAI());
  hit(2);
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(sim().hitThisTick(1));
  m_handler->rumbleForHits(sim());
  EXPECT_TRUE(m_rumble.calls.empty());
}

TEST_F(InputControlTest, ABotWhosePadIsStillMappedDoesNotRumble)
{
  // The simulation hands the slot back to the AI by an event this layer did not
  // produce (netplay's LEAVE, a replay): the pad is still mapped, the player is a bot.
  startRound4();
  TickInputs leave;
  leave.control.push_back(ControlEvent(1, ControlEvent::LEAVE));
  sim().step(leave);
  ASSERT_TRUE(player(2)->isControlledByAI());
  ASSERT_EQ(m_handler->controllerForSlot(1), PAD);

  hit(2);
  sim().step(m_handler->sampleTick());
  ASSERT_TRUE(sim().hitThisTick(1));
  m_handler->rumbleForHits(sim());
  EXPECT_TRUE(m_rumble.calls.empty());
}

TEST_F(InputControlTest, ATakenOverSlotRumblesItsNewPad)
{
  startRound4();
  joinWithPad(NEW_PAD);
  sim().step(m_handler->sampleTick());
  hit(3);
  sim().step(m_handler->sampleTick());
  m_handler->rumbleForHits(sim());
  ASSERT_EQ(m_rumble.calls.size(), 1u);
  EXPECT_EQ(m_rumble.calls[0].controller, NEW_PAD);
}

// ---------------------------------------------------------------------------
// A new round
// ---------------------------------------------------------------------------

TEST_F(InputControlTest, ANewRoundForgetsPendingEventsAndMidRoundMappings)
{
  startRound4();
  joinWithPad(NEW_PAD);
  m_handler->detachController(PAD);
  startRound();
  m_handler->setPointers(allPlayers(), &m_view);
  EXPECT_TRUE(m_handler->sampleTick().control.empty());
  EXPECT_EQ(m_handler->controllerForSlot(2), -1);
  EXPECT_EQ(m_handler->controllerForSlot(1), PAD) << "the menu's choice is seeded again";
  EXPECT_TRUE(m_handler->isLocalSlot(1));
  EXPECT_FALSE(m_handler->isLocalSlot(2));
}

} // namespace
} // namespace bitriot_test
