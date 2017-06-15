/*
 * GameOverTimer.cpp
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#include "GameScoreBoardTimer.h"

using namespace std;

static GameScoreBoardTimer *s_instance = NULL;

GameScoreBoardTimer::GameScoreBoardTimer(std::string name, int* seconds) : GameTimer(name, seconds)
{
}

GameTimer* GameScoreBoardTimer::getInstance()
{
  if(s_instance == NULL)
  {
    s_instance = new GameScoreBoardTimer("GameScoreBoardTimer", NULL);
  }
  return s_instance;
}

GameTimer* GameScoreBoardTimer::getInstance(std::string name, int* seconds)
{
  if(s_instance == NULL)
  {
    s_instance = new GameScoreBoardTimer(name, seconds);
  }
  return s_instance;
}

GameScoreBoardTimer::~GameScoreBoardTimer()
{
  if(s_instance != NULL)
  {
    delete s_instance;
    s_instance = NULL;
  }
}
