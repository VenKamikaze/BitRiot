// TickPacer.h
//
// Turns real elapsed time into whole simulation ticks. Pure arithmetic on a
// millisecond counter you hand it: no clock, no SDL, no sleeping, so a test drives
// it with a fake clock and the web build drives it from requestAnimationFrame
// time just the same.
//
//   TickPacer pacer(33, 5);
//   pacer.restart(now);               // when a round starts, or whenever ticks were not wanted
//   pacer.advance(now);               // once per frame
//   while (pacer.tickDue() && wantToStep) { step(); pacer.consumeTick(); }
//
// The backlog is capped at `maxBacklogTicks` ticks' worth. That one number is
// the whole catch-up policy: after a slow frame the sim runs up to that many
// ticks in a row to get back on pace, and a longer stall (a drag of the window,
// a netplay wait, a debugger) is forgiven rather than fast-forwarded. Time that
// is not a whole tick is kept, so 11 ms three times is one tick, not none.
#ifndef TICKPACER_H_
#define TICKPACER_H_

#include <cstdint>

class TickPacer
{
  public:
    // tickMs: real milliseconds per tick (33 in the game). maxBacklogTicks: the most
    // ticks that can ever be owed, and so the most one frame can run (at least 1).
    TickPacer(uint32_t tickMs, int maxBacklogTicks);

    // Empties the backlog and counts from `nowMs`. Call it when no ticks were being
    // run (a menu, a round starting) so the time spent there is not owed afterwards.
    void restart(uint32_t nowMs);

    // Adds the time since the last restart() or advance(). `nowMs` is a free-running
    // millisecond counter; it may wrap around 2^32 (a clock that goes backwards
    // reads as a huge gap, which the cap absorbs).
    void advance(uint32_t nowMs);

    bool tickDue() const { return m_backlogMs >= m_tickMs; }
    void consumeTick();

    uint32_t tickMs() const { return m_tickMs; }
    int maxBacklogTicks() const { return m_maxBacklogTicks; }
    uint32_t backlogMs() const { return m_backlogMs; }

    // Real milliseconds until the next tick falls due, as of `nowMs` (0 when one is
    // due already). What a loop that sleeps between frames should sleep for.
    uint32_t msUntilTickDue(uint32_t nowMs) const;

  private:
    uint32_t m_tickMs;
    int m_maxBacklogTicks;
    uint32_t m_backlogMs = 0;
    uint32_t m_lastMs = 0;
};

#endif /* TICKPACER_H_ */
