/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// Note: This class no longer resembles the sample file from libRocket at all, sans the ProcessEvent method definition.
//       I've kept the MIT license as part of this anyway.

#include "EventHandlerPlayerSelect.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/binders/PlayersSelectBinder.h"

using namespace std;

const string EventHandlerPlayerSelect::RML_FILENAME = "playerselect.rml";
static const int NAVIGATION_MIN_ROW_EXTRA_CONTROLLER = 2;
static const int NAVIGATION_MAX_ROW_EXTRA_CONTROLLER = 3;

EventHandlerPlayerSelect::EventHandlerPlayerSelect()
{
  primaryControllerId = GameRunningSDLInput::getPrimaryJoystickID();
}

EventHandlerPlayerSelect::~EventHandlerPlayerSelect()
{

}



void EventHandlerPlayerSelect::playerSelectPageInit(Rml::ElementDocument* bodyElement)
{
  if (! bodyElement)
    return;
}

void EventHandlerPlayerSelect::ProcessEvent(Rml::Event& event, const Rml::String& value)
{
		// Check for a generic 'load' or 'exit' command.
		Rml::StringList values;
		Rml::StringUtilities::ExpandString(values, value, ' ');

		if (values.empty())
			return;

		// Fired by <body onload="psinit">: re-read map dims + recompute the start
		// gate so a Map Height change made in Options is reflected on return here.
		if (values[0] == "psinit")
		{
			PlayersSelectBinder::refreshMapConfig();
			// Auto-attach the primary pad to slot 0 (Bug 1), seed device-less
			// humans a free keyboard set, and drop stale controller ids - then
			// show whatever that seeded so a post-game revisit reflects it.
			PlayersSelectBinder::normalizeAssignments();
			PlayersSelectBinder::refreshDeviceLabels();
		}
}

void EventHandlerPlayerSelect::controllerJoin(SDL_JoystickID controllerId)
{
	if(PlayersSelectBinder::getPlayerAttachedToController(controllerId) != nullptr)
	{
		// This pad already owns a slot (e.g. via normalizeAssignments' Bug-1
		// auto-attach, or a previous join) - nothing to do.
		MickLogger::getInstance()->debug(nullptr, string("EHPS.cJ: controller ").append(to_string(controllerId)).append(" already owns a slot; ignoring join"));
		return;
	}

	Rml::ElementList eList;
	Rml::Context *context = Rml::GetContext("default");
	context->GetFocusElement()->GetOwnerDocument()->GetElementsByTagName(eList, "article");
	for(auto &el : eList)
	{
    if(el->HasAttribute("playerId"))
		{
			MickLogger::getInstance()->debug(nullptr, string("EHPS.cJ: el has attribute playerId: ").append(el->GetId()));
			if(MickRmlUIElementUtil::getChildElementWithClassName(el, MickRmlUIElementUtil::ANY_PLAYER_HIGHLIGHTED_CLASS) == nullptr
			    && MickRmlUIElementUtil::getChildElementWithClassName(el, MickRmlUIElementUtil::ANY_PLAYER_SELECTED_CLASS) == nullptr)
			{
				MickLogger::getInstance()->debug(nullptr, string("EHPS.cJ: el looks free: ").append(el->GetId()));
				// this article column looks free. setup a new playerhighlight here.
				Rml::Element *joinElement = MickRmlUIElementUtil::getChildElementWithClassName(el, MickRmlUIElementUtil::JOIN_PLAYER_CLASS);
				if(joinElement)
				{
					MickLogger::getInstance()->debug(nullptr, string("EHPS.cJ: focussing el: ").append(joinElement->GetId()));
					MickRmlUIElementUtil::setFocusElementForController(joinElement, controllerId, true);
				  joinElement->Click(); // set to Human
					MickRmlUIElementUtil::setPlayerChosenForControllerId(joinElement, controllerId);
					// The article column's playerId attribute is already the 0-based
					// canonical player index that attachPlayerToController expects, and
					// the free-column scan above skips the default-human slot 0 (it
					// carries the "selected" class). The previous "+ 1" therefore bound
					// one slot past the column found and threw .at(4) on the last column.
					// NOTE: controller path - static-analysis fix, needs a gamepad pass.
					PlayersSelectBinder::attachPlayerToController(atoi(el->GetAttribute("playerId")->Get<Rml::String>().c_str()), controllerId);
					break;
				}
			}
		} 
	}
}

void EventHandlerPlayerSelect::processControllerButtonDown(int controllerId, Uint8 button)
{
  RmlUiSDL2SystemInterface *systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();
	Rml::Context *context = Rml::GetContext("default");

  if(primaryControllerId == controllerId)
	{
		switch(button)
		{
			case SDL_CONTROLLER_BUTTON_A:
			{
				MickLogger::getInstance()->debug(this, "Primary controller, got A.");
				context->ProcessKeyDown(systemInterface->TranslateKey(SDLK_RETURN), systemInterface->GetKeyModifiers());
				break;
			}
			case SDL_CONTROLLER_BUTTON_B:
			{
				MickLogger::getInstance()->debug(this, "Primary controller, got B.");
				MickRmlUIElementUtil::menuBackButtonTriggered(context);
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			{
				MickLogger::getInstance()->debug(this, "Primary controller, got dpad down.");
				MickRmlUIElementUtil::focusNextElement(context);
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			{
        MickLogger::getInstance()->debug(this, "Primary controller, got dpad up.");
        MickRmlUIElementUtil::focusPrevElement(context);
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element)
        {
					// mismatch here - highlighting will be different for left+right than normal focus on up/down. decide which one to do
					MickRmlUIElementUtil::navigateLeftForController(element, controllerId);
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element)
        {
					// mismatch here - highlighting will be different for left+right than normal focus on up/down. decide which one to do
					MickRmlUIElementUtil::navigateRightForController(element, controllerId);
				}
				break;
			}
		}
	}
  else
	{
    switch(button)
		{
			case SDL_CONTROLLER_BUTTON_A:
			{
				MickLogger::getInstance()->debug(this, "non-primary controller, got A.");
        Rml::Element *element = MickRmlUIElementUtil::getElementHighlightedByControllerId(context, controllerId);
				if (element)
				{
					MickLogger::getInstance()->debug(this, string("non-primary controller, element click with ID:").append(element->GetId()));
					element->Click();
				}
				else
				{
					MickLogger::getInstance()->debug(this, string("non-primary controller join event for ID:").append(to_string(controllerId)));
					// new controller join
					controllerJoin(controllerId);
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_B:
			{
        MickLogger::getInstance()->debug(this, "non-primary controller, got B.");
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element)
        {
          MickRmlUIElementUtil::cancelPlayerChosenForControllerId(element, controllerId);
        }
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element && element->HasAttribute("row") && element->HasAttribute("col"))
        {
					MickLogger::getInstance()->debug(this, string("non-primary controller, current element focussed is: ").append(element->GetId()));
					int currentTabIndex = MickRmlUIElementUtil::getTabIndex(element);
					int currentCol = MickRmlUIElementUtil::getAttributeValueAsInt(element, "col");
					Rml::Element *elementDown = MickRmlUIElementUtil::getElementWithTabIndex(element->GetOwnerDocument(), ++currentTabIndex);
					MickLogger::getInstance()->debug(this, string("non-primary controller, element down focus will be: ").append(elementDown->GetId()));
					int elementDownRow = MickRmlUIElementUtil::getAttributeValueAsInt(elementDown, "row");
					int elementDownCol = MickRmlUIElementUtil::getAttributeValueAsInt(elementDown, "col");

					if(currentCol == elementDownCol && NAVIGATION_MIN_ROW_EXTRA_CONTROLLER <= elementDownRow && elementDownRow <= NAVIGATION_MAX_ROW_EXTRA_CONTROLLER)
					{
						MickRmlUIElementUtil::setFocusElementForController(element, controllerId, false);
						MickRmlUIElementUtil::setFocusElementForController(elementDown, controllerId, true);
						MickLogger::getInstance()->debug(this, string("non-primary controller, focussed new element").c_str());
					}
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element && element->HasAttribute("row") && element->HasAttribute("col"))
        {
					MickLogger::getInstance()->debug(this, string("non-primary controller, current element focussed is: ").append(element->GetId()));
					int currentTabIndex = MickRmlUIElementUtil::getTabIndex(element);
					int currentCol = MickRmlUIElementUtil::getAttributeValueAsInt(element, "col");
					Rml::Element *elementUp = MickRmlUIElementUtil::getElementWithTabIndex(element->GetOwnerDocument(), --currentTabIndex);
					MickLogger::getInstance()->debug(this, string("non-primary controller, element up focus will be: ").append(elementUp->GetId()));
					int elementUpRow = MickRmlUIElementUtil::getAttributeValueAsInt(elementUp, "row");
					int elementUpCol = MickRmlUIElementUtil::getAttributeValueAsInt(elementUp, "col");

					if(currentCol == elementUpCol && NAVIGATION_MIN_ROW_EXTRA_CONTROLLER <= elementUpRow && elementUpRow <= NAVIGATION_MAX_ROW_EXTRA_CONTROLLER)
					{
						MickRmlUIElementUtil::setFocusElementForController(element, controllerId, false);
						MickRmlUIElementUtil::setFocusElementForController(elementUp, controllerId, true);
					}
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element)
        {
					MickRmlUIElementUtil::navigateLeftForController(element, controllerId);
				}
				break;
			}
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
			{
				Rml::Element *element = MickRmlUIElementUtil::getElementChosenByControllerId(context->GetFocusElement()->GetOwnerDocument(), controllerId);
        if (element)
        {
					MickRmlUIElementUtil::navigateRightForController(element, controllerId);
				}
				break;
			}
		}
	}
}

bool EventHandlerPlayerSelect::processSDLEvent(SDL_Event& event)
{
  RmlUiSDL2SystemInterface *systemInterface = (RmlUiSDL2SystemInterface*) Rml::GetSystemInterface();
	Rml::Context *context = Rml::GetContext("default");

  bool continueRunning = true;
  // copy+paste TODO fix this to be specific for player select and handling multiple controllers/keyboard players.
	switch(event.type)
	{
		case SDL_QUIT:
		{
			MickLogger::getInstance()->debug(this, "SDL_QUIT");
			GameSettings::getInstance()->setGameState(GameSettings::GAME_QUIT);
			continueRunning = true;
			break;
		}

		case SDL_MOUSEMOTION:
		{
			context->ProcessMouseMove(event.motion.x, event.motion.y, systemInterface->GetKeyModifiers());
			break;
		}

		case SDL_MOUSEBUTTONDOWN:
		{
			context->ProcessMouseButtonDown(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
			break;
		}

		case SDL_MOUSEBUTTONUP:
		{
      context->ProcessMouseButtonUp(systemInterface->TranslateMouseButton(event.button.button), systemInterface->GetKeyModifiers());
      break;
		}

		case SDL_MOUSEWHEEL:
		{
			context->ProcessMouseWheel(event.wheel.y, systemInterface->GetKeyModifiers());
			break;
		}

		case SDL_KEYDOWN:
		{
				MickLogger::getInstance()->debug(this, std::string("SDL_KEYDOWN, sym:").append(std::to_string(event.key.keysym.sym)));
				if(event.key.keysym.sym == SDLK_ESCAPE)
				{
					MickRmlUIElementUtil::menuBackButtonTriggered(context);
					break;
				}
				else if(event.key.keysym.sym == SDLK_DOWN
								|| event.key.keysym.sym == SDLK_UP)
				{
					event.key.keysym.sym == SDLK_DOWN ? MickRmlUIElementUtil::focusNextElement(context) : MickRmlUIElementUtil::focusPrevElement(context);
					break;
				}
				else
				{
					// Phase 2.5 keyboard-join: an action key from one of the fixed
					// keyboard sets (e.g. LCTRL/RCTRL/KP_PLUS/[) binds that set to its
					// player slot and flips the slot to Human (mirrors controllerJoin).
					// The set's action keys are not menu-nav keys, so non-matches fall
					// through to ProcessKeyDown unchanged.
					int keyboardSet = GameRunningSDLInput::getInstance()->getKeyboardSetForActionKey(event.key.keysym.sym);
					if(keyboardSet > -1)
					{
						MickLogger::getInstance()->debug(this, std::string("EHPS keyboard-join for set:").append(std::to_string(keyboardSet)));
						PlayersSelectBinder::attachPlayerToKeyboard(keyboardSet);
						break;
					}
				}
				context->ProcessKeyDown(systemInterface->TranslateKey(event.key.keysym.sym), systemInterface->GetKeyModifiers());
				break;
		}

		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
		{
			//event.jbutton.which, event.jbutton.button, event.jbutton.state
			if(event.jbutton.state == SDL_PRESSED)
		  {
				MickLogger::getInstance()->debug(this, string("EHPS event.jbutton.which: ").append(to_string(event.jbutton.which)));
				processControllerButtonDown(event.jbutton.which, event.jbutton.button);
				
		  }
			break;
		}

		case SDL_CONTROLLERDEVICEADDED: // event.jbutton.which is a device_index in this case
		{
			MickLogger::getInstance()->debug(this, string("EHPS Opening game controller: ").append(to_string(event.jbutton.which)));
			if (SDL_IsGameController(event.jbutton.which))
			{
				if(SDL_GameControllerOpen(event.jbutton.which))
				{
					MickLogger::getInstance()->debug(this, string("EHPS Opened game controller: ").append(to_string(event.jbutton.which)));
					PlayersSelectBinder::controllerDeviceAttached(event.jbutton.which);
					if(primaryControllerId == -1)
					{
						primaryControllerId = MickSDLUtil::getPrimaryJoystickID();
					}
				}
			}
			break;
		}
		case SDL_CONTROLLERDEVICEREMOVED: // event.jbutton.which is an instance ID in this case
		{
			MickLogger::getInstance()->debug(this, string("EHPS Gamecontroller removed: ").append(to_string(event.jbutton.which)));
			PlayersSelectBinder::controllerDeviceRemoved(event.jbutton.which);
			primaryControllerId = MickSDLUtil::getPrimaryJoystickID();
			break;
		}

		default:
			break;
	}
	return continueRunning;
}
