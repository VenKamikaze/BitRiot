

#ifndef _PLAYERSETUPSTRUCT_H_
#define _PLAYERSETUPSTRUCT_H_

#include <string>
#include "MickAssets.h"

struct Player {
  int id;
  int team;
  std::string portrait;
  std::MickAssets::PlayerType type;
  int human = 0; // 1 for true
  int attachedControllerId = -1;
  int attachedKeyboardId = -1; // index for m_keyMap
  std::string deviceLabel = "CPU"; // display-only; kept in sync by PlayersSelectBinder::refreshDeviceLabels()
};

#endif //_PLAYERSETUPSTRUCT_H_