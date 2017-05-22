/*
 * MenuRenderer.h
 *
 *  Created on: Nov 18, 2016
 *      Author: msaun
 */

#ifndef MICKRENDERER_H_
#define MICKRENDERER_H_

#include <string>
#include "RuntimeException.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>

#include <Rocket/Core.h>
#include <Rocket/Core/Input.h>
#include <Rocket/Debugger/Debugger.h>

#include "rocket/glue/SystemInterfaceSDL2.h"
#include "rocket/glue/RenderInterfaceSDL2.h"
#include "rocket/glue/ShellFileInterface.h"


class MenuRenderer
{
  public:
    MenuRenderer(SDL_Renderer *renderer, SDL_Window *screen);
    bool showMenu();
    virtual ~MenuRenderer();

  private:
    void init(SDL_Renderer* renderer, SDL_Window *screen);

    Rocket::Core::Context* m_context = NULL;

};

#endif /* MICKRENDERER_H_ */
