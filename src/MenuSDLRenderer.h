/*
 * MenuRenderer.h
 *
 *  Created on: Apr 2, 2022
 *      Author: msaun
 */

#ifndef MENUSDLRENDERER_H_
#define MENUSDLRENDERER_H_

#include "InputHandler.h"
#include <RmlUi/Core/ElementDocument.h>
#include <SDL2/SDL_render.h>

#include "RmlUI/binders/PlayersSelectBinder.h"
#include "SDL_keycode.h"
#include "SDL_joystick.h"
#include "SDL_pixels.h"
#include "RuntimeException.h"

#include "SDL_gamecontroller.h"
#include "SDL_keycode.h"
#include <SDL2/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <memory>
#ifndef NDEBUG
#include <RmlUi/Debugger/Debugger.h>
#endif

#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include "RmlUI/glue/RenderInterfaceSDL2.h"
#include "RmlUI/glue/ShellFileInterface.h"


#include "MickLogger.h"
#include "PlayerCharacterEntity.h"
#include "GameSettings.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/events/EventInstancer.h"
#include "RmlUI/events/EventManager.h"
#include "RmlUI/events/EventHandlerOptions.h"
#include "RmlUI/events/EventHandlerPlayerSelect.h"
#include "RmlUI/binders/ScoreBoardBinder.h"
#include <iostream>
#include <stdexcept>
#include <string>

struct ControllerPlayerSelect {
  Rml::Element *focussedElement;
  
  bool isReady = false;
  int controllerId = -1; // will be >= 0 if controller input (action1) is detected.
  bool isKeyboardId = -1; // will be >= 0 if keyboard input (action1) is detected.
};


class MenuSDLRenderer
{
  public:
    MenuSDLRenderer(SDL_Renderer *renderer, SDL_Window *screen, shared_ptr<InputHandler>);
    void loadScoreBoard(shared_ptr<PlayerCharacterEntity> winner, int gameTotalLength);
    void clearScoreBoard();
    bool showMenu();
    virtual ~MenuSDLRenderer();

  private:
    void handleWindowResize();
    void initGui(SDL_Renderer* renderer, SDL_Window *screen);
    int playerJoin();
    int getTabIndex(Rml::Element* node);
    Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    Rml::Context* context;
    Rml::ElementDocument* loadMenu(std::string menuRmlFile, bool loadAndShow = true);
    SDL_Window *m_screen;
    SDL_Renderer *m_renderer;
    ScoreBoardBinder *m_scoreBinder = nullptr;
    unique_ptr<PlayersSelectBinder> m_playersBinder = nullptr;
    shared_ptr<InputHandler> m_inputHandler = nullptr;
    std::map<int, ControllerPlayerSelect> playerIdToFocussedElement;
    shared_ptr<EventHandlerPlayerSelect> m_eHandlerPlayerSelect;
};

#endif /* MENUSDLRENDERER_H_ */
