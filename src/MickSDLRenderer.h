/*
 * MickSDLRenderer.h
 *
 *  Created on: Apr 04, 2022
 *      Author: msaun
 */

#ifndef MICKSDLRENDERER_H_
#define MICKSDLRENDERER_H_

#include <SDL2/SDL.h>

#include "MickBaseRenderer.h"

#include "MickLogger.h"
#include "SDL_render.h"
#include <stdexcept>


//class MickSDLSound: public std::MickBaseSound
class MickSDLRenderer : MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture>
{
  public:
    MickSDLRenderer();
    MickSDLRenderer(WindowMetadata windowMetadata);
    virtual ~MickSDLRenderer();
    void init(WindowMetadata windowMetadata);
    static MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture>* getInstance();
    static MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture>* getInstance(WindowMetadata windowMetadata); // windowMetadata is ignored unless instance is being created

  protected:
    void destructVideo();

};

#endif /* MICKSDLRENDERER_H_ */
