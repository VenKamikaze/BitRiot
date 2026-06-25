/*
 * MickRmlUIElementUtil.h
 *
 *  Created on: 25 May 2017
 *      Author: msaun
 */

#ifndef PLAYERSSELECTBINDER_H_
#define PLAYERSSELECTBINDER_H_

#include <string>
#include "MickLogger.h"
#include "PlayerSetupStruct.h"
#include "SDL_events.h"

#include "GameSettings.h"
#include "MickAssets.h"
#include "MickLogger.h"
#include <cassert>
#include <cstddef>
#include "RmlUI/MickRmlUIElementUtil.h"

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>

#include "SDL_gamecontroller.h"
#include "SDL_joystick.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <sstream>
#include <string>
#include "MickSDLUtil.h"

namespace std
{
  class PlayersSelectBinder
  {
    public:
      PlayersSelectBinder(Rml::Context* rmlContext);
      static void mapPlayersData();
      static void mapControllersData();
      void removeDataBinding(Rml::Context *context);
      static void controllerDeviceAttached(int controllerId);
      static void controllerDeviceRemoved(int controllerId);
      static Player* getPlayerAttachedToController(int controllerId);
      static Player* attachPlayerToController(int playerIndex, int controllerId);
      static Player* attachPlayerToKeyboard(int keyboardSetId); // bind keyboard set N to player slot N (keyboard-join); returns the player or nullptr if the slot is taken / out of range
      static void detachPlayerFromController(int controllerId);
    private:
      bool setupDataBinding(Rml::Context* context);
      std::string m_modelName = "playersSelect";
      Rml::DataModelHandle m_dataModelHandle = nullptr;
  };
}; /* namespace std */

#endif /* PLAYERSSELECTBINDER_H_ */
