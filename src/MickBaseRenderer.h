/*
 * MickBaseRenderer.h
 *
 *  Created on: Apr 04, 2022
 *      Author: msaun
 */

#ifndef MICKBASERENDERER_H_
#define MICKBASERENDERER_H_

#include <string>
#include <assert.h>

struct WindowMetadata
{
  int windowWidth;
  int windowHeight;
  int windowBpp;
  std::string windowTitle;
  unsigned int initFlags = 0;
};

// Initialisation parameters supported
static const unsigned int ASK_FULLSCREEN  = (1 << 1);

template <class W, class R, class S, class T> // WindowHandle, RendererHandle, SurfaceHandle, TextureHandle
class MickBaseRenderer
{
  public:
    //MickBaseRenderer();
    virtual ~MickBaseRenderer() = default;
    virtual void init(WindowMetadata windowMetadata) = 0;

    W* getWindowHandle()
    {
      assert(initialised);
      return m_windowHandle;
    }
    R* getRendererHandle()
    {
      assert(initialised);
      return m_rendererHandle;
    }
    S* getSurfaceBackBufferHandle()
    {
      assert(initialised);
      return m_surfaceBackBufferHandle;
    }
    T* getPrimaryTextureHandle()
    {
      assert(initialised);
      return m_textureHandle;
    }

  protected:
    bool initialised = false;
    W* m_windowHandle;
    R* m_rendererHandle;
    S* m_surfaceBackBufferHandle;
    T* m_textureHandle;


};

#endif /* MICKBASERENDERER_H_ */
