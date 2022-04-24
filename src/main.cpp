// WinMain cpp file

// INCLUDES ///////////////////////////////////////////////


/// UNIX INCLUDES
#include "MickBaseRenderer.h"
#include "MickSDLRenderer.h"
#include "SDL_render.h"
#include "SDL_surface.h"
#include <stdexcept>
#if defined (__GNUC__) && defined(__unix__)


/// WIN32 INCLUDES
#elif defined (WIN32)

#include <getopt.h> // MinGW

#endif


#include <iostream> // include important C/C++ stuff
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>

#include <SDL2/SDL.h>
#include "MickSDLSound.h"

// game object includes
#include "GameEngine.h"

#include <GL/glew.h>

#ifndef NULL
#define NULL nullptr;
#endif

#include "MenuSDLRenderer.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger/Debugger.h>

#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include "RmlUI/glue/RenderInterfaceSDL2.h"
#include "RmlUI/glue/ShellFileInterface.h"

// DEFINES ////////////////////////////////////////////////

// defines for windows interface
#define WINDOW_TITLE      "Bit Riot Beta"
#define WINDOW_BPP        32    // bitdepth of window (8,16,24 etc.)

// Controls initialisation parameters, such as FULLSCREEN, etc
unsigned int initFlags = 0; // start with all turned off


// PROTOTYPES /////////////////////////////////////////////

// game console
//int consoleInit();
//int consoleShutdown();
bool consoleMain();

// GLOBALS ////////////////////////////////////////////////

// Create an SDL renderer
MickBaseRenderer<SDL_Window, SDL_Renderer, SDL_Surface, SDL_Texture> *m_renderer = nullptr;

MenuSDLRenderer* menu = nullptr;

// game object globals
GameEngine* engine = nullptr;

// FUNCTIONS //////////////////////////////////////////////

#if !(SDL_VIDEO_RENDER_OGL)
#error "Only the opengl sdl backend is supported. To add support for others, see http://mdqinc.com/blog/2013/01/integrating-librocket-with-sdl-2/"
#endif


int consoleInit()
{
  WindowMetadata windowMetadata;
  windowMetadata.windowWidth = (Map::MAP_WIDTH * Map::TILE_WIDTH) + InfoPanel::WIDTH;   // size of window
  windowMetadata.windowHeight = Map::MAP_HEIGHT * Map::TILE_HEIGHT;
  windowMetadata.windowBpp = WINDOW_BPP;
  windowMetadata.windowTitle = WINDOW_TITLE;

  m_renderer = MickSDLRenderer::getInstance(windowMetadata); // performs initialisation
  SDL_JoystickEventState(SDL_ENABLE);

  if (TTF_Init() == -1)
  {
    cerr << "Failed in TTF_Init!!";
    return 1;
  }

  try
  {
    MickSDLSound::getInstance()->initAudio();
  }
  catch(const exception &e)
  {
    std::string msg = "Could not initialise audio!";
    MickLogger::getInstance()->error(nullptr, msg);
    MickLogger::getInstance()->error(nullptr, e.what());
    throw runtime_error(msg);
  }

  Map * staticMap = Map::getInstance();
  staticMap->init(m_renderer->getSurfaceBackBufferHandle());
  EntityRendererFactory * erf = EntityRendererFactory::getInstance();
  erf->initSurfaces(m_renderer->getSurfaceBackBufferHandle());
  engine = new GameEngine(m_renderer->getSurfaceBackBufferHandle());

  return 0;
} // end Game_Init

bool consoleMain()
{
  SDL_FillRect(m_renderer->getSurfaceBackBufferHandle(), nullptr, SDL_MapRGB(m_renderer->getSurfaceBackBufferHandle()->format, 200, 200, 100)); // as per ddbltfxClear

  Uint32 startTime = SDL_GetTicks();

  bool keepRunning = engine->runEngine();

  while ((SDL_GetTicks() - startTime) < GameSettings::NORMAL_TICKS_SPEED)
  {
    //wait a bit to sync to 30fps
    //Sleep(1);
    SDL_Delay(1);
  }

  return keepRunning;

} // end Game_Main

static void show_opts(std::string exec)
{
  std::cerr << "Usage: " << exec << " <option(s)>\n"
            << "Options:\n"
            << "\t-h\t\tShow this help message\n"
            << "\t-f\t\tScale to fullscreen\n"
            << "\t-b\t\tUse a big map\n"
            << std::endl;
}

static void setInitFlags(int argc, char* argv[])
{
  int c = 0;

  while ((c = getopt (argc, argv, "fb")) != -1)
  {
    switch (c)
    {
      case 'f':
        initFlags = initFlags | ASK_FULLSCREEN;
        break;
      case 'b':
        Map::MAP_WIDTH=25;
        Map::MAP_HEIGHT=19;
        break;
      default :  /* h */
        show_opts(argv[0]);
        break;
    }
  }
}

int main(int argc, char* argv[])
{
  int quitkey = 0;

  if(argc > 0)
  {
    setInitFlags(argc, argv);
  }

  // Init sub-systems
  try
  {
    quitkey = consoleInit();
    menu = new MenuSDLRenderer(m_renderer->getRendererHandle(), m_renderer->getWindowHandle());
    engine->setMenuSystem(menu);
  }
  catch(const exception &e)
  {
    fprintf(stderr, "Exception occurred in initialisation code(): %s\n", e.what());
    quitkey = -1;
  }

  // Do game loop
  try
  {
    while(!quitkey)
    {

      if(SDL_MUSTLOCK(m_renderer->getSurfaceBackBufferHandle()))
      {
        if(SDL_LockSurface(m_renderer->getSurfaceBackBufferHandle()) < 0)
        {
          break;
        }
      }

      try
      {
        // main game processing goes here
        if(! consoleMain())
        {
          quitkey = 1;
        }
      }
      catch(const InputException& e)
      {
        if(e.gotQuit())
        {
          quitkey = 1;
        }
        else
        {
          cerr << "Caught strange InputException! ";
          cerr << e.what();
          break;
        }
      }

      if(m_renderer->getSurfaceBackBufferHandle() != nullptr)
      {
        SDL_UpdateTexture(m_renderer->getPrimaryTextureHandle(), nullptr, m_renderer->getSurfaceBackBufferHandle()->pixels, m_renderer->getSurfaceBackBufferHandle()->pitch);
        SDL_RenderClear(m_renderer->getRendererHandle());
        SDL_RenderCopy(m_renderer->getRendererHandle(), m_renderer->getPrimaryTextureHandle(), nullptr, nullptr);
        SDL_RenderPresent(m_renderer->getRendererHandle());
      }
      else
      {
        cerr << "Something major went wrong. Lost primary buffer pointer.";
        break;
      }

      if(SDL_MUSTLOCK(m_renderer->getSurfaceBackBufferHandle()))
      {
        SDL_UnlockSurface(m_renderer->getSurfaceBackBufferHandle());
      }

    }

  }
  catch(const exception& e)
  {
    fprintf(stderr, "Caught exception in main game loop.");
    cerr << "Caught exception! ";
    cerr << e.what();
  }

  // Un-init section
  try
  {
    if(TTF_WasInit())
    {
      TTF_Quit(); //Quit SDL_ttf
    }

    delete m_renderer;
    m_renderer = nullptr;

    delete MickSDLSound::getInstance();
  }
  catch(const exception &e)
  {
    cerr << "Caught exception when de-initializing subsystems!" << endl;
    cerr << e.what();
  }

  if (engine)
  {
    delete engine;
    engine = nullptr;
  }
  
//  if (menu)
//  {
//    delete menu;
//    menu = nullptr;
//  }

  cout << "Exiting.. " << endl;
  return 0;
}


//////////////////////////////////////////////////////////
