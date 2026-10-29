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
#include <algorithm>


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
    throw runtime_error(msg);
  }

  const Uint32 windowFlags = (windowMetadata.initFlags & ASK_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
  m_windowHandle = SDL_CreateWindow(windowMetadata.windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                windowMetadata.windowWidth, windowMetadata.windowHeight, windowFlags);
  if (m_windowHandle == nullptr)
  {
    std::string msg = "Failed to create sdl_window!";
    MickLogger::getInstance()->error(this, msg);
    throw runtime_error(msg);
  }

  m_rendererHandle = SDL_CreateRenderer(m_windowHandle, -1, 0);
  if (m_rendererHandle == nullptr)
  {
    // No accelerated renderer (in a browser: WebGL is disabled or failed to start;
    // natively: no usable GPU driver). Everything the game draws goes through
    // SDL_Renderer, which has a software implementation, so fall back to it rather
    // than give up. The window is made again first: creating the failed renderer
    // may have changed it (SDL re-creates a window for a GL driver).
    MickLogger::getInstance()->warn(this, std::string("Could not create an accelerated renderer (")
        .append(SDL_GetError()).append("); trying the software renderer."));
    SDL_DestroyWindow(m_windowHandle);
    m_windowHandle = SDL_CreateWindow(windowMetadata.windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                windowMetadata.windowWidth, windowMetadata.windowHeight, windowFlags);
    if (m_windowHandle != nullptr)
    {
      // (Override: a user's SDL_RENDER_DRIVER environment variable must not
      // decide a fallback.)
      SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
      m_rendererHandle = SDL_CreateRenderer(m_windowHandle, -1, 0);
    }
  }
  if (m_rendererHandle == nullptr)
  {
    std::string msg = std::string("Failed to create sdl_renderer! ").append(SDL_GetError());
    MickLogger::getInstance()->error(this, msg);
    // The constructor is about to throw, so the destructor will not run: release
    // what this attempt made.
    if (m_windowHandle != nullptr)
    {
      SDL_DestroyWindow(m_windowHandle);
      m_windowHandle = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    throw runtime_error(msg);
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
    throw runtime_error(msg);
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
    throw runtime_error(msg);
  }
  // update blitting RECT
  SDL_Rect clientArea;
  clientArea.x = 0;
  clientArea.y = 0;
  clientArea.w = windowMetadata.windowWidth -1 ;
  clientArea.h = windowMetadata.windowHeight - 1;

  SDL_SetClipRect(m_surfaceBackBufferHandle, &clientArea);

  initialised = true;
}

void MickSDLRenderer::resize(int contentWidth, int contentHeight)
{
  // Note: this frees+recreates the back buffer mid-frame (the main loop locks it
  // around consoleMain). That is safe only because these software SDL surfaces
  // report SDL_MUSTLOCK==false, so the loop's lock/unlock are no-ops. If the back
  // buffer ever becomes a surface that must be locked, this needs revisiting.
  SDL_SetWindowSize(m_windowHandle, contentWidth, contentHeight);

  Uint32 pixel_format = SDL_GetWindowPixelFormat(m_windowHandle);

  // Recreate the primary texture at the new size.
  if (m_textureHandle != nullptr)
  {
    SDL_DestroyTexture(m_textureHandle);
    m_textureHandle = nullptr;
  }
  m_textureHandle = SDL_CreateTexture(m_rendererHandle, pixel_format, SDL_TEXTUREACCESS_STREAMING,
                                      contentWidth, contentHeight);
  if (m_textureHandle == nullptr)
  {
    std::string msg = "Failed to recreate primary SDL texture on resize!";
    MickLogger::getInstance()->error(this, msg);
    throw runtime_error(msg);
  }

  // Recreate the back-buffer surface at the new size, preserving bit depth.
  int bpp = (m_surfaceBackBufferHandle != nullptr) ? m_surfaceBackBufferHandle->format->BitsPerPixel : 32;
  if (m_surfaceBackBufferHandle != nullptr)
  {
    SDL_FreeSurface(m_surfaceBackBufferHandle);
    m_surfaceBackBufferHandle = nullptr;
  }

  int depth = 0;
  Uint32 rmask = 0, gmask = 0, bmask = 0, amask = 0;
  SDL_PixelFormatEnumToMasks(pixel_format, &depth, &rmask, &gmask, &bmask, &amask);

  m_surfaceBackBufferHandle = SDL_CreateRGBSurface(0, contentWidth, contentHeight, bpp,
                                                   rmask, gmask, bmask, amask);
  if (m_surfaceBackBufferHandle == nullptr)
  {
    std::string msg = "Failed to recreate primary SDL surface on resize!";
    MickLogger::getInstance()->error(this, msg);
    throw runtime_error(msg);
  }

  SDL_Rect clientArea;
  clientArea.x = 0;
  clientArea.y = 0;
  clientArea.w = contentWidth - 1;
  clientArea.h = contentHeight - 1;
  SDL_SetClipRect(m_surfaceBackBufferHandle, &clientArea);
}

void MickSDLRenderer::pushCpuBufferToHardwareBuffer()
{
  SDL_UpdateTexture(getPrimaryTextureHandle(), nullptr, getSurfaceBackBufferHandle()->pixels, getSurfaceBackBufferHandle()->pitch);
  // Clear to black so letterbox/pillarbox bars are black (default draw colour can
  // be left over from elsewhere).
  SDL_SetRenderDrawColor(getRendererHandle(), 0, 0, 0, 255);
  SDL_RenderClear(getRendererHandle());

  if (m_preserveAspect)
  {
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(getRendererHandle(), &outW, &outH);
    int contentW = getSurfaceBackBufferHandle()->w;
    int contentH = getSurfaceBackBufferHandle()->h;
    if (outW > 0 && outH > 0 && contentW > 0 && contentH > 0)
    {
      // Fit the content within the window, preserving aspect ratio, centred.
      double scale = std::min((double) outW / contentW, (double) outH / contentH);
      SDL_Rect dest;
      dest.w = (int) (contentW * scale);
      dest.h = (int) (contentH * scale);
      dest.x = (outW - dest.w) / 2;
      dest.y = (outH - dest.h) / 2;
      SDL_RenderCopy(getRendererHandle(), getPrimaryTextureHandle(), nullptr, &dest);
      return;
    }
  }

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
  if (sInstance == this)
  {
    sInstance = nullptr;
  }
}

MickSDLRenderer* MickSDLRenderer::getInstance()
{
  assert(sInstance);
  return sInstance;
}

MickSDLRenderer* MickSDLRenderer::getInstanceIfCreated()
{
  return sInstance;
}
