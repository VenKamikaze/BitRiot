/*
 * GameSettings.cpp
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#include "GameSettings.h"

using namespace std;

static GameSettings *s_instance = nullptr;

GameSettings::GameSettings()
{
  genders.reserve(numPlayers+1);
  playerAIs.reserve(numPlayers);
  for(int i = 0; i < numPlayers; i++)
  {
    if(i == 2 || i == 3 || i == 5)
    {
      genders.push_back(false);
    }
    genders.push_back(true);
    playerAIs.push_back(i == 0 ? false : true); // default to one human player
  }
  genders.push_back(true); // additional gender for purple (non character player).
}

GameSettings* GameSettings::getInstance()
{
  if(s_instance == nullptr)
  {
    s_instance = new GameSettings();
  }
  return s_instance;
}

GameSettings::~GameSettings()
{
  if(s_instance != nullptr)
  {
    delete s_instance;
    s_instance = nullptr;
  }
}
