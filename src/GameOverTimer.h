/*
 * GameOverTimer.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMEOVERTIMER_H_
#define GAMEOVERTIMER_H_

#include "GameTimer.h"

namespace std
{

/**
 * How long after the round-end condition first holds the round really ends.
 * Counted in simulation ticks (see GameTimer).
 */
class GameOverTimer: public std::GameTimer
{
  public:
    // The one place the countdown length is defined: 2 seconds, at the sim's
    // 33 ms per tick (GameSettings::NORMAL_TICKS_SPEED).
    static const int GAME_OVER_COUNTDOWN_TICKS = 60;

    GameOverTimer(std::string name);
    static GameTimer* getInstance();

  private:
    ~GameOverTimer() {} // singleton: never deleted
};

} /* namespace std */
#endif /* GAMEOVERTIMER_H_ */
