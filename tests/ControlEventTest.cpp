// Brief 04b: control events (who controls a slot), hitThisTick, and the sim's
// per-slot selection view.
//
// Everything here drives Simulation::step() with scripted TickInputs: no SDL, no
// devices, no InputHandler. (The local half - the join selector, the unplug, the
// rumble that follows a hit - is in InputControlTest.cpp, in the core executable.)
//
// SLOT is playerNumber - 1 throughout.

#include "support/HeadlessGame.h"
#include "support/ScriptedInput.h"

#include <cstdint>
#include <initializer_list>
#include <list>
#include <stdexcept>
#include <vector>

#include "AIGoal.h"
#include "AIInstruction.h"
#include "DynamicMap.h"
#include "GameSettings.h"
#include "Simulation.h"
#include "TickInputs.h"

namespace bitriot_test
{
namespace
{

using Kind = ControlEvent::Kind;

// The protected AI state of a player, to see exactly what a switch does to it.
struct ControlAccess : public PlayerCharacterEntity
{
  static int PlayerCharacterEntity::* aiFrame() { return &ControlAccess::m_aiFrame; }
  static std::list<AIGoal*> PlayerCharacterEntity::* goals() { return &ControlAccess::m_goalList; }
  static std::list<AIInstruction*> PlayerCharacterEntity::* instructions() { return &ControlAccess::m_insList; }
};

TickInputs withEvents(std::initializer_list<ControlEvent> events)
{
  TickInputs t;
  for (const ControlEvent& e : events)
  {
    t.control.push_back(e);
  }
  return t;
}

PlayerInput held(std::initializer_list<PlayerInput::Button> buttons)
{
  PlayerInput in;
  for (PlayerInput::Button b : buttons)
  {
    in.setHeld(b);
  }
  return in;
}

class ControlEventTest : public HeadlessGameTest
{
  protected:
    // `bots` bots on the default map: no spawner (nothing else shoots), no blocks.
    // `humans` of them (the first slots) start as humans.
    void start(int players, int humans = 0)
    {
      configurePlayers(players);
      for (int i = 0; i < humans; i++)
      {
        std::GameSettings::getInstance()->setPlayerAI(i, false);
      }
      std::GameSettings::getInstance()->setNpcSpawnerEnabled(false);
      setRoundSeed(ROUND_SEED); // a test that starts twice compares like with like
      startRound();
    }

    static constexpr uint64_t ROUND_SEED = 424242;

    int aiFrame(int playerNumber) { return (*player(playerNumber)).*ControlAccess::aiFrame(); }
    std::list<AIGoal*>& goals(int playerNumber) { return (*player(playerNumber)).*ControlAccess::goals(); }
    std::list<AIInstruction*>& instructions(int playerNumber)
    {
      return (*player(playerNumber)).*ControlAccess::instructions();
    }

    void stepEmpty(int ticks = 1)
    {
      for (int i = 0; i < ticks; i++)
      {
        sim().step();
      }
    }

    void stepWith(int slot, PlayerInput in, int ticks = 1)
    {
      TickInputs t;
      t.slot[slot] = in;
      for (int i = 0; i < ticks; i++)
      {
        sim().step(t);
      }
    }

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

    // Plants a goal and an instruction no bot would make (tile 77, 77), to see whether
    // they survive a switch.
    void plantSentinelPlans(int playerNumber)
    {
      goals(playerNumber).push_back(new AIGoal(AIGoal::GOAL_SECURE_AREA, 3, 77, 77, PLAYER_CHARACTER, -1));
      instructions(playerNumber).push_back(new AIInstruction(AIInstruction::GO_TO, 0, PLAYER_CHARACTER, 77, 77));
    }
    bool hasSentinelPlans(int playerNumber)
    {
      bool goal = false, instruction = false;
      for (const AIGoal* g : goals(playerNumber))
      {
        goal = goal || (g->m_tileX == 77 && g->m_tileY == 77);
      }
      for (const AIInstruction* i : instructions(playerNumber))
      {
        instruction = instruction || (i->m_targetTileX == 77 && i->m_targetTileY == 77);
      }
      return goal || instruction;
    }
};

// ---------------------------------------------------------------------------
// HUMAN_TAKES_OVER
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, ATakeoverStopsTheAiOnExactlyThatTick)
{
  start(2);
  stepEmpty(40);
  ASSERT_TRUE(player(1)->isControlledByAI());
  // the AI runs on every ordinary tick
  const int before = aiFrame(1);
  stepEmpty();
  ASSERT_EQ(aiFrame(1), before + 1) << "precondition: a bot's AI frame advances once per tick";
  const int frame = aiFrame(1);

  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));

  EXPECT_FALSE(player(1)->isControlledByAI());
  EXPECT_EQ(aiFrame(1), frame) << "the bot did not run on the tick it was taken over";
  EXPECT_EQ(aiFrame(2), frame + 1 + 0) << "the other bot did";
  stepEmpty(5);
  EXPECT_EQ(aiFrame(1), frame) << "and never runs again while it is a human";
}

TEST_F(ControlEventTest, TheBotRunsOnTheTickBeforeAndNotOnTheTickOfTheEvent)
{
  // The same round twice: the event on tick N moves the bot's last AI tick from N to N-1.
  start(2);
  stepEmpty(30);
  const int frameBefore = aiFrame(1);
  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  EXPECT_EQ(aiFrame(1), frameBefore);

  start(2);
  stepEmpty(31);
  EXPECT_EQ(aiFrame(1), frameBefore + 1) << "without the event the same tick runs the AI";
}

TEST_F(ControlEventTest, ATakenOverPlayerWithNoInputStaysWhereItIs)
{
  start(2);
  stepEmpty(100);
  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  const int x = player(1)->pixelX();
  const int y = player(1)->pixelY();
  stepEmpty(100);
  EXPECT_EQ(player(1)->pixelX(), x);
  EXPECT_EQ(player(1)->pixelY(), y);
}

TEST_F(ControlEventTest, ATakenOverPlayerMovesByTheInputOfTheTickOfTheEvent)
{
  // Slot 0 starts on tile (1, 1). Taken over at tick 1 with RIGHT held, it moves on
  // that very tick; a bot given the same tick would be waiting (its first instruction
  // is a WAIT of 23 or more ticks) and an input is ignored for a bot.
  start(2);
  const int x0 = player(1)->pixelX();
  TickInputs t = withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)});
  t.slot[0] = held({PlayerInput::RIGHT});
  sim().step(t);
  EXPECT_GT(player(1)->pixelX(), x0) << "the event is applied before the input of its own tick";

  // Without the event the same input is ignored.
  start(2);
  const int x1 = player(1)->pixelX();
  TickInputs ignored;
  ignored.slot[0] = held({PlayerInput::RIGHT});
  sim().step(ignored);
  EXPECT_EQ(player(1)->pixelX(), x1) << "a bot's slot ignores input";
}

TEST_F(ControlEventTest, ATakeoverClearsTheBotsPlans)
{
  start(2);
  stepEmpty(100);
  ASSERT_TRUE(!goals(1).empty() || !instructions(1).empty()) << "precondition: the bot has a plan";
  plantSentinelPlans(1);
  ASSERT_TRUE(hasSentinelPlans(1));

  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));

  EXPECT_TRUE(goals(1).empty());
  EXPECT_TRUE(instructions(1).empty());
}

// ---------------------------------------------------------------------------
// RETURN_TO_AI
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, ReturnToAiResumesTheAiOnExactlyThatTickWithClearedGoals)
{
  start(2, /*humans=*/1);
  stepEmpty(40);
  ASSERT_FALSE(player(1)->isControlledByAI());
  plantSentinelPlans(1);
  const int frame = aiFrame(1);
  EXPECT_EQ(frame, 0) << "a human never ran the AI";

  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI)}));

  EXPECT_TRUE(player(1)->isControlledByAI());
  EXPECT_EQ(aiFrame(1), frame + 1) << "the AI ran on the very tick of the event";
  EXPECT_FALSE(hasSentinelPlans(1)) << "the plan from before the switch is gone";
}

TEST_F(ControlEventTest, ABotThatIsReturnedPlansFromScratch)
{
  // take over, plant a plan, give it back: the stale plan must not run.
  start(2);
  stepEmpty(10);
  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  plantSentinelPlans(1);
  stepEmpty(5);
  ASSERT_TRUE(hasSentinelPlans(1)) << "a human keeps whatever is in the lists (nothing consumes them)";
  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI)}));
  EXPECT_FALSE(hasSentinelPlans(1));
  stepEmpty(200);
  EXPECT_FALSE(hasSentinelPlans(1));
}

TEST_F(ControlEventTest, TheInputOfTheTickOfAReturnIsIgnored)
{
  start(2, 1);
  TickInputs t = withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI)});
  t.slot[0] = held({PlayerInput::ACTION1});
  sim().step(t);
  EXPECT_FALSE(sim().inputState(0).action1Down) << "the slot is a bot from the start of this tick";
}

TEST_F(ControlEventTest, LeaveDoesWhatReturnToAiDoes)
{
  // Same round, same ticks, the event differing only in its kind: identical hashes.
  std::vector<uint64_t> a, b;
  for (Kind kind : {ControlEvent::RETURN_TO_AI, ControlEvent::LEAVE})
  {
    start(3, 2);
    std::vector<uint64_t>& out = kind == ControlEvent::LEAVE ? b : a;
    stepEmpty(20);
    sim().step(withEvents({ControlEvent(1, kind)}));
    out.push_back(sim().stateHash());
    for (int i = 0; i < 100; i++)
    {
      sim().step();
      out.push_back(sim().stateHash());
    }
    EXPECT_TRUE(player(2)->isControlledByAI());
  }
  EXPECT_EQ(a, b);
}

// ---------------------------------------------------------------------------
// What a switch does and does not do
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, BothTransitionsResetTheSlotsInputState)
{
  start(2, 1);
  stepWith(0, held({PlayerInput::ACTION1, PlayerInput::UP}), 2); // the first tick begins the hold
  ASSERT_TRUE(sim().inputState(0).action1Down);
  ASSERT_EQ(sim().inputState(0).spawnSelection, BOMB);

  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI)}));
  EXPECT_FALSE(sim().inputState(0).action1Down);
  EXPECT_FALSE(sim().inputState(0).action2Down);
  EXPECT_EQ(sim().inputState(0).spawnSelection, BLOCK) << "a phantom hold would show on the next device";

  // and the next device starts clean: with the old hold gone, a bare release is a tap
  // (the tap rule places the default BLOCK); with a leftover hold it would have laid
  // the BOMB the old device had selected.
  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  PlayerInput release;
  release.setReleased(PlayerInput::ACTION1);
  stepWith(0, release);
  EXPECT_EQ(countType(BOMB), 0) << "the old hold's BOMB selection did not come back";
  EXPECT_EQ(countType(BLOCK), 1) << "a fresh slot: the tap placed the default";
}

TEST_F(ControlEventTest, ATransitionResetsAHoldInProgressOnTheTakeoverToo)
{
  // A human holds A1 with MINE selected, is unplugged and the slot is taken over again
  // by another device: the new device does not inherit the hold.
  start(2, 1);
  stepWith(0, held({PlayerInput::ACTION1, PlayerInput::LEFT}), 2);
  ASSERT_EQ(sim().inputState(0).spawnSelection, MINE);
  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI), ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  EXPECT_FALSE(player(1)->isControlledByAI());
  EXPECT_FALSE(sim().inputState(0).action1Down);
  EXPECT_EQ(sim().inputState(0).spawnSelection, BLOCK);
}

TEST_F(ControlEventTest, AnEventThatChangesNothingDoesNothing)
{
  // A takeover of a human and a return of a bot are no-ops: nothing is cleared, nothing
  // reset. Compared with a round that gets no event at all, tick by tick.
  const auto play = [&](bool withNoOps)
  {
    start(3, 1);
    stepWith(0, held({PlayerInput::ACTION1, PlayerInput::UP}));
    std::vector<uint64_t> hashes;
    for (int i = 0; i < 60; i++)
    {
      TickInputs t;
      t.slot[0] = held({PlayerInput::ACTION1, PlayerInput::UP});
      if (withNoOps && i % 7 == 3)
      {
        t.control.push_back(ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)); // already a human
        t.control.push_back(ControlEvent(1, ControlEvent::RETURN_TO_AI));     // already a bot
        t.control.push_back(ControlEvent(2, ControlEvent::LEAVE));            // already a bot
      }
      sim().step(t);
      hashes.push_back(sim().stateHash());
    }
    return hashes;
  };
  const std::vector<uint64_t> quiet = play(false);
  const std::vector<uint64_t> noisy = play(true);
  EXPECT_EQ(quiet, noisy);
}

TEST_F(ControlEventTest, ADeadPlayersEventsAreIgnored)
{
  const auto play = [&](bool withEvent)
  {
    start(3, 1);
    stepEmpty(10);
    destroyPlayer(1);
    std::vector<uint64_t> hashes;
    for (int i = 0; i < 30; i++)
    {
      TickInputs t;
      if (withEvent)
      {
        t.control.push_back(ControlEvent(0, ControlEvent::RETURN_TO_AI));
      }
      sim().step(t);
      hashes.push_back(sim().stateHash());
    }
    return hashes;
  };
  EXPECT_EQ(play(false), play(true));
}

TEST_F(ControlEventTest, TwoEventsForOneSlotEndInTheLastOne)
{
  start(3);
  sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER), ControlEvent(0, ControlEvent::RETURN_TO_AI)}));
  EXPECT_TRUE(player(1)->isControlledByAI());
  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI), ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)}));
  EXPECT_FALSE(player(1)->isControlledByAI());
}

TEST_F(ControlEventTest, EventsForSeveralSlotsAreAllApplied)
{
  start(4);
  sim().step(withEvents({ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER), ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)}));
  EXPECT_TRUE(player(1)->isControlledByAI());
  EXPECT_FALSE(player(2)->isControlledByAI());
  EXPECT_TRUE(player(3)->isControlledByAI());
  EXPECT_FALSE(player(4)->isControlledByAI());
}

TEST_F(ControlEventTest, TheControlFlagStaysPutBetweenEvents)
{
  start(2);
  sim().step(withEvents({ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)}));
  stepEmpty(300);
  EXPECT_FALSE(player(2)->isControlledByAI());
  EXPECT_TRUE(player(1)->isControlledByAI());
}

// ---------------------------------------------------------------------------
// Refused events
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, ASlotOutsideTheRoundIsRefusedBeforeAnythingHappens)
{
  start(2);
  stepEmpty(5);
  const uint64_t tick = sim().tick();
  const uint64_t hash = sim().stateHash();

  // The valid event first: it must not be applied when a later one is refused.
  TickInputs t = withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER), ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER)});
  EXPECT_THROW(sim().step(t), std::out_of_range);
  EXPECT_EQ(sim().tick(), tick);
  EXPECT_EQ(sim().stateHash(), hash) << "a refused step changes nothing";
  EXPECT_TRUE(player(1)->isControlledByAI());

  EXPECT_THROW(sim().step(withEvents({ControlEvent(-1, ControlEvent::RETURN_TO_AI)})), std::out_of_range);
  EXPECT_THROW(sim().step(withEvents({ControlEvent(std::GameSettings::MAX_PLAYERS, ControlEvent::RETURN_TO_AI)})),
               std::out_of_range);
  EXPECT_EQ(sim().tick(), tick);
}

TEST_F(ControlEventTest, AnUnknownKindIsRefused)
{
  start(2);
  const uint64_t tick = sim().tick();
  EXPECT_THROW(sim().step(withEvents({ControlEvent(0, static_cast<Kind>(9))})), std::invalid_argument);
  EXPECT_EQ(sim().tick(), tick);
}

TEST_F(ControlEventTest, EventsBeforeStartAreRefusedLikeAnyStep)
{
  EXPECT_THROW(sim().step(withEvents({ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)})), std::logic_error);
}

// ---------------------------------------------------------------------------
// The control state is in the hash, and a scripted sequence is deterministic
// ---------------------------------------------------------------------------

struct ScriptedEvent
{
  int tick;
  ControlEvent event;
};

// A 4-player round, slots 0 and 1 humans from the start, with takeovers, returns and a
// leave over 700 ticks, and random scripted input for slots 0..3 (what a bot's slot
// gets is ignored).
const ScriptedEvent EVENT_SCRIPT[] = {
  {40, ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER)},
  {95, ControlEvent(0, ControlEvent::RETURN_TO_AI)},
  {95, ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER)},
  {210, ControlEvent(2, ControlEvent::LEAVE)},
  {260, ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER)},
  {300, ControlEvent(1, ControlEvent::RETURN_TO_AI)},
  {301, ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)},
  {150, ControlEvent(3, ControlEvent::RETURN_TO_AI)},
  {380, ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER)},
};

TEST_F(ControlEventTest, AScriptedSequenceOfSwitchesGivesTheSameHashOnEveryTickOfEveryRun)
{
  const std::vector<TickInputs> inputs = randomInputs(77, 700, {0, 1, 2, 3});
  const auto run = [&](int skipEvent)
  {
    start(4, 2);
    std::vector<uint64_t> hashes;
    hashes.push_back(sim().stateHash());
    for (int t = 0; t < 700 && !sim().roundOver(); t++)
    {
      TickInputs in = inputs[t];
      for (int i = 0; i < (int) (sizeof(EVENT_SCRIPT) / sizeof(EVENT_SCRIPT[0])); i++)
      {
        if (i != skipEvent && EVENT_SCRIPT[i].tick == t + 1)
        {
          in.control.push_back(EVENT_SCRIPT[i].event);
        }
      }
      sim().step(in);
      hashes.push_back(sim().stateHash());
    }
    return hashes;
  };

  const std::vector<uint64_t> first = run(-1);
  const std::vector<uint64_t> second = run(-1);
  ASSERT_GT(first.size(), 600u) << "the round should still be running while the script plays";
  EXPECT_EQ(first, second);

  // Each event matters: leaving any one out changes the hashes from its tick on.
  for (int i = 0; i < (int) (sizeof(EVENT_SCRIPT) / sizeof(EVENT_SCRIPT[0])); i++)
  {
    const std::vector<uint64_t> without = run(i);
    EXPECT_NE(without, first) << "event " << i << " changed nothing";
    // identical until the event's tick
    for (int t = 0; t < EVENT_SCRIPT[i].tick && t < (int) without.size() && t < (int) first.size(); t++)
    {
      ASSERT_EQ(without[t], first[t]) << "event " << i << " tick " << t;
    }
  }
}

// ---------------------------------------------------------------------------
// hitThisTick
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, ADamagedPlayerIsRecordedAsHitForThatTickOnly)
{
  start(3, 2);
  stepEmpty(3);
  for (int slot = 0; slot < 3; slot++)
  {
    EXPECT_FALSE(sim().hitThisTick(slot)) << "slot " << slot << " before anything";
  }
  const int hp = player(2)->getHealth();

  spawnEntity(EXPLOSION, 0, player(2)->getTileX(), player(2)->getTileY());
  stepEmpty();

  ASSERT_LT(player(2)->getHealth(), hp) << "precondition: the explosion hurt player 2";
  EXPECT_TRUE(sim().hitThisTick(1));
  EXPECT_FALSE(sim().hitThisTick(0));
  EXPECT_FALSE(sim().hitThisTick(2));

  // The flag follows the tick: a quiet tick clears it.
  for (int i = 0; i < 40; i++)
  {
    stepEmpty();
    if (countType(EXPLOSION) == 0)
    {
      break;
    }
  }
  stepEmpty();
  EXPECT_FALSE(sim().hitThisTick(1));
}

TEST_F(ControlEventTest, ABotThatIsHitIsRecordedToo)
{
  // The simulation does not know about devices: any player can be hit. The local layer
  // decides whether anyone is holding a pad.
  start(2);
  stepEmpty(3);
  spawnEntity(EXPLOSION, 0, player(1)->getTileX(), player(1)->getTileY());
  stepEmpty();
  EXPECT_TRUE(sim().hitThisTick(0));
}

TEST_F(ControlEventTest, TheHitFlagIsClearAfterStartAndSlotsAreChecked)
{
  start(2, 1);
  stepEmpty(3);
  spawnEntity(EXPLOSION, 0, player(1)->getTileX(), player(1)->getTileY());
  stepEmpty();
  ASSERT_TRUE(sim().hitThisTick(0));

  startRound();
  EXPECT_FALSE(sim().hitThisTick(0)) << "a new round has been hit by nothing yet";

  EXPECT_THROW(sim().hitThisTick(-1), std::out_of_range);
  EXPECT_THROW(sim().hitThisTick(std::GameSettings::MAX_PLAYERS), std::out_of_range);
}

TEST_F(ControlEventTest, ADeadPlayersFinalHitIsRecorded)
{
  // The hit that kills a player is still a hit for that tick (the pad buzzes as the
  // player dies, as it always did), and the roster keeps the player.
  start(2, 1);
  stepEmpty(3);
  setPlayerHealth(1, 1);
  spawnEntity(EXPLOSION, 0, player(1)->getTileX(), player(1)->getTileY());
  stepEmpty();
  EXPECT_TRUE(sim().hitThisTick(0));
}

// ---------------------------------------------------------------------------
// The selection view
// ---------------------------------------------------------------------------

TEST_F(ControlEventTest, TheHeldSelectionIsTheItemTheHeldActionHasSelected)
{
  start(2, 2);
  EXPECT_EQ(sim().heldSelection(0), PLAYER_CHARACTER) << "nothing held: the no-selection marker";
  stepWith(0, held({PlayerInput::ACTION1}));
  EXPECT_EQ(sim().heldSelection(0), BLOCK);
  stepWith(0, held({PlayerInput::ACTION1, PlayerInput::UP}));
  EXPECT_EQ(sim().heldSelection(0), BOMB);
  EXPECT_EQ(sim().heldSelection(1), PLAYER_CHARACTER) << "another slot is unaffected";

  PlayerInput release;
  release.setReleased(PlayerInput::ACTION1);
  stepWith(0, release);
  EXPECT_EQ(sim().heldSelection(0), PLAYER_CHARACTER) << "released: back to nothing";

  stepWith(1, held({PlayerInput::ACTION2, PlayerInput::LEFT}), 2); // the first tick begins the hold
  EXPECT_EQ(sim().heldSelection(1), EGG2);

  EXPECT_THROW(sim().heldSelection(-1), std::out_of_range);
  EXPECT_THROW(sim().heldSelection(std::GameSettings::MAX_PLAYERS), std::out_of_range);
}

TEST_F(ControlEventTest, ASwitchEmptiesTheHeldSelectionToo)
{
  start(2, 1);
  stepWith(0, held({PlayerInput::ACTION2, PlayerInput::DOWN}), 2);
  ASSERT_EQ(sim().heldSelection(0), EGG5);
  sim().step(withEvents({ControlEvent(0, ControlEvent::RETURN_TO_AI)}));
  EXPECT_EQ(sim().heldSelection(0), PLAYER_CHARACTER);
}

} // namespace
} // namespace bitriot_test
