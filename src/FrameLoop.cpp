#include "FrameLoop.h"

FrameLoop::FrameLoop(Hooks hooks, uint32_t tickMs, int maxTicksPerFrame, int maxExtraTicksPerFrame)
  : m_hooks(std::move(hooks)), m_pacer(tickMs, maxTicksPerFrame),
    m_maxExtraTicks(maxExtraTicksPerFrame < 0 ? 0 : maxExtraTicksPerFrame)
{
}

FrameResult FrameLoop::frame()
{
  FrameResult result;

  if (m_hooks.pumpEvents)
  {
    m_hooks.pumpEvents();
  }

  // Did this frame start inside a round? (If not, render() may be what starts one.)
  const bool insideRound = m_started && m_hooks.simulating();

  const uint32_t now = m_hooks.nowMs();
  if (!insideRound)
  {
    // Nothing is owed for time spent outside a round (or before the first frame).
    m_pacer.restart(now);
    m_started = true;
  }
  else
  {
    m_pacer.advance(now);
  }

  // The pacer already caps the backlog at maxBacklogTicks, so this loop cannot
  // run more than that many steps; the explicit bound is just belt and braces.
  int budget = m_pacer.maxBacklogTicks();
  while (budget > 0 && m_pacer.tickDue() && m_hooks.simulating()
         && (!m_hooks.canStep || m_hooks.canStep()))
  {
    m_hooks.step();
    m_pacer.consumeTick();
    result.ticksStepped++;
    budget--;
  }

  m_held = m_hooks.simulating() && m_pacer.tickDue();

  // Catch-up beyond what the clock owes (brief 07: a peer that holds more confirmed
  // ticks than it should). Asked once, after the owed ticks, so the answer can
  // reflect them; bounded; and each step is still gated like any other.
  if (m_hooks.extraTicks && m_maxExtraTicks > 0 && m_hooks.simulating())
  {
    int wanted = m_hooks.extraTicks();
    if (wanted > m_maxExtraTicks)
    {
      wanted = m_maxExtraTicks;
    }
    while (wanted > 0 && m_hooks.simulating() && (!m_hooks.canStep || m_hooks.canStep()))
    {
      m_hooks.step();
      result.ticksStepped++;
      result.extraTicksStepped++;
      wanted--;
    }
  }

  result.keepRunning = m_hooks.render();

  if (!insideRound && m_hooks.simulating())
  {
    // render() started a round (GAME_INIT: window, bitmaps, HUD). The time that took
    // is setup, not game time: do not owe it as ticks on the next frame.
    m_pacer.restart(m_hooks.nowMs());
  }
  return result;
}

uint32_t FrameLoop::sleepHintMs() const
{
  const uint32_t untilDue = m_pacer.msUntilTickDue(m_hooks.nowMs());
  if (m_held && untilDue == 0)
  {
    // A tick is owed but was held back (or the frame hit its cap). Do not spin:
    // look again in a few milliseconds.
    return 2;
  }
  return untilDue;
}
