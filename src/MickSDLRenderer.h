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
    // Recreates the back-buffer surface + primary texture at a new content size and
    // resizes the window. Used when the map dimensions change pre-game. Keeps the
    // window + renderer (and the preserve-aspect flag) intact so RmlUi is undisturbed.
    // Callers MUST re-fetch getSurfaceBackBufferHandle() afterwards (the old pointer
    // is freed).
    void resize(int contentWidth, int contentHeight);
    static MickSDLRenderer* getInstance();
    // nullptr if the renderer was never created (its constructor threw): lets the
    // caller report the failure and shut down instead of tripping getInstance()'s assert.
    static MickSDLRenderer* getInstanceIfCreated();

    // When true, the game view is letterboxed to preserve its native aspect ratio
    // in pushCpuBufferToHardwareBuffer; when false it stretches to fill the window.
    // Applied only to the game blit, NOT the RmlUi menu (which renders separately),
    // so it cannot use SDL_RenderSetLogicalSize (that would transform the menu too).
    void setPreserveAspect(bool preserve) { m_preserveAspect = preserve; }
    bool isPreserveAspect() { return m_preserveAspect; }

  protected:
    void destructVideo();

    bool m_preserveAspect = false;

};

#endif /* MICKSDLRENDERER_H_ */
