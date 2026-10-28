/*
 * GameOverTimer.cpp
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#include "GameOverTimer.h"

using namespace std;

static GameOverTimer *s_instance = NULL;

GameOverTimer::GameOverTimer(std::string name) : GameTimer(name, GAME_OVER_COUNTDOWN_TICKS)
{
}

GameTimer* GameOverTimer::getInstance()
{
  if(s_instance == NULL)
  {
    s_instance = new GameOverTimer("GameOverTimer");
  }
  return s_instance;
}

GameOverTimer::~GameOverTimer()
{
  if(s_instance != NULL)
  {
    delete s_instance;
    s_instance = NULL;
  }
}
