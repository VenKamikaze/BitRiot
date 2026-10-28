/*
 * GameSettings.cpp
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#include "GameSettings.h"
#include "SimRandom.h"
#include <set>

using namespace std;

static GameSettings *s_instance = nullptr;

// Builds a default Player record for the given slot, using the same
// conventions as the player-select menu (player 0 human, rest AI; alternating
// male/female) so the canonical model and the menu agree before any toggle.
static Player makeDefaultPlayer(int i)
{
  Player p;
  p.id = i;
  p.team = i;
  p.human = (i == 0) ? 1 : 0;
  p.type = (i % 2 == 0) ? MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE;
  p.portrait = MickAssets::getPortraitBitmapFilename(i, p.type);
  return p;
}

GameSettings::GameSettings()
{
  m_players.reserve(numPlayers);
  for(int i = 0; i < numPlayers; i++)
  {
    m_players.push_back(makeDefaultPlayer(i));
  }
  syncMirrors();
}

// Re-derive the genders/playerAIs mirror vectors from the canonical m_players.
// Consumers index both by 0-based player index; genders keeps the trailing
// "+1" purple (non-character) slot that setNumberOfPlayers historically sized.
void GameSettings::syncMirrors()
{
  playerAIs.resize(numPlayers);
  genders.resize(numPlayers + 1);
  for(int i = 0; i < numPlayers; i++)
  {
    playerAIs[i] = (m_players.at(i).human == 0);
    genders[i] = (m_players.at(i).type == MickAssets::PlayerType::MALE);
  }
  genders[numPlayers] = true; // purple non-character player slot
}

void GameSettings::setNumberOfPlayers(int allPlayers)
{
  if(MIN_PLAYERS <= allPlayers && allPlayers <= MAX_PLAYERS)
  {
    numPlayers = allPlayers;
    // Grow by appending defaults, preserving existing entries (and any device
    // assignments stored on them); shrink by truncating the extras.
    for(int i = (int) m_players.size(); i < allPlayers; i++)
    {
      m_players.push_back(makeDefaultPlayer(i));
    }
    if((int) m_players.size() > allPlayers)
    {
      m_players.resize(allPlayers);
    }
    // A team number must always be <= the player count. If the count dropped,
    // reset any now-invalid team back to that player's own slot (free-for-all).
    for(int i = 0; i < allPlayers; i++)
    {
      if(m_players.at(i).team >= allPlayers)
      {
        m_players.at(i).team = m_players.at(i).id;
      }
    }
    syncMirrors();
  }
}

int GameSettings::numConfiguredTeams()
{
  std::set<int> teams;
  for(int i = 0; i < numPlayers; i++)
  {
    teams.insert(m_players.at(i).team);
  }
  return (int) teams.size();
}

uint64_t GameSettings::beginRoundSeed()
{
  if (!roundSeedPinned)
  {
    roundSeed = SimRandom::freshSeed();
  }
  roundSeedPinned = false; // a pin lasts for one round
  SimRandom::sim().seed(roundSeed);
  return roundSeed;
}

void GameSettings::setPlayerGender(int playerIndex, bool isMale)
{
  m_players.at(playerIndex).type = isMale ? MickAssets::PlayerType::MALE : MickAssets::PlayerType::FEMALE;
  syncMirrors();
}

void GameSettings::setPlayerAI(int playerIndex, bool isAI)
{
  MickLogger::getInstance()->debug(this, string("setPlayerAI(").append(to_string(playerIndex)).append(",").append(to_string(isAI)).append(")"));
  m_players.at(playerIndex).human = isAI ? 0 : 1;
  syncMirrors();
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
