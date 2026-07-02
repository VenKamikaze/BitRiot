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
      // Tagged control assignment: CPU has no value; KEYBOARD's value is a
      // 0-based keyboard-set index; CONTROLLER's value is an SDL instance id.
      enum class ControlChoiceType { CPU, KEYBOARD, CONTROLLER };

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
      // Re-read map dims + recompute the start gate (canStart / message) from the
      // canonical GameSettings, then dirty the bound variables. Call when the
      // player-select page is (re)shown and when the player count changes, so the
      // displayed dims and the gate reflect any Map Height change made in Options.
      static void refreshMapConfig();

      // Single write path for player<->device assignment. Clears whichever ids
      // don't apply to the chosen type, flips the human/AI flag, updates the
      // controller strip images (old pad -> join, new pad -> ready), and
      // refreshes deviceLabel + the map/start gate. Always ends by dirtying
      // "players" and "controllers".
      static Player* assignPlayerControl(int playerIndex, ControlChoiceType type, int value = -1);
      // True if some player other than exceptPlayerId already holds this
      // keyboard set / controller instance id.
      static bool isKeyboardSetTaken(int setId, int exceptPlayerId);
      static bool isControllerTaken(int controllerId, int exceptPlayerId);
      // Rewrites every player's deviceLabel from their current human/id fields.
      static void refreshDeviceLabels();
      // Idempotent pass over the canonical model: auto-attaches the primary pad
      // to slot 0 (Bug 1 fix) when it's human/device-less and the pad is free,
      // gives any other device-less human the first free keyboard set (slots
      // 0-3) or leaves them CPU (slots 4-5), and drops stored controller ids
      // for devices no longer present in controllersData. Call on page (re)load.
      static void normalizeAssignments();
    private:
      bool setupDataBinding(Rml::Context* context);
      std::string m_modelName = "playersSelect";
      Rml::DataModelHandle m_dataModelHandle = nullptr;
  };
}; /* namespace std */

#endif /* PLAYERSSELECTBINDER_H_ */
