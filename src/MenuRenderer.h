/*
 * MenuRenderer.h
 *
 *  Created on: Apr 2, 2022
 *      Author: msaun
 */

#ifndef MICKRENDERER_H_
#define MICKRENDERER_H_

#include <SDL2/SDL_render.h>
#include <stdexcept>
#include <string>
#include "RuntimeException.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>

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

class MenuRenderer
{
  public:
    MenuRenderer(SDL_Renderer *renderer, SDL_Window *screen);
    bool showMenu();
    virtual ~MenuRenderer();

  private:
    void init(SDL_Renderer* renderer, SDL_Window *screen);
    int getTabIndex(Rml::Element* node);
    Rml::Element* getChildElementWithTabIndex(Rml::Element* parentNode, int tabIndex);
    Rml::Context* m_context = nullptr;
    SDL_Renderer *m_renderer;
};

#endif /* MICKRENDERER_H_ */
