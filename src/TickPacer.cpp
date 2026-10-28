#include "TickPacer.h"

TickPacer::TickPacer(uint32_t tickMs, int maxBacklogTicks)
  : m_tickMs(tickMs ? tickMs : 1),
    m_maxBacklogTicks(maxBacklogTicks < 1 ? 1 : maxBacklogTicks)
{
}

void TickPacer::restart(uint32_t nowMs)
{
  m_backlogMs = 0;
  m_lastMs = nowMs;
}

void TickPacer::advance(uint32_t nowMs)
{
  const uint32_t elapsed = nowMs - m_lastMs; // modular, so a wrapped counter is fine
  m_lastMs = nowMs;

  const uint64_t cap = (uint64_t) m_tickMs * (uint64_t) m_maxBacklogTicks;
  const uint64_t backlog = (uint64_t) m_backlogMs + elapsed;
  m_backlogMs = (uint32_t) (backlog > cap ? cap : backlog);
}

void TickPacer::consumeTick()
{
  m_backlogMs = (m_backlogMs >= m_tickMs) ? m_backlogMs - m_tickMs : 0;
}

uint32_t TickPacer::msUntilTickDue(uint32_t nowMs) const
{
  const uint64_t owed = (uint64_t) m_backlogMs + (uint32_t) (nowMs - m_lastMs);
  return owed >= m_tickMs ? 0 : (uint32_t) (m_tickMs - owed);
}
