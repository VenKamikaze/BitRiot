// The frame loop (brief 03b): a fixed-tick accumulator driven by a fake clock.
//
// FrameLoop and TickPacer take their time from a hook, never from SDL, so these
// tests run the exact code the game runs without a window, a device or a real
// clock.

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "FrameLoop.h"
#include "TickPacer.h"

namespace
{

const uint32_t TICK = 33;
const int CAP = 5;

// A scripted game: a clock the test moves, and hooks that count what the loop does.
struct FakeGame
{
  uint32_t now = 0;
  bool simulating = true;
  bool canStep = true;
  bool renderResult = true;
  int steps = 0;
  int renders = 0;
  int pumps = 0;
  std::vector<std::string> log;
  std::function<void()> onStep;
  std::function<int()> extra; // when set, becomes the extraTicks() hook
  std::function<void()> onRender;

  FrameLoop::Hooks hooks()
  {
    FrameLoop::Hooks h;
    h.nowMs = [this] { return now; };
    h.simulating = [this] { return simulating; };
    h.canStep = [this] { return canStep; };
    h.pumpEvents = [this] { pumps++; log.push_back("pump"); };
    h.step = [this]
    {
      steps++;
      log.push_back("step");
      if (onStep)
      {
        onStep();
      }
    };
    h.render = [this]
    {
      renders++;
      log.push_back("render");
      if (onRender)
      {
        onRender();
      }
      return renderResult;
    };
    if (extra)
    {
      h.extraTicks = extra;
    }
    return h;
  }
};

// ---------------------------------------------------------------------------
// TickPacer
// ---------------------------------------------------------------------------

TEST(TickPacerTest, WholeTicksAreOwedOnlyOnceEnoughTimeHasPassed)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(1000);
  EXPECT_FALSE(pacer.tickDue());
  pacer.advance(1032);
  EXPECT_FALSE(pacer.tickDue()) << "32 ms is not a tick";
  pacer.advance(1033);
  EXPECT_TRUE(pacer.tickDue());
  pacer.consumeTick();
  EXPECT_FALSE(pacer.tickDue());
  EXPECT_EQ(pacer.backlogMs(), 0u);
}

TEST(TickPacerTest, TimeThatIsNotAWholeTickIsKept)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(0);
  pacer.advance(40); // 1 tick and 7 ms
  EXPECT_TRUE(pacer.tickDue());
  pacer.consumeTick();
  EXPECT_EQ(pacer.backlogMs(), 7u);
  pacer.advance(66); // +26 -> 33
  EXPECT_TRUE(pacer.tickDue());
}

TEST(TickPacerTest, TheBacklogIsCappedAtTheMostATickCanOwe)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(0);
  pacer.advance(1000000);
  EXPECT_EQ(pacer.backlogMs(), TICK * CAP);
  int owed = 0;
  while (pacer.tickDue())
  {
    pacer.consumeTick();
    owed++;
  }
  EXPECT_EQ(owed, CAP);
}

TEST(TickPacerTest, RestartForgetsTheBacklog)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(0);
  pacer.advance(100);
  ASSERT_TRUE(pacer.tickDue());
  pacer.restart(5000);
  EXPECT_FALSE(pacer.tickDue());
  EXPECT_EQ(pacer.backlogMs(), 0u);
  pacer.advance(5010);
  EXPECT_EQ(pacer.backlogMs(), 10u) << "counting from the restart";
}

TEST(TickPacerTest, ConsumingWithNothingOwedDoesNotUnderflow)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(0);
  pacer.consumeTick();
  EXPECT_EQ(pacer.backlogMs(), 0u);
  EXPECT_FALSE(pacer.tickDue());
}

TEST(TickPacerTest, ACounterThatWrapsAroundIsStillElapsedTime)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(0xFFFFFFF0u);
  pacer.advance(0xFFFFFFF0u + 33u); // wraps past 2^32
  EXPECT_EQ(pacer.backlogMs(), 33u);
  EXPECT_TRUE(pacer.tickDue());
}

TEST(TickPacerTest, MillisecondsUntilTheNextTickCountDown)
{
  TickPacer pacer(TICK, CAP);
  pacer.restart(100);
  EXPECT_EQ(pacer.msUntilTickDue(100), 33u);
  EXPECT_EQ(pacer.msUntilTickDue(120), 13u);
  EXPECT_EQ(pacer.msUntilTickDue(133), 0u);
  EXPECT_EQ(pacer.msUntilTickDue(500), 0u);
  pacer.advance(120);
  EXPECT_EQ(pacer.msUntilTickDue(125), 8u);
}

TEST(TickPacerTest, DegenerateSettingsAreClamped)
{
  TickPacer pacer(0, 0);
  EXPECT_EQ(pacer.tickMs(), 1u);
  EXPECT_EQ(pacer.maxBacklogTicks(), 1);
}

// ---------------------------------------------------------------------------
// FrameLoop
// ---------------------------------------------------------------------------

TEST(FrameLoopTest, NTicksOfElapsedTimeGiveNSteps)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame(); // the first frame starts the clock

  for (int i = 0; i < 10; i++)
  {
    game.now += TICK;
    const FrameResult r = loop.frame();
    EXPECT_EQ(r.ticksStepped, 1) << "frame " << i;
  }
  EXPECT_EQ(game.steps, 10);
  EXPECT_EQ(game.renders, 11);
}

TEST(FrameLoopTest, TimeThatIsNotAWholeTickCarriesOver)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += 11;
  EXPECT_EQ(loop.frame().ticksStepped, 0);
  game.now += 11;
  EXPECT_EQ(loop.frame().ticksStepped, 0);
  game.now += 11;
  EXPECT_EQ(loop.frame().ticksStepped, 1) << "three 11 ms frames are one tick";
  game.now += 33;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
  EXPECT_EQ(game.steps, 2);
}

TEST(FrameLoopTest, ASlowFrameCatchesUpInsteadOfSlowingTheGame)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += 3 * TICK; // the frame took three ticks' worth of time
  const FrameResult r = loop.frame();
  EXPECT_EQ(r.ticksStepped, 3);
  EXPECT_EQ(game.renders, 2) << "one render for the three steps";
}

TEST(FrameLoopTest, ALargeGapIsCappedNotFastForwarded)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += 10000; // a stall: 300 ticks of real time
  EXPECT_EQ(loop.frame().ticksStepped, CAP);
  // And then back on pace, with no debt left over.
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
  EXPECT_EQ(game.steps, CAP + 2);
}

TEST(FrameLoopTest, CanStepFalseHoldsTicksButNeverBlocksEventsOrDrawing)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  const int pumpsBefore = game.pumps;
  const int rendersBefore = game.renders;

  game.canStep = false;
  for (int i = 0; i < 8; i++)
  {
    game.now += TICK;
    const FrameResult r = loop.frame();
    EXPECT_EQ(r.ticksStepped, 0);
    EXPECT_TRUE(r.keepRunning);
  }
  EXPECT_EQ(game.steps, 0);
  EXPECT_EQ(game.pumps - pumpsBefore, 8) << "events are pumped every frame";
  EXPECT_EQ(game.renders - rendersBefore, 8) << "and the frame is drawn every frame";
}

TEST(FrameLoopTest, TimeHeldBackIsPaidBackUpToTheCap)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  // Held for two ticks' time, released: nothing is lost under the cap.
  game.canStep = false;
  game.now += TICK;
  loop.frame();
  game.now += TICK;
  loop.frame();
  game.canStep = true;
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 3);

  // Held for a long time: only the cap is paid back.
  game.canStep = false;
  for (int i = 0; i < 20; i++)
  {
    game.now += TICK;
    loop.frame();
  }
  game.canStep = true;
  EXPECT_EQ(loop.frame().ticksStepped, CAP);
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1) << "back on pace";
}

TEST(FrameLoopTest, CanStepIsAskedBeforeEveryStep)
{
  FakeGame game;
  int allowed = 2; // netplay holds the inputs for two more ticks
  FrameLoop::Hooks hooks = game.hooks();
  hooks.canStep = [&] { return allowed > 0; };
  hooks.step = [&] { game.steps++; allowed--; };
  FrameLoop loop(hooks, TICK, CAP);
  loop.frame();

  game.now += 4 * TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 2) << "owed four, allowed two";
  EXPECT_EQ(game.steps, 2);
}

TEST(FrameLoopTest, AMissingCanStepMeansAlwaysYes)
{
  FakeGame game;
  FrameLoop::Hooks hooks = game.hooks();
  hooks.canStep = nullptr;
  FrameLoop loop(hooks, TICK, CAP);
  loop.frame();
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
}

TEST(FrameLoopTest, NothingStepsOutsideARoundAndTheTimeSpentThereIsNotOwed)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.simulating = false; // a menu
  for (int i = 0; i < 100; i++)
  {
    game.now += TICK;
    EXPECT_EQ(loop.frame().ticksStepped, 0);
  }
  EXPECT_EQ(game.steps, 0);
  EXPECT_EQ(game.renders, 101);

  game.simulating = true; // the round starts
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1) << "one tick, not a burst for the minutes spent in the menu";
}

TEST(FrameLoopTest, AStepThatEndsTheRoundStopsTheRestOfTheFramesSteps)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.onStep = [&] { if (game.steps == 2) game.simulating = false; };
  game.now += 5 * TICK;
  const FrameResult r = loop.frame();
  EXPECT_EQ(r.ticksStepped, 2);
  EXPECT_EQ(game.steps, 2);
  EXPECT_EQ(game.renders, 2) << "the frame still draws, once";
}

TEST(FrameLoopTest, AFrameIsEventsThenStepsThenOneRender)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.log.clear();

  game.now += 2 * TICK;
  loop.frame();
  const std::vector<std::string> expected = {"pump", "step", "step", "render"};
  EXPECT_EQ(game.log, expected);

  game.log.clear();
  game.now += 10; // no tick due: events and a render, nothing else
  loop.frame();
  const std::vector<std::string> idle = {"pump", "render"};
  EXPECT_EQ(game.log, idle);
}

TEST(FrameLoopTest, RenderingAskingToQuitEndsTheLoop)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  EXPECT_TRUE(loop.frame().keepRunning);
  game.renderResult = false;
  game.now += TICK;
  EXPECT_FALSE(loop.frame().keepRunning);
}

TEST(FrameLoopTest, TheSleepHintIsTheTimeUntilTheNextTick)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.now += TICK;
  loop.frame(); // stepped; nothing owed
  EXPECT_EQ(loop.sleepHintMs(), 33u);
  game.now += 7;
  EXPECT_EQ(loop.sleepHintMs(), 26u);
  game.now += 100;
  EXPECT_EQ(loop.sleepHintMs(), 0u) << "already late: do not sleep";
}

TEST(FrameLoopTest, AHeldTickMakesTheLoopPollNotSpin)
{
  FakeGame game;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.canStep = false;
  game.now += TICK;
  loop.frame();
  EXPECT_GE(loop.sleepHintMs(), 1u) << "a tick is owed but vetoed: never a zero-length sleep";
  EXPECT_LE(loop.sleepHintMs(), 10u) << "and look again soon";
}

TEST(FrameLoopTest, OutsideARoundTheSleepHintHoldsTheFrameRate)
{
  FakeGame game;
  game.simulating = false;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.now += 10; // the frame's work took 10 ms
  EXPECT_EQ(loop.sleepHintMs(), 23u);
}

TEST(FrameLoopTest, TheClockWrappingAroundDoesNotConfuseTheLoop)
{
  FakeGame game;
  game.now = 0xFFFFFFF0u;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.now += TICK; // wraps past 2^32
  EXPECT_EQ(loop.frame().ticksStepped, 1);
}

// ---------------------------------------------------------------------------
// extraTicks(): catch-up beyond the clock (brief 07)
// ---------------------------------------------------------------------------

TEST(FrameLoopTest, ExtraTicksRunOnTopOfTheOwedOnesAndAreBounded)
{
  FakeGame game;
  game.extra = [] { return 100; }; // wants far more than allowed
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += TICK; // one tick owed
  const FrameResult r = loop.frame();
  EXPECT_EQ(r.ticksStepped, 1 + 2) << "one owed, and the default bound of two extra";
  EXPECT_EQ(r.extraTicksStepped, 2);
  EXPECT_EQ(FrameLoop::DEFAULT_MAX_EXTRA_TICKS_PER_FRAME, 2);
}

TEST(FrameLoopTest, ExtraTicksAreAskedOnceAfterTheOwedTicksAndHonourTheAnswer)
{
  FakeGame game;
  int asked = 0;
  game.extra = [&] { asked++; return 1; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  asked = 0;

  game.now += 3 * TICK;
  game.log.clear();
  const FrameResult r = loop.frame();
  EXPECT_EQ(asked, 1);
  EXPECT_EQ(r.ticksStepped, 3 + 1);
  const std::vector<std::string> expected = {"pump", "step", "step", "step", "step", "render"};
  EXPECT_EQ(game.log, expected);
}

TEST(FrameLoopTest, ExtraTicksRunEvenWhenTheClockOwesNothing)
{
  FakeGame game;
  game.extra = [] { return 2; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += 5; // no tick due
  const FrameResult r = loop.frame();
  EXPECT_EQ(r.ticksStepped, 2);
  EXPECT_EQ(r.extraTicksStepped, 2);
}

TEST(FrameLoopTest, ExtraTicksDoNotBorrowFromOrRepayTheClock)
{
  FakeGame game;
  bool wantExtra = true;
  game.extra = [&] { return wantExtra ? 2 : 0; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.now += 5;
  loop.frame(); // two extra ticks, 5 ms of clock
  wantExtra = false;
  game.now += 28; // 33 ms in all: exactly one tick of clock time
  EXPECT_EQ(loop.frame().ticksStepped, 1) << "the extra ticks did not use up the 5 ms already counted";
  game.now += 33;
  EXPECT_EQ(loop.frame().ticksStepped, 1);
}

TEST(FrameLoopTest, ExtraTicksStillNeedCanStep)
{
  FakeGame game;
  game.extra = [] { return 2; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.canStep = false;
  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 0);

  int allowed = 0; // netplay holds inputs for no ticks at first
  FrameLoop::Hooks hooks = game.hooks();
  hooks.canStep = [&] { return allowed > 0; };
  hooks.step = [&] { game.steps++; allowed--; };
  FrameLoop limited(hooks, TICK, CAP);
  limited.frame();
  allowed = 1; // now it holds inputs for one more tick only
  game.now += 5;
  game.steps = 0;
  EXPECT_EQ(limited.frame().ticksStepped, 1) << "asked for two, allowed one";
}

TEST(FrameLoopTest, ExtraTicksStopWhenAStepEndsTheRoundAndNeverRunOutsideOne)
{
  FakeGame game;
  game.extra = [] { return 2; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.onStep = [&] { game.simulating = false; }; // the first step ends the round
  game.now += 5;
  EXPECT_EQ(loop.frame().ticksStepped, 1);

  game.onStep = nullptr;
  game.simulating = false; // a menu
  game.now += TICK;
  game.steps = 0;
  EXPECT_EQ(loop.frame().ticksStepped, 0);
  EXPECT_EQ(game.steps, 0);
}

TEST(FrameLoopTest, AMissingNegativeOrZeroExtraTicksMeansNone)
{
  FakeGame plain;
  FrameLoop none(plain.hooks(), TICK, CAP);
  none.frame();
  plain.now += 5;
  EXPECT_EQ(none.frame().ticksStepped, 0);

  FakeGame game;
  int answer = -3;
  game.extra = [&] { return answer; };
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.now += 5;
  EXPECT_EQ(loop.frame().ticksStepped, 0) << "negative";
  answer = 0;
  game.now += 5;
  EXPECT_EQ(loop.frame().ticksStepped, 0);

  FakeGame off;
  off.extra = [] { return 2; };
  FrameLoop disabled(off.hooks(), TICK, CAP, /*maxExtraTicksPerFrame=*/0);
  disabled.frame();
  off.now += 5;
  EXPECT_EQ(disabled.frame().ticksStepped, 0) << "a bound of zero turns the hook off";
}

TEST(FrameLoopTest, ASetUpHeavyFrameThatStartsARoundDoesNotOweItsSetUpTimeAsTicks)
{
  // The GAME_INIT frame: it starts outside a round, and render() builds the world
  // (window, bitmaps, HUD), which takes a while, then the round is running.
  FakeGame game;
  game.simulating = false;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();

  game.onRender = [&]
  {
    game.now += 500; // set-up
    game.simulating = true;
    game.onRender = nullptr;
  };
  game.now += TICK;
  const FrameResult setUpFrame = loop.frame();
  EXPECT_EQ(setUpFrame.ticksStepped, 0);

  game.now += TICK;
  EXPECT_EQ(loop.frame().ticksStepped, 1) << "one tick, not a burst for the 500 ms of set-up";
}

TEST(FrameLoopTest, AMenuFrameThatStaysAMenuStillPacesFromItsOwnStart)
{
  // The restart after render() is only for a frame that ended inside a round: a
  // menu frame's sleep hint still counts the frame's own work.
  FakeGame game;
  game.simulating = false;
  FrameLoop loop(game.hooks(), TICK, CAP);
  loop.frame();
  game.onRender = [&] { game.now += 10; };
  loop.frame();
  EXPECT_EQ(loop.sleepHintMs(), 23u);
}

} // namespace
