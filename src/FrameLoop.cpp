#include "FrameLoop.h"

FrameLoop::FrameLoop(Hooks hooks, uint32_t tickMs, int maxTicksPerFrame)
  : m_hooks(std::move(hooks)), m_pacer(tickMs, maxTicksPerFrame)
{
}

FrameResult FrameLoop::frame()
{
  FrameResult result;

  if (m_hooks.pumpEvents)
  {
    m_hooks.pumpEvents();
  }

  const uint32_t now = m_hooks.nowMs();
  if (!m_started || !m_hooks.simulating())
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

  result.keepRunning = m_hooks.render();
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
