/*
 * GameTimer.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMETIMER_H_
#define GAMETIMER_H_

#include <string>

namespace std
{

/**
 * A countdown measured in simulation ticks, not wall-clock time.
 *
 * The sim loop calls tick() exactly once per simulation tick. setTimerTriggered()
 * starts the countdown on the current tick; getTimerCompleted() becomes true
 * exactly timeoutTicks ticks later, however long those ticks took in real time.
 * reset() un-latches the timer so it can be used again next round.
 *
 * The timeout is passed to the constructor by the subclass (never read through a
 * virtual call from the base constructor, where it would not dispatch to the subclass).
 */
class GameTimer
{
  public:
    GameTimer(std::string name, int timeoutTicks);

    // Advance the timer's clock by one simulation tick. Call once per sim tick.
    inline void tick()
    {
      ++currentTick;
    }

    inline void setTimerTriggered()
    {
      this->timerTriggered = true;
      this->triggeredAtTick = currentTick;
    }

    inline bool isTimerTriggered() const
    {
      return timerTriggered;
    }

    inline bool getTimerCompleted() const
    {
      return timerTriggered && (currentTick - triggeredAtTick) >= timeoutTicks;
    }

    // The whole per-tick protocol in one call, shared by the engine and the tests:
    // advances the clock, starts the countdown the first tick the end condition
    // holds, and returns true once the countdown has run out. Call exactly once
    // per sim tick, with whether the round-end condition currently holds.
    inline bool advance(bool endConditionHolds)
    {
      tick();
      if (!endConditionHolds)
      {
        return false;
      }
      if (!timerTriggered)
      {
        setTimerTriggered();
        return false;
      }
      return getTimerCompleted();
    }

    // Back to the untriggered state, for the start of each round.
    inline void reset()
    {
      timerTriggered = false;
      currentTick = 0;
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
    long currentTick = 0;
    long triggeredAtTick = 0;
    bool timerTriggered = false;
};

} /* namespace std */
#endif /* GAMETIMER_H_ */
