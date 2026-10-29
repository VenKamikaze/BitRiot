// FrameLoop.h
//
// One frame of the game, shared by every front end: the native main loop calls
// frame() and sleeps for sleepHintMs(); the web build (brief 09) hands the same
// frame() to emscripten_set_main_loop. frame() never blocks or sleeps.
//
// A frame, in this order:
//   1. pumpEvents()     device and window events are read, whether or not a tick runs;
//   2. real time since the last frame is added to the pacer's backlog;
//   3. while a whole tick (NORMAL_TICKS_SPEED, 33 ms) is owed, up to the per-frame
//      cap, and ticks are allowed (simulating() and canStep()): step();
//   3b. then, if extraTicks() is set, up to maxExtraTicksPerFrame (2) more steps that
//      it asks for, under the same two gates (see below);
//   4. render(), exactly once.
//
// Rendering never steps and stepping never renders: a frame with no tick draws the
// same world again (a tick held back, or a frame that came early), and a frame
// that owes three ticks runs three steps and draws once. A slow frame therefore
// catches up, up to the cap, where the old loop simply slowed the game down.
//
// Two hooks decide whether a tick may run:
//   * simulating(): is a round in progress (not a menu, not the scoreboard)? When
//     it is false the pacer is restarted every frame, so time spent in a menu is
//     never owed afterwards, and it is re-checked after every step so a step that
//     ends the round stops the frame's remaining steps.
//   * canStep(): the netplay veto (brief 07). Returning false holds ticks back
//     without blocking events or drawing; the owed time keeps building up to the
//     cap and is paid back (at most the cap's worth) when it returns true. Locally
//     it is absent, which means always yes.
//   * extraTicks(): optional catch-up. It returns how many steps beyond the clock's
//     the caller wants this frame (netplay: "I hold more confirmed ticks than I
//     should, run two more"). It is asked once per frame, after the owed ticks, the
//     answer is clamped to [0, maxExtraTicksPerFrame] (default 2), and each extra
//     step still needs simulating() and canStep(). Extra steps do not touch the
//     pacer's backlog, so they neither borrow from nor repay clock time; without
//     them a peer that stalled for more than the cap's worth (5 ticks) would never
//     close the gap, because the clock forgives the rest. So a frame runs at most
//     5 + 2 steps.
//
// A frame that starts outside a round and ends inside one (render() ran GAME_INIT's
// set-up) restarts the pacer after render(), so the time the set-up took is not
// owed as ticks on the next frame.
#ifndef FRAMELOOP_H_
#define FRAMELOOP_H_

#include <cstdint>
#include <functional>

#include "TickPacer.h"

struct FrameResult
{
  bool keepRunning = true; // false once render() asked to quit
  int ticksStepped = 0;    // how many times step() ran this frame (extra ticks included)
  int extraTicksStepped = 0; // how many of those were asked for by extraTicks()
};

class FrameLoop
{
  public:
    struct Hooks
    {
      std::function<uint32_t()> nowMs;      // required: a free-running millisecond clock
      std::function<bool()> simulating;     // required: may ticks run at all right now?
      std::function<void()> step;           // required: run one tick (input, then the sim)
      std::function<bool()> render;         // required: draw; false means quit
      std::function<bool()> canStep;        // optional: false vetoes a tick (netplay)
      std::function<void()> pumpEvents;     // optional: read devices and window events
      std::function<int()> extraTicks;      // optional: steps wanted beyond the clock's (netplay catch-up)
    };

    // The most ticks one frame can run, and the most that can ever be owed.
    static constexpr int DEFAULT_MAX_TICKS_PER_FRAME = 5;
    // The most extra steps extraTicks() can add to one frame.
    static constexpr int DEFAULT_MAX_EXTRA_TICKS_PER_FRAME = 2;

    FrameLoop(Hooks hooks, uint32_t tickMs, int maxTicksPerFrame = DEFAULT_MAX_TICKS_PER_FRAME,
              int maxExtraTicksPerFrame = DEFAULT_MAX_EXTRA_TICKS_PER_FRAME);

    FrameResult frame();

    // How long the caller should sleep before the next frame: until the next tick
    // is due, or a short poll while ticks are owed but held back. Never negative.
    uint32_t sleepHintMs() const;

    const TickPacer& pacer() const { return m_pacer; }

  private:
    Hooks m_hooks;
    TickPacer m_pacer;
    int m_maxExtraTicks;
    bool m_started = false;
    bool m_held = false; // the last frame ended with a tick owed that it did not run
};

#endif /* FRAMELOOP_H_ */
