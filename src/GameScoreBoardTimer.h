/*
 * GameSettings.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMESCOREBOARDTIMER_H_
#define GAMESCOREBOARDTIMER_H_

#include "RuntimeException.h"
#include "Map.h"
#include "GameTimer.h"

namespace std
{

class GameScoreBoardTimer: public std::GameTimer
{
  public:
    GameScoreBoardTimer(std::string name, int* seconds);
    static GameTimer* getInstance();
    static GameTimer* getInstance(std::string name, int* seconds);
    static const int GAME_SCOREBOARD_COUNTDOWN = 10; // 10 seconds till we return to main menu


  protected:
    int getDefaultTimeout()
    {
      return GAME_SCOREBOARD_COUNTDOWN;
    }

  private:
    virtual ~GameScoreBoardTimer();
};

} /* namespace std */
#endif /* GAMEOVERTIMER_H_ */
