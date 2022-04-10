/*
 * MenuRenderer.h
 *
 *  Created on: Apr 2, 2022
 *      Author: msaun
 */

#ifndef MICKRENDERER_H_
#define MICKRENDERER_H_

#include <RmlUi/Core/ElementDocument.h>
#include <SDL2/SDL_render.h>
#include <stdexcept>
#include <string>
#include "RuntimeException.h"

#include <SDL2/SDL.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger/Debugger.h>

#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include "RmlUI/glue/RenderInterfaceSDL2.h"
#include "RmlUI/glue/ShellFileInterface.h"

#include "RmlUI/MickRmlUIElementUtil.h"

#include "GameSettings.h"
#include "RmlUI/events/EventInstancer.h"
#include "RmlUI/events/EventManager.h"
#include "RmlUI/events/EventHandlerOptions.h"
#include "RmlUI/ScoreBoardBinder.h"
#include <iostream>
#include "MickLogger.h"

class MenuRenderer
{
  public:
    MenuRenderer(SDL_Renderer *renderer, SDL_Window *screen);
    void loadScoreBoard(PlayerCharacterEntity *winner, int gameTotalLength);
    void clearScoreBoard();
    bool showMenu();
    virtual ~MenuRenderer();

  private:
    void init(SDL_Renderer* renderer, SDL_Window *screen);
    int getTabIndex(Rml::Element* node);
    Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    Rml::Context* m_context;
    void loadMenu(std::string menuRmlFile);
    SDL_Window *m_screen;
    SDL_Renderer *m_renderer;
    ScoreBoardBinder *m_scoreBinder = nullptr;
};

#endif /* MICKRENDERER_H_ */
