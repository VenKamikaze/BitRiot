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
class MickSDLRenderer : public MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture>
{
  public:
    MickSDLRenderer(WindowMetadata windowMetadata);
    virtual ~MickSDLRenderer();
    void init(WindowMetadata windowMetadata);
    void pushCpuBufferToHardwareBuffer(); // copies the SDL_Surface to the SDL_Texture and then renders.
    static MickSDLRenderer* getInstance();
    
  protected:
    void destructVideo();

};

#endif /* MICKSDLRENDERER_H_ */
