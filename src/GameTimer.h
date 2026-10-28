/*
 * GameTimer.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMETIMER_H_
#define GAMETIMER_H_

#include <cstdint>
#include <string>

namespace std
{

/**
 * A countdown measured in simulation ticks, not wall-clock time.
 *
 * The timer keeps no clock of its own: the caller passes the simulation's tick
 * number (Simulation::tick()) on every call, so the whole simulation has exactly
 * one notion of "now". setTimerTriggered(now) starts the countdown on tick `now`;
 * getTimerCompleted(now) becomes true exactly timeoutTicks ticks later, however
 * long those ticks took in real time. reset() un-latches the timer so it can be
 * used again next round.
 *
 * The timeout is passed to the constructor by the subclass (never read through a
 * virtual call from the base constructor, where it would not dispatch to the subclass).
 */
class GameTimer
{
  public:
    GameTimer(std::string name, int timeoutTicks);

    inline void setTimerTriggered(uint64_t now)
    {
      this->timerTriggered = true;
      this->triggeredAtTick = now;
    }

    inline bool isTimerTriggered() const
    {
      return timerTriggered;
    }

    // The tick the countdown started on (0 while it is not triggered).
    inline uint64_t getTriggeredAtTick() const
    {
      return triggeredAtTick;
    }

    inline bool getTimerCompleted(uint64_t now) const
    {
      return timerTriggered && now >= triggeredAtTick
             && (now - triggeredAtTick) >= (uint64_t) timeoutTicks;
    }

    // The whole per-tick protocol in one call, shared by the simulation and the
    // tests: starts the countdown the first tick the end condition holds, and
    // returns true once the countdown has run out. Call exactly once per sim tick,
    // with that tick's number (1 for the first tick of a round, counting up by one)
    // and whether the round-end condition currently holds.
    inline bool advance(uint64_t now, bool endConditionHolds)
    {
      if (!endConditionHolds)
      {
        return false;
      }
      if (!timerTriggered)
      {
        setTimerTriggered(now);
        return false;
      }
      return getTimerCompleted(now);
    }

    // Back to the untriggered state, for the start of each round.
    inline void reset()
    {
      timerTriggered = false;
      triggeredAtTick = 0;
    }

    inline int getTimeoutTicks() const
    {
      return timeoutTicks;
    }

  protected:
    // Not public and not virtual: timers are singletons that live for the whole
    // process, and nothing may delete one through a GameTimer*.
    ~GameTimer() {}

  private:
    std::string name;
    int timeoutTicks;
    uint64_t triggeredAtTick = 0;
    bool timerTriggered = false;
};

} /* namespace std */
#endif /* GAMETIMER_H_ */
