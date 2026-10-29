// WinMain cpp file

// INCLUDES ///////////////////////////////////////////////


/// UNIX INCLUDES
#include "MickBaseRenderer.h"
#include "MickLogger.h"
#include "GameRunningSDLInput.h"
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
#include <fcntl.h>

#include <SDL2/SDL.h>
#include "MickSDLSound.h"

// game object includes
#include "GameEngine.h"

#ifndef NULL
#define NULL nullptr;
#endif

#include "MenuSDLRenderer.h"
#include "FrameLoop.h"
#include "EntityRendererFactory.h"
#include "MapRenderer.h"
#include "MickSDLUtil.h"

// DEFINES ////////////////////////////////////////////////

// defines for windows interface
#define WINDOW_TITLE      "Bit Riot Beta"
#define WINDOW_BPP        32    // bitdepth of window (8,16,24 etc.)

// Controls initialisation parameters, such as FULLSCREEN, etc
unsigned int initFlags = 0; // start with all turned off


// PROTOTYPES /////////////////////////////////////////////
FrameResult frame();

// GLOBALS ////////////////////////////////////////////////

// game object globals
unique_ptr<GameEngine> engine = nullptr;
unique_ptr<FrameLoop> frameLoop = nullptr;

// FUNCTIONS //////////////////////////////////////////////

// The frame loop's clock: a free-running millisecond counter. It reads SDL's
// monotonic performance counter, not SDL_GetTicks(), because SDL_GetTicks() on
// Linux can step 1 ms backwards and forwards at second boundaries (its seconds and
// nanoseconds are scaled separately), which showed up as a frame that came a
// millisecond early and so had no tick due.
static uint32_t clockMs()
{
  const Uint64 perMs = SDL_GetPerformanceFrequency() / 1000;
  return (uint32_t) (SDL_GetPerformanceCounter() / (perMs ? perMs : 1));
}

#if !(SDL_VIDEO_OPENGL)
#error "Only the opengl sdl backend is supported. To add support for others, see http://mdqinc.com/blog/2013/01/integrating-librocket-with-sdl-2/"
#endif


int consoleInit()
{
  WindowMetadata windowMetadata;
  windowMetadata.windowWidth = (Map::MAP_WIDTH * Map::TILE_WIDTH) + InfoPanel::WIDTH;   // size of window
  windowMetadata.windowHeight = Map::MAP_HEIGHT * Map::TILE_HEIGHT;
  windowMetadata.windowBpp = WINDOW_BPP;
  windowMetadata.windowTitle = WINDOW_TITLE;
  windowMetadata.initFlags = initFlags;

  new MickSDLRenderer(windowMetadata); // performs initialisation
  SDL_JoystickEventState(SDL_ENABLE);

  if (TTF_Init() == -1)
  {
    cerr << "Failed in TTF_Init!!";
    return 1;
  }

  shared_ptr<GameRunningSDLInput> input;
  try
  {
    MickSDLSound::getInstance()->initAudio();
    input = GameRunningSDLInput::create(make_shared<InputHandler>());
  }
  catch(const exception &e)
  {
    std::string msg = "Could not initialise audio and/or input!";
    MickLogger::getInstance()->error(nullptr, msg);
    MickLogger::getInstance()->error(nullptr, e.what());
    throw runtime_error(msg);
  }

  MickSDLRenderer *renderer = MickSDLRenderer::getInstance();
  // The simulation builds its own Map for every round (Simulation::start); this
  // draws it.
  Map::getInstance()->resize(Map::MAP_WIDTH, Map::MAP_HEIGHT);
  MapRenderer::getInstance()->init(renderer->getSurfaceBackBufferHandle());
  EntityRendererFactory * erf = EntityRendererFactory::getInstance();
  erf->initSurfaces(renderer->getSurfaceBackBufferHandle());
  
  engine = make_unique<GameEngine>(renderer->getSurfaceBackBufferHandle(), input->getInputHandler() );

  return 0;
} // end Game_Init

// One frame of the game: events, however many whole ticks are owed (usually one),
// then one render. Does not block or sleep; the caller sleeps for
// frameLoop->sleepHintMs() afterwards. This is the function a web build hands to
// emscripten_set_main_loop (see FrameLoop.h).
FrameResult frame()
{
  return frameLoop->frame();
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
        // Keep the canonical model in sync. GameSettings is constructed pre-main
        // (a file-scope binder forces it), so it already captured the default 17x13;
        // without this the GAME_INIT resize guard would see a mismatch and shrink
        // the map back to 17x13, defeating -b (and it also fixes the pre-existing
        // render-25/logic-17 split).
        GameSettings::getInstance()->setMapWidth(25);
        GameSettings::getInstance()->setMapHeight(19);
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

    engine->setMenuSystem(make_unique<MenuSDLRenderer>(MickSDLRenderer::getInstance()->getRendererHandle(), 
      MickSDLRenderer::getInstance()->getWindowHandle(), GameRunningSDLInput::getInstance()->getInputHandler()));

    frameLoop = make_unique<FrameLoop>(engine->frameHooks(clockMs),
                                       (uint32_t) GameSettings::NORMAL_TICKS_SPEED);
  }
  catch(const exception &e)
  {
    MickLogger::getInstance()->error(nullptr, string("Exception occurred in initialisation code(): ").append(e.what()));
    quitkey = -1;
  }

  MickSDLRenderer *renderer = MickSDLRenderer::getInstance();

  // Do game loop
  try
  {
    while(!quitkey)
    {

      if(SDL_MUSTLOCK(renderer->getSurfaceBackBufferHandle()))
      {
        if(SDL_LockSurface(renderer->getSurfaceBackBufferHandle()) < 0)
        {
          break;
        }
      }

      // main game processing goes here. (ESC is a flag the engine checks, not an
      // exception; the outer catch below handles anything the frame throws.)
      if(! frame().keepRunning)
      {
        quitkey = 1;
      }

      if(renderer->getSurfaceBackBufferHandle() != nullptr)
      {
        SDL_RenderPresent(renderer->getRendererHandle());
      }
      else
      {
        cerr << "Something major went wrong. Lost primary buffer pointer.";
        break;
      }

      if(SDL_MUSTLOCK(renderer->getSurfaceBackBufferHandle()))
      {
        SDL_UnlockSurface(renderer->getSurfaceBackBufferHandle());
      }

      // Hold the tick rate: sleep until the next tick falls due (about 33 ms after
      // the last one). frame() itself never waits, so a slow frame is not followed
      // by a sleep, and the ticks it owes are run by the next frame.
      if(!quitkey)
      {
        const uint32_t sleepMs = frameLoop->sleepHintMs();
        if(sleepMs > 0)
        {
          SDL_Delay(sleepMs);
        }
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

    delete renderer;
    renderer = nullptr;

    delete MickSDLSound::getInstance();
  }
  catch(const exception &e)
  {
    cerr << "Caught exception when de-initializing subsystems!" << endl;
    cerr << e.what();
  }

  cout << "Exiting.. " << endl;
  return 0;
}


//////////////////////////////////////////////////////////
