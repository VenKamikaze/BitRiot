/*
 * GameSettings.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMEOVERTIMER_H_
#define GAMEOVERTIMER_H_

#include "RuntimeException.h"
#include "Map.h"
#include "GameTimer.h"

namespace std
{

class GameOverTimer: public std::GameTimer
{
  public:
    GameOverTimer(std::string name, int* seconds);
    static GameTimer* getInstance();
    static GameTimer* getInstance(std::string name, int* seconds);
    static const int GAME_OVER_COUNTDOWN = 5; // 5 seconds till we end the game completely;


  protected:
    int getDefaultTimeout()
    {
      return GAME_OVER_COUNTDOWN;
    }

  private:
    virtual ~GameOverTimer();
};

} /* namespace std */
#endif /* GAMEOVERTIMER_H_ */
