// Brief 04a: Simulation::step(const TickInputs&) and the apply rules (the table at
// the top of src/TickInputs.h).
//
// Everything here drives the simulation with scripted TickInputs: no SDL, no
// devices, no InputHandler. The sampler (the other half) is in InputSamplerTest.cpp.
//
// SLOT is playerNumber - 1 throughout. Slot 0 is player 1, who starts on tile
// (1, 1); slot 1 is player 2 on (width - 2, height - 2).

#include "support/HeadlessGame.h"
#include "support/ScriptedInput.h"

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#include "DynamicMap.h"
#include "GameSettings.h"
#include "Simulation.h"
#include "TickInputs.h"

namespace bitriot_test
{
namespace
{

using Button = PlayerInput::Button;

PlayerInput held(std::initializer_list<Button> buttons)
{
  PlayerInput in;
  for (Button b : buttons)
  {
    in.setHeld(b);
  }
  return in;
}

PlayerInput heldAndReleased(std::initializer_list<Button> down, std::initializer_list<Button> released)
{
  PlayerInput in = held(down);
  for (Button b : released)
  {
    in.setReleased(b);
  }
  return in;
}

class InputApplyTest : public HeadlessGameTest
{
  protected:
    // Two human players on the default 17x13 map, no blocks, no monsters, so that the
    // only thing that moves or appears is what the test does.
    void startHumans(int humans = 2, int players = 2)
    {
      configurePlayers(players);
      for (int i = 0; i < humans; i++)
      {
        std::GameSettings::getInstance()->setPlayerAI(i, false);
      }
      std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
      startRound();
    }

    // One tick with `in` on `slot` (everything else empty); returns after the step.
    void tick(int slot, PlayerInput in, int times = 1)
    {
      TickInputs inputs;
      inputs.slot[slot] = in;
      for (int i = 0; i < times; i++)
      {
        sim().step(inputs);
      }
    }

    const Simulation::SlotInputState& state(int slot) { return sim().inputState(slot); }

    int countType(EntityType type)
    {
      int n = 0;
      for (const auto& entry : em().getDynamicMap()->entityList)
      {
        if (entry.second->getType() == type)
        {
          n++;
        }
      }
      return n;
    }
};

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, AHeldDirectionMovesThePlayerThatWay)
{
  startHumans();
  // Player 2 starts in the opposite corner, so it has room to go left and up; player
  // 1 has room to go right and down.
  struct Case { int slot; Button button; int dx; int dy; };
  const Case cases[] = {
    {0, PlayerInput::RIGHT, 1, 0}, {0, PlayerInput::DOWN, 0, 1},
    {1, PlayerInput::LEFT, -1, 0}, {1, PlayerInput::UP, 0, -1},
  };
  for (const Case& c : cases)
  {
    startHumans();
    const int x0 = player(c.slot + 1)->pixelX();
    const int y0 = player(c.slot + 1)->pixelY();
    tick(c.slot, held({c.button}), 6);
    const int dx = player(c.slot + 1)->pixelX() - x0;
    const int dy = player(c.slot + 1)->pixelY() - y0;
    EXPECT_GT(dx * c.dx + dy * c.dy, 0) << "slot " << c.slot << " button " << (int) c.button << " moved the wrong way";
    EXPECT_EQ(dx * c.dy, 0) << "it must not drift across";
    EXPECT_EQ(dy * c.dx, 0) << "it must not drift across";
  }
}

TEST_F(InputApplyTest, NoInputMeansNoMovement)
{
  startHumans();
  const int x0 = player(1)->pixelX();
  const int y0 = player(1)->pixelY();
  tick(0, PlayerInput(), 10);
  EXPECT_EQ(player(1)->pixelX(), x0);
  EXPECT_EQ(player(1)->pixelY(), y0);
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_FALSE(state(0).action2Down);
}

TEST_F(InputApplyTest, DirectionPrecedenceIsUpThenDownThenLeftThenRight)
{
  // Player 2 (slot 1) starts at (15, 11), the bottom-right corner: open to the west
  // and the north, walled in to the south and the east. Each case holds several
  // directions and checks which one won by where the player went (the sign of the
  // move on each axis).
  struct Case { std::initializer_list<Button> buttons; int sx; int sy; };
  const Case cases[] = {
    {{PlayerInput::UP, PlayerInput::DOWN}, 0, -1},
    {{PlayerInput::UP, PlayerInput::LEFT}, 0, -1},
    {{PlayerInput::UP, PlayerInput::RIGHT}, 0, -1},
    {{PlayerInput::DOWN, PlayerInput::LEFT}, 0, 0},  // down wins, and is a wall: LEFT does not take over
    {{PlayerInput::DOWN, PlayerInput::RIGHT}, 0, 0},
    {{PlayerInput::LEFT, PlayerInput::RIGHT}, -1, 0},
    {{PlayerInput::UP, PlayerInput::DOWN, PlayerInput::LEFT, PlayerInput::RIGHT}, 0, -1},
  };
  for (const Case& c : cases)
  {
    startHumans();
    const int x0 = player(2)->pixelX();
    const int y0 = player(2)->pixelY();
    tick(1, held(c.buttons), 4);
    const int dx = player(2)->pixelX() - x0;
    const int dy = player(2)->pixelY() - y0;
    EXPECT_EQ((dx > 0) - (dx < 0), c.sx) << "horizontal move " << dx;
    EXPECT_EQ((dy > 0) - (dy < 0), c.sy) << "vertical move " << dy;
  }
}

TEST_F(InputApplyTest, MovementIsAppliedBeforeTheActionThatBeginsAHoldOnTheSameTick)
{
  startHumans();
  const int x0 = player(1)->pixelX();
  tick(0, held({PlayerInput::RIGHT, PlayerInput::ACTION1}));
  EXPECT_GT(player(1)->pixelX(), x0) << "the tick that presses A1 still moves";
  EXPECT_TRUE(state(0).action1Down);
  const int x1 = player(1)->pixelX();
  tick(0, held({PlayerInput::RIGHT, PlayerInput::ACTION1}), 5);
  EXPECT_EQ(player(1)->pixelX(), x1) << "from the next tick on, the hold blocks movement";
}

// ---------------------------------------------------------------------------
// Holding an action: selection
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, ActionOneWithADirectionSelectsTheItem)
{
  struct Case { Button direction; EntityType item; };
  const Case cases[] = {
    {PlayerInput::UP, BOMB}, {PlayerInput::DOWN, BLOCK},
    {PlayerInput::LEFT, MINE}, {PlayerInput::RIGHT, ROCKET_SPIN},
  };
  for (const Case& c : cases)
  {
    startHumans();
    tick(0, held({PlayerInput::ACTION1})); // the hold begins, with the default
    EXPECT_TRUE(state(0).action1Down);
    EXPECT_EQ(state(0).spawnSelection, BLOCK);
    tick(0, held({PlayerInput::ACTION1, c.direction}));
    EXPECT_EQ(state(0).spawnSelection, c.item) << "direction " << (int) c.direction;
    EXPECT_TRUE(state(0).action1Down);
    EXPECT_FALSE(state(0).action2Down);
  }
}

TEST_F(InputApplyTest, ActionOneSelectionPrecedenceAndItSticks)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, held({PlayerInput::ACTION1, PlayerInput::RIGHT, PlayerInput::LEFT}));
  EXPECT_EQ(state(0).spawnSelection, MINE) << "left beats right";
  tick(0, held({PlayerInput::ACTION1, PlayerInput::RIGHT, PlayerInput::LEFT, PlayerInput::DOWN}));
  EXPECT_EQ(state(0).spawnSelection, BLOCK) << "down beats left and right";
  tick(0, held({PlayerInput::ACTION1, PlayerInput::RIGHT, PlayerInput::LEFT, PlayerInput::DOWN, PlayerInput::UP}));
  EXPECT_EQ(state(0).spawnSelection, BOMB) << "up beats everything";
  tick(0, held({PlayerInput::ACTION1}), 3);
  EXPECT_EQ(state(0).spawnSelection, BOMB) << "the selection stays after the direction is let go";
}

TEST_F(InputApplyTest, ActionTwoSelectsEggOneAtOnceAndDirectionsSelectTheOthers)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action2Down);
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_EQ(state(0).spawnSelection, EGG1) << "A2 selects EGG1 on the press itself";

  struct Case { Button direction; EntityType egg; };
  const Case cases[] = {
    {PlayerInput::UP, EGG3}, {PlayerInput::DOWN, EGG5}, {PlayerInput::LEFT, EGG2}, {PlayerInput::RIGHT, EGG4},
  };
  for (const Case& c : cases)
  {
    tick(0, held({PlayerInput::ACTION2, c.direction}));
    EXPECT_EQ(state(0).spawnSelection, c.egg) << "direction " << (int) c.direction;
  }
  tick(0, held({PlayerInput::ACTION2, PlayerInput::RIGHT, PlayerInput::LEFT}));
  EXPECT_EQ(state(0).spawnSelection, EGG2) << "left beats right";
  tick(0, held({PlayerInput::ACTION2, PlayerInput::RIGHT, PlayerInput::LEFT, PlayerInput::UP, PlayerInput::DOWN}));
  EXPECT_EQ(state(0).spawnSelection, EGG3) << "up beats down, left and right";
}

TEST_F(InputApplyTest, HoldingAnActionBlocksMovement)
{
  for (Button action : {PlayerInput::ACTION1, PlayerInput::ACTION2})
  {
    startHumans();
    tick(0, held({action}));
    const int x0 = player(1)->pixelX();
    const int y0 = player(1)->pixelY();
    tick(0, held({action, PlayerInput::RIGHT}), 8);
    tick(0, held({action, PlayerInput::DOWN}), 8);
    EXPECT_EQ(player(1)->pixelX(), x0);
    EXPECT_EQ(player(1)->pixelY(), y0);
  }
}

TEST_F(InputApplyTest, ActionOneBeatsActionTwoWhenBothArePressed)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION1, PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action1Down);
  EXPECT_FALSE(state(0).action2Down);
}

// ---------------------------------------------------------------------------
// Placing
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, PressThenReleasePlacesTheSelectedItem)
{
  struct Case { Button action; Button direction; bool useDirection; EntityType item; };
  const Case cases[] = {
    {PlayerInput::ACTION1, PlayerInput::UP, false, BLOCK}, // no direction: the default
    {PlayerInput::ACTION1, PlayerInput::UP, true, BOMB},
    {PlayerInput::ACTION1, PlayerInput::DOWN, true, BLOCK},
    {PlayerInput::ACTION1, PlayerInput::LEFT, true, MINE},
    {PlayerInput::ACTION1, PlayerInput::RIGHT, true, ROCKET_SPIN},
    {PlayerInput::ACTION2, PlayerInput::UP, false, EGG1},
    {PlayerInput::ACTION2, PlayerInput::LEFT, true, EGG2},
    {PlayerInput::ACTION2, PlayerInput::UP, true, EGG3},
    {PlayerInput::ACTION2, PlayerInput::RIGHT, true, EGG4},
    {PlayerInput::ACTION2, PlayerInput::DOWN, true, EGG5},
  };
  for (const Case& c : cases)
  {
    startHumans();
    const int health = player(1)->getHealth();
    tick(0, held({c.action}));
    if (c.useDirection)
    {
      tick(0, held({c.action, c.direction}));
    }
    EXPECT_EQ(countType(c.item), 0) << "nothing is placed until the release";
    tick(0, heldAndReleased({}, {c.action}));
    EXPECT_EQ(countType(c.item), 1) << "item " << (int) c.item;
    EXPECT_LT(player(1)->getHealth(), health) << "placing costs energy";
    EXPECT_FALSE(state(0).action1Down);
    EXPECT_FALSE(state(0).action2Down);
    EXPECT_EQ(state(0).spawnSelection, BLOCK) << "the selection goes back to BLOCK";
  }
}

TEST_F(InputApplyTest, TheDirectionHeldOnTheReleaseTickStillCounts)
{
  // The direction is applied before the release, as it always was: letting go of the
  // action and the direction together places the item the direction selected.
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, heldAndReleased({PlayerInput::UP}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BOMB), 1);
  EXPECT_EQ(countType(BLOCK), 0);
}

TEST_F(InputApplyTest, NothingIsPlacedOnATileWithAStaticEntityButTheHoldEnds)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  ASSERT_EQ(countType(BLOCK), 1) << "a block now stands under the player";
  const int health = player(1)->getHealth();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1) << "no second block on a tile that already holds one";
  EXPECT_EQ(player(1)->getHealth(), health) << "and nothing was paid for";
  EXPECT_FALSE(state(0).action1Down) << "the hold ended all the same";
  EXPECT_EQ(state(0).spawnSelection, BLOCK);
}

TEST_F(InputApplyTest, ReleasingWhileStillHoldingTheButtonStillPlaces)
{
  // HELD and RELEASED together (released and pressed again inside the window) during a
  // hold: the release counts, then the next tick's HELD begins a fresh hold. For A1:
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, heldAndReleased({PlayerInput::ACTION1}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_FALSE(state(0).action1Down);
  tick(0, held({PlayerInput::ACTION1}));
  EXPECT_TRUE(state(0).action1Down);

  // and for A2:
  startHumans();
  tick(0, held({PlayerInput::ACTION2}));
  tick(0, heldAndReleased({PlayerInput::ACTION2}, {PlayerInput::ACTION2}));
  EXPECT_EQ(countType(EGG1), 1);
  EXPECT_FALSE(state(0).action2Down);
  tick(0, held({PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action2Down) << "and the button still being down starts the next hold";
  EXPECT_EQ(state(0).spawnSelection, EGG1);
}

// ---------------------------------------------------------------------------
// Both actions: the preserved rules
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, TheOtherActionIsIgnoredWhileAHoldIsOn)
{
  // A1 hold: A2 pressed, held and released does nothing.
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, held({PlayerInput::ACTION1, PlayerInput::ACTION2}), 3);
  tick(0, heldAndReleased({PlayerInput::ACTION1}, {PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action1Down);
  EXPECT_FALSE(state(0).action2Down);
  EXPECT_EQ(countType(EGG1), 0);
  EXPECT_EQ(countType(BLOCK), 0) << "an A2 release is not an A1 release";

  // A2 hold: A1 pressed, held and released does nothing.
  startHumans();
  tick(0, held({PlayerInput::ACTION2}));
  tick(0, held({PlayerInput::ACTION2, PlayerInput::ACTION1}), 3);
  tick(0, heldAndReleased({PlayerInput::ACTION2}, {PlayerInput::ACTION1}));
  EXPECT_TRUE(state(0).action2Down);
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_EQ(countType(BLOCK), 0);
  EXPECT_EQ(countType(EGG1), 0) << "an A1 release is not an A2 release";
  EXPECT_EQ(state(0).spawnSelection, EGG1);
}

TEST_F(InputApplyTest, AnActionStillHeldWhenTheOtherHoldEndsStartsItsHoldNextTick)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, held({PlayerInput::ACTION1, PlayerInput::ACTION2}), 2); // A2 pressed during the A1 hold
  tick(0, heldAndReleased({PlayerInput::ACTION2}, {PlayerInput::ACTION1})); // A1 let go, A2 still down
  EXPECT_EQ(countType(BLOCK), 1) << "A1 placed its block";
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_FALSE(state(0).action2Down) << "A2 is not looked at on the tick A1's hold ends (as before)";
  tick(0, held({PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action2Down) << "it starts on the next tick";
  EXPECT_EQ(state(0).spawnSelection, EGG1);
}

// ---------------------------------------------------------------------------
// The same-tick tap (the one documented change)
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, ASameTickTapOfActionOnePlacesTheDefaultBlock)
{
  // The sampler reports a press-and-release inside one tick as RELEASED with HELD
  // clear. Before brief 04a that press never reached the game (the key was up by the
  // time input was applied), and the release was ignored with no hold to end.
  startHumans();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1) << "A1's default item is BLOCK";
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_EQ(state(0).spawnSelection, BLOCK);
}

TEST_F(InputApplyTest, ASameTickTapOfActionTwoPlacesEggOne)
{
  startHumans();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION2}));
  EXPECT_EQ(countType(EGG1), 1) << "A2's default item is EGG1";
  EXPECT_FALSE(state(0).action2Down);
  EXPECT_EQ(state(0).spawnSelection, BLOCK);
}

TEST_F(InputApplyTest, ATapLeavesNoStaleReleaseBehindToEndTheNextHold)
{
  // The old bug: the lost tap left its release pending, and the next real hold of that
  // action was ended on its first tick. Now a hold after a tap is just a hold.
  startHumans();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  tick(0, held({PlayerInput::ACTION1}), 4);
  EXPECT_TRUE(state(0).action1Down);
  EXPECT_EQ(countType(BLOCK), 1) << "only the tap's block";
}

TEST_F(InputApplyTest, ATapMovesThePlayerOnTheSameTickLikeAnyPress)
{
  startHumans();
  const int x0 = player(1)->pixelX();
  tick(0, heldAndReleased({PlayerInput::RIGHT}, {PlayerInput::ACTION1}));
  EXPECT_GT(player(1)->pixelX(), x0);
  EXPECT_EQ(countType(BLOCK), 1);
}

TEST_F(InputApplyTest, ATapOnATileWithAStaticEntityPlacesNothing)
{
  startHumans();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  ASSERT_EQ(countType(BLOCK), 1);
  const int health = player(1)->getHealth();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_EQ(player(1)->getHealth(), health);
  EXPECT_FALSE(state(0).action1Down);
}

TEST_F(InputApplyTest, ATapBouncedBackDownBeginsAFreshHoldNextTick)
{
  // Pressed, released and pressed again within one tick: HELD and RELEASED.
  startHumans();
  tick(0, heldAndReleased({PlayerInput::ACTION1}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_FALSE(state(0).action1Down);
  tick(0, held({PlayerInput::ACTION1}));
  EXPECT_TRUE(state(0).action1Down);
}

TEST_F(InputApplyTest, WhenBothActionsAreInvolvedActionOnesTapWinsAndTheOthersIsDropped)
{
  // A1 tapped while A2 is held: the A1 tap places, then A2's hold begins next tick.
  startHumans();
  tick(0, heldAndReleased({PlayerInput::ACTION2}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_FALSE(state(0).action2Down);
  tick(0, held({PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action2Down);

  // A1 held and A2 tapped: A1's hold begins and A2's tap is not remembered.
  startHumans();
  tick(0, heldAndReleased({PlayerInput::ACTION1}, {PlayerInput::ACTION2}));
  EXPECT_TRUE(state(0).action1Down);
  EXPECT_EQ(countType(EGG1), 0);
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_EQ(countType(EGG1), 0) << "no stale A2 release comes back later";

  // Both tapped in the same tick: A1 wins, A2's tap is dropped.
  startHumans();
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1, PlayerInput::ACTION2}));
  EXPECT_EQ(countType(BLOCK), 1);
  EXPECT_EQ(countType(EGG1), 0);
}

// ---------------------------------------------------------------------------
// Who is driven
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, InputForAnAIPlayerOrAnEmptySlotIsIgnored)
{
  startHumans(1, 2); // slot 0 human, slot 1 a bot
  TickInputs inputs;
  inputs.slot[1] = held({PlayerInput::ACTION1, PlayerInput::UP});
  inputs.slot[4] = held({PlayerInput::ACTION1}); // no such player
  inputs.slot[5] = heldAndReleased({}, {PlayerInput::ACTION2});
  for (int i = 0; i < 5; i++)
  {
    sim().step(inputs);
  }
  EXPECT_FALSE(state(1).action1Down) << "a bot's slot keeps no input state";
  EXPECT_FALSE(state(4).action1Down);
  EXPECT_EQ(state(5).spawnSelection, BLOCK);
  EXPECT_EQ(state(1).spawnSelection, BLOCK);
}

TEST_F(InputApplyTest, InputForADeadPlayerIsIgnored)
{
  startHumans();
  killPlayer(1);
  const int x0 = player(1)->pixelX();
  tick(0, held({PlayerInput::RIGHT}), 6);
  tick(0, heldAndReleased({}, {PlayerInput::ACTION1}));
  EXPECT_EQ(player(1)->pixelX(), x0);
  EXPECT_EQ(countType(BLOCK), 0);
  EXPECT_FALSE(state(0).action1Down);
}

TEST_F(InputApplyTest, AnotherSlotsInputDoesNotLeakIntoThisOne)
{
  startHumans();
  const int x0 = player(1)->pixelX();
  tick(1, held({PlayerInput::ACTION1}));
  tick(1, held({PlayerInput::ACTION1, PlayerInput::UP}));
  EXPECT_TRUE(state(1).action1Down);
  EXPECT_EQ(state(1).spawnSelection, BOMB);
  EXPECT_FALSE(state(0).action1Down);
  EXPECT_EQ(state(0).spawnSelection, BLOCK);
  EXPECT_EQ(player(1)->pixelX(), x0);
}

TEST_F(InputApplyTest, InputStateIsClearedByANewRound)
{
  startHumans();
  tick(0, held({PlayerInput::ACTION2}));
  tick(0, held({PlayerInput::ACTION2, PlayerInput::UP}));
  ASSERT_TRUE(state(0).action2Down);
  startRound();
  EXPECT_FALSE(state(0).action2Down);
  EXPECT_EQ(state(0).spawnSelection, BLOCK);
}

TEST_F(InputApplyTest, ASlotOutsideTheRangeIsRefused)
{
  startHumans();
  EXPECT_THROW(sim().inputState(-1), std::out_of_range);
  EXPECT_THROW(sim().inputState(std::GameSettings::MAX_PLAYERS), std::out_of_range);
  EXPECT_NO_THROW(sim().inputState(std::GameSettings::MAX_PLAYERS - 1));
}

TEST_F(InputApplyTest, ControlEventsAreNotSupportedYetAndAreNotSilentlyDropped)
{
  startHumans();
  TickInputs inputs;
  inputs.control.push_back(ControlEvent());
  const uint64_t tickBefore = sim().tick();
  EXPECT_THROW(sim().step(inputs), std::logic_error);
  EXPECT_EQ(sim().tick(), tickBefore) << "a refused step does nothing";
}

TEST_F(InputApplyTest, SteppingWithoutInputsIsSteppingWithEmptyInputs)
{
  MatchConfig config;
  config.seed = 99;
  config.mapWidth = 13;
  config.mapHeight = 11;
  config.blockPercentage = 55;
  for (int i = 0; i < 3; i++)
  {
    MatchConfig::Slot slot;
    slot.team = i;
    slot.ai = (i != 0);
    config.slots.push_back(slot);
  }
  std::vector<uint64_t> a, b;
  sim().start(config);
  for (int i = 0; i < 200; i++)
  {
    sim().step();
    a.push_back(sim().stateHash());
  }
  sim().start(config);
  for (int i = 0; i < 200; i++)
  {
    sim().step(TickInputs());
    b.push_back(sim().stateHash());
  }
  EXPECT_EQ(a, b);
}

// ---------------------------------------------------------------------------
// The hash
// ---------------------------------------------------------------------------

TEST_F(InputApplyTest, ThePerSlotInputStateIsPartOfTheHash)
{
  // Two worlds that differ in nothing but what a held action has selected.
  auto selectionHash = [&](Button direction) {
    setRoundSeed(4242); // the same round every time
    startHumans();
    tick(0, held({PlayerInput::ACTION1}));
    tick(0, held({PlayerInput::ACTION1, direction}));
    return sim().stateHash();
  };
  const uint64_t bomb = selectionHash(PlayerInput::UP);
  const uint64_t mine = selectionHash(PlayerInput::LEFT);
  EXPECT_NE(bomb, mine);
  EXPECT_EQ(bomb, selectionHash(PlayerInput::UP)) << "and the same input gives the same hash";

  // A1 held versus A2 held (both begin a hold, nothing else differs once selection is
  // normalised): the action flags themselves are hashed.
  setRoundSeed(4242);
  startHumans();
  tick(0, held({PlayerInput::ACTION1}));
  tick(0, held({PlayerInput::ACTION1}));
  const uint64_t a1 = sim().stateHash();
  setRoundSeed(4242);
  startHumans();
  tick(0, held({PlayerInput::ACTION2}));
  tick(0, held({PlayerInput::ACTION2}));
  EXPECT_NE(a1, sim().stateHash());
}

TEST_F(InputApplyTest, TheSameSeedAndTheSameInputsGiveTheSameHashOnEveryTick)
{
  MatchConfig config;
  config.seed = 20260101;
  config.mapWidth = 17;
  config.mapHeight = 13;
  config.blockPercentage = 55;
  for (int i = 0; i < 4; i++)
  {
    MatchConfig::Slot slot;
    slot.team = i;
    slot.ai = (i >= 2); // two humans, two bots
    config.slots.push_back(slot);
  }
  const std::vector<TickInputs> script = randomInputs(7, 900, {0, 1});

  std::vector<uint64_t> first, second, other;
  for (int run = 0; run < 3; run++)
  {
    sim().start(config);
    std::vector<uint64_t>& hashes = run == 0 ? first : (run == 1 ? second : other);
    hashes.push_back(sim().stateHash());
    for (size_t t = 0; t < script.size() && !sim().roundOver(); t++)
    {
      if (run == 2 && t == 300)
      {
        TickInputs changed = script[t];
        changed.slot[0].setHeld(PlayerInput::ACTION1, !changed.slot[0].held(PlayerInput::ACTION1));
        sim().step(changed);
      }
      else
      {
        sim().step(script[t]);
      }
      hashes.push_back(sim().stateHash());
    }
  }
  ASSERT_GT(first.size(), 300u);
  EXPECT_EQ(first, second);
  ASSERT_EQ(first.size(), other.size());
  EXPECT_EQ(std::vector<uint64_t>(first.begin(), first.begin() + 300), std::vector<uint64_t>(other.begin(), other.begin() + 300))
      << "the runs agree until the one changed input";
  EXPECT_NE(first, other) << "a different input at one tick ends in a different world";
}

// ---------------------------------------------------------------------------
// The old rules, as an oracle
// ---------------------------------------------------------------------------

// The old InputHandler::processPlayerButtons, minus the world: the same state
// transitions, with the releases sticky as the old devices kept them (a release stays
// pending until the first read that looks for it).
struct OldRules
{
  bool a1 = false;
  bool a2 = false;
  EntityType sel = BLOCK;
  bool pending[PlayerInput::NUM_BUTTONS] = {false, false, false, false, false, false};
  uint8_t prev = 0;

  void tick(uint8_t heldBits)
  {
    for (int b = 0; b < PlayerInput::NUM_BUTTONS; b++)
    {
      if ((prev & (1u << b)) && !(heldBits & (1u << b)))
      {
        pending[b] = true;
      }
    }
    prev = heldBits;
    const auto isDown = [&](Button b) { return (heldBits & (1u << b)) != 0; };
    const auto isReleased = [&](Button b) { const bool r = pending[b]; pending[b] = false; return r; };

    if (a1 == false && a2 == false)
    {
      if (isDown(PlayerInput::ACTION1))
      {
        a1 = true;
      }
      else if (isDown(PlayerInput::ACTION2))
      {
        a2 = true;
        sel = EGG1;
      }
    }
    else if (a1)
    {
      if (isDown(PlayerInput::UP)) { sel = BOMB; }
      else if (isDown(PlayerInput::DOWN)) { sel = BLOCK; }
      else if (isDown(PlayerInput::LEFT)) { sel = MINE; }
      else if (isDown(PlayerInput::RIGHT)) { sel = ROCKET_SPIN; }
      if (isReleased(PlayerInput::ACTION1))
      {
        a1 = false;
        sel = BLOCK;
      }
    }
    else
    {
      if (isDown(PlayerInput::UP)) { sel = EGG3; }
      else if (isDown(PlayerInput::DOWN)) { sel = EGG5; }
      else if (isDown(PlayerInput::LEFT)) { sel = EGG2; }
      else if (isDown(PlayerInput::RIGHT)) { sel = EGG4; }
      if (isReleased(PlayerInput::ACTION2))
      {
        a2 = false;
        sel = BLOCK;
      }
    }
  }

  bool anyStale() const
  {
    for (int b = PlayerInput::ACTION1; b <= PlayerInput::ACTION2; b++)
    {
      if (pending[b])
      {
        return true;
      }
    }
    return false;
  }
};

TEST(OldRulesTest, TheOldRulesLostATapAndLeftItsReleasePendingToEndALaterHold)
{
  // What brief 04a fixed, shown on the model of the old code. A press and release
  // between two applies is never seen held (no tick has the key down), and the
  // release is only looked at inside a hold, so the old code did nothing with it and
  // left it pending. The next real press then began a hold and the first tick of that
  // hold found the stale release and placed at once: an item the player did not mean
  // to place, a tick after a press they were still holding.
  OldRules old;
  old.tick(0);                       // nothing held at the tick the tap fell between...
  old.pending[PlayerInput::ACTION1] = true; // ...but the device recorded the release
  EXPECT_FALSE(old.a1) << "the tap placed nothing";
  old.tick(PlayerInput::heldBit(PlayerInput::ACTION1)); // a real press, still held
  EXPECT_TRUE(old.a1);
  old.tick(PlayerInput::heldBit(PlayerInput::ACTION1)); // its first tick as a hold
  EXPECT_FALSE(old.a1) << "the stale release ended the hold while the button was still down";
}

TEST_F(InputApplyTest, OnEveryInputTheOldRulesHandledWellTheNewRulesGiveTheSameStates)
{
  // Gestures that never press a button on one tick and release it on the same one
  // (taps) and never leave a release for an action nobody is holding: on those, the old
  // rules and the new agree on every tick. Anything outside that is the documented
  // change, covered above.
  startHumans();
  OldRules old;
  int compared = 0;
  for (int seed = 1; seed <= 6; seed++)
  {
    startHumans();
    old = OldRules();
    const std::vector<PlayerInput> script = gestureInputs(seed * 1000 + 17, 700);
    for (size_t t = 0; t < script.size(); t++)
    {
      setPlayerHealth(1, 100);
      TickInputs inputs;
      inputs.slot[0] = script[t];
      sim().step(inputs);
      old.tick(script[t].bits & PlayerInput::HELD_MASK);
      if (!player(1) || !player(1)->isAlive())
      {
        break; // killed by its own bombs: a dead player takes no input
      }
      ASSERT_EQ(state(0).action1Down, old.a1) << "seed " << seed << " tick " << t;
      ASSERT_EQ(state(0).action2Down, old.a2) << "seed " << seed << " tick " << t;
      ASSERT_EQ((int) state(0).spawnSelection, (int) old.sel) << "seed " << seed << " tick " << t;
      EXPECT_FALSE(old.anyStale()) << "the script left a stale release (a test bug), seed " << seed << " tick " << t;
      compared++;
    }
  }
  EXPECT_GT(compared, 1500);
}

} // namespace
} // namespace bitriot_test
