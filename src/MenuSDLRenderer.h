/*
 * MenuRenderer.h
 *
 *  Created on: Apr 2, 2022
 *      Author: msaun
 */

#ifndef MENUSDLRENDERER_H_
#define MENUSDLRENDERER_H_

#include <RmlUi/Core/ElementDocument.h>
#include <SDL2/SDL_render.h>
#include "RmlUI/binders/PlayersBinder.h"
#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "RuntimeException.h"

#include <SDL2/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
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
#include "RmlUI/binders/ScoreBoardBinder.h"
#include <iostream>
#include <stdexcept>
#include <string>


class MenuSDLRenderer
{
  public:
    MenuSDLRenderer(SDL_Renderer *renderer, SDL_Window *screen);
    void loadScoreBoard(shared_ptr<PlayerCharacterEntity> winner, int gameTotalLength);
    void clearScoreBoard();
    bool showMenu();
    void menuBackButtonHit();
    virtual ~MenuSDLRenderer();

  private:
    void handleWindowResize();
    void init(SDL_Renderer* renderer, SDL_Window *screen);
    int getTabIndex(Rml::Element* node);
    Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    Rml::Context* m_context;
    Rml::ElementDocument* loadMenu(std::string menuRmlFile, bool loadAndShow = true);
    SDL_Window *m_screen;
    SDL_Renderer *m_renderer;
    ScoreBoardBinder *m_scoreBinder = nullptr;
    unique_ptr<PlayersBinder> m_playersBinder = nullptr;
};

#endif /* MENUSDLRENDERER_H_ */
