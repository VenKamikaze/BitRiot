/*
 * MickSDLRenderer.cpp
 *
 *  Created on: Apr 04, 2022
 *      Author: msaun
 */

#include "MickSDLRenderer.h"
#include "MickLogger.h"
#include "SDL.h"
#include "SDL_render.h"
#include "SDL_video.h"


using namespace std;

static MickSDLRenderer *sInstance = nullptr;


MickSDLRenderer::MickSDLRenderer(WindowMetadata windowMetadata) : MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture>()
{
  assert(sInstance == nullptr);
  init(windowMetadata);
  MickLogger::getInstance()->debug(this, "Created and initialised instance of MickSDLRenderer.");
  sInstance = this;
}

void MickSDLRenderer::init(WindowMetadata windowMetadata)
{
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0 )
  {
    std::string msg = "Failed to SDL_InitSubSystem video!";
    MickLogger::getInstance()->error(this, msg);
    throw new runtime_error(msg);
  }

  m_windowHandle = SDL_CreateWindow(windowMetadata.windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                windowMetadata.windowWidth, windowMetadata.windowHeight, (windowMetadata.initFlags & ASK_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE );
  if (m_windowHandle == nullptr)
  {
    std::string msg = "Failed to create sdl_window!";
    MickLogger::getInstance()->error(this, msg);
    throw new runtime_error(msg);
  }

  m_rendererHandle = SDL_CreateRenderer(m_windowHandle, -1, 0);
  if (m_rendererHandle == nullptr)
  {
    std::string msg = "Failed to create sdl_renderer!";
    MickLogger::getInstance()->error(this, msg);
    throw new runtime_error(msg);
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  //SDL_RenderSetLogicalSize(m_rendererHandle, windowMetadata.windowWidth, windowMetadata.windowHeight);

  Uint32 pixel_format = SDL_GetWindowPixelFormat(m_windowHandle);

  m_textureHandle = SDL_CreateTexture(m_rendererHandle, pixel_format, SDL_TEXTUREACCESS_STREAMING,
                                          windowMetadata.windowWidth, windowMetadata.windowHeight);
  if (m_textureHandle == nullptr)
  {
    std::string msg = "Failed to create primary SDL texture!";
    MickLogger::getInstance()->error(this, msg);
    throw new runtime_error(msg);
  }

  int depth = 0;
  Uint32 rmask = 0;
  Uint32 gmask = 0;
  Uint32 bmask = 0;
  Uint32 amask = 0;
  SDL_PixelFormatEnumToMasks(pixel_format, &depth, &rmask, &gmask, &bmask, &amask);

  m_surfaceBackBufferHandle = SDL_CreateRGBSurface(0, windowMetadata.windowWidth, windowMetadata.windowHeight, windowMetadata.windowBpp,
                                     rmask, gmask, bmask, amask);

  if (m_surfaceBackBufferHandle == nullptr)
  {
    std::string msg = "Failed to create primary SDL surface!";
    MickLogger::getInstance()->error(this, msg);
    throw new runtime_error(msg);
  }
  // update blitting RECT
  SDL_Rect clientArea;
  clientArea.x = 0;
  clientArea.y = 0;
  clientArea.w = windowMetadata.windowWidth -1 ;
  clientArea.h = windowMetadata.windowHeight - 1;

  SDL_SetClipRect(m_surfaceBackBufferHandle, &clientArea);

  // seed random number generator
  srand(SDL_GetTicks());

  initialised = true;
}

void MickSDLRenderer::pushCpuBufferToHardwareBuffer()
{
  SDL_UpdateTexture(getPrimaryTextureHandle(), nullptr, getSurfaceBackBufferHandle()->pixels, getSurfaceBackBufferHandle()->pitch);
  SDL_RenderClear(getRendererHandle());
  SDL_RenderCopy(getRendererHandle(), getPrimaryTextureHandle(), nullptr, nullptr);
}

void MickSDLRenderer::destructVideo()
{
  if(initialised && SDL_WasInit(SDL_INIT_VIDEO))
  {
    if(m_surfaceBackBufferHandle != nullptr)
    {
      SDL_FreeSurface(m_surfaceBackBufferHandle);
      m_surfaceBackBufferHandle = nullptr;
    }

    if(m_rendererHandle != nullptr)
    {
      SDL_DestroyRenderer(m_rendererHandle); // also frees m_textureHandle
      m_textureHandle = nullptr;
      m_rendererHandle = nullptr;
    }

    if(m_windowHandle != nullptr)
    {
      SDL_DestroyWindow(m_windowHandle);
      m_windowHandle = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
}


MickSDLRenderer::~MickSDLRenderer()
{
  destructVideo();
}

MickSDLRenderer* MickSDLRenderer::getInstance()
{
  assert(sInstance);
  return sInstance;
}
