/*
 * GameOverTimer.cpp
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#include "GameOverTimer.h"

using namespace std;

GameTimer::GameTimer(std::string name, int* seconds)
{
  if(seconds != NULL)
  {
    this->seconds = *seconds;
  }
  else
  {
    this->seconds = getDefaultTimeout();
  }
  this->name = name;
}

