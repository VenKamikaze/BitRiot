/*
 * GameSettings.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMETIMER_H_
#define GAMETIMER_H_

#include "RuntimeException.h"
#include "Map.h"

namespace std
{

class GameTimer
{
  public:
    GameTimer(std::string name, int* seconds);

    inline void setTimerTriggered()
    {
      this->timerTriggered = true;
      this->startTime = SDL_GetTicks();
    }

    inline bool isTimerTriggered()
    {
      return timerTriggered;
    }

    inline bool getTimerCompleted()
    {
      return timerTriggered && ((SDL_GetTicks() - startTime) > (seconds*1000));
    }

    static const int STANDARD_COUNTDOWN = 5; // 5 seconds till we end the game completely;

  protected:
    virtual int getDefaultTimeout()
    {
      return STANDARD_COUNTDOWN;
    }

  private:

    std::string name;
    int seconds = -1;
    long startTime = -1;
    bool timerTriggered = false;

};

} /* namespace std */
#endif /* GAMETIMER_H_ */
