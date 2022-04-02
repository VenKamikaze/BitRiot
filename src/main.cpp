// WinMain cpp file

// INCLUDES ///////////////////////////////////////////////


/// UNIX INCLUDES
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

#include "MenuRenderer.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Debugger/Debugger.h>

#include "RmlUI/glue/SystemInterfaceSDL2.h"
#include "RmlUI/glue/RenderInterfaceSDL2.h"
#include "RmlUI/glue/ShellFileInterface.h"

// DEFINES ////////////////////////////////////////////////

// defines for windows interface
#define WINDOW_TITLE      "Bit Riot Beta - SDL2 Port"


#define WINDOW_BPP        32    // bitdepth of window (8,16,24 etc.)
// note: if windowed and not
// fullscreen then bitdepth must
// be same as system bitdepth
// also if 8-bit the a pallete
// is created and attached

// Ouestion: is the above comment still valid for SDL2 ?

//const unsigned int COLORKEY_LOW = 0; // low and high values for source
//const unsigned int COLORKEY_HIGH = 0; // color keying (transparency range)

//const Uint8 COLORKEY_R = 0;
//const Uint8 COLORKEY_G = 0;
//const Uint8 COLORKEY_B = 0;
//const Uint8 COLORKEY_A = 0;

// Controls initialisation parameters, such as FULLSCREEN, etc
unsigned int initFlags = 0; // start with all turned off

// Initialisation parameters supported
static const unsigned int ASK_FULLSCREEN  = (1 << 1);

// PROTOTYPES /////////////////////////////////////////////

// game console
//int consoleInit();
//int consoleShutdown();
bool consoleMain();

// GLOBALS ////////////////////////////////////////////////

SDL_Window* sdl_window = nullptr;
SDL_Renderer* sdl_renderer = nullptr;
SDL_Texture* sdl_primary_texture = nullptr;
SDL_Surface* sdl_primary = nullptr;

MenuRenderer* menu = nullptr;

// game object globals
GameEngine* engine = nullptr;

// FUNCTIONS //////////////////////////////////////////////

#if !(SDL_VIDEO_RENDER_OGL)
#error "Only the opengl sdl backend is supported. To add support for others, see http://mdqinc.com/blog/2013/01/integrating-librocket-with-sdl-2/"
#endif


int consoleInit()
{
  if (SDL_Init(SDL_INIT_EVERYTHING) < 0 )
  {
    cerr << "Failed to SDL_Init video!";
    return 1;
  }

  const int WINDOW_WIDTH = (Map::MAP_WIDTH * Map::TILE_WIDTH) + InfoPanel::WIDTH;   // size of window
  const int WINDOW_HEIGHT = Map::MAP_HEIGHT * Map::TILE_HEIGHT;

  sdl_window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                WINDOW_WIDTH, WINDOW_HEIGHT, (initFlags & ASK_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_SHOWN );
  if (sdl_window == nullptr)
  {
    cerr << "Failed to create sdl_window!";
    return 1;
  }

  SDL_JoystickEventState(SDL_ENABLE);


  sdl_renderer = SDL_CreateRenderer(sdl_window, -1, 0);
  if (sdl_renderer == nullptr)
  {
    cerr << "Failed to create sdl_renderer!";
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_RenderSetLogicalSize(sdl_renderer, WINDOW_WIDTH, WINDOW_HEIGHT);

  //SDL_RendererInfo rendererInfo;
  //SDL_GetRendererInfo(sdl_renderer, &rendererInfo);
  //std::cout << "Renderer: " << rendererInfo.name << std::endl;

  Uint32 pixel_format = SDL_GetWindowPixelFormat(sdl_window);

  sdl_primary_texture = SDL_CreateTexture(sdl_renderer, pixel_format, SDL_TEXTUREACCESS_STREAMING,
                                          WINDOW_WIDTH, WINDOW_HEIGHT);
  if (sdl_primary_texture == nullptr)
  {
    cerr << "Failed to create sdl_primary_texture!";
    return 1;
  }

  int depth = 0;
  Uint32 rmask = 0;
  Uint32 gmask = 0;
  Uint32 bmask = 0;
  Uint32 amask = 0;
  SDL_PixelFormatEnumToMasks(pixel_format, &depth, &rmask, &gmask, &bmask, &amask);

  sdl_primary = SDL_CreateRGBSurface(0, WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_BPP,
                                     rmask, gmask, bmask, amask);


  if (sdl_primary == nullptr)
  {
    cerr << "Failed to create sdl_primary!";
    return 1;
  }

  if (TTF_Init() == -1)
  {
    cerr << "Failed in TTF_Init!!";
    return 1;
  }

  try
  {
    MickSDLSound::getInstance()->initAudio();
  }
  catch(exception &e)
  {
    cerr << "Could not init audio!";
    return 1;
  }


  // update blitting RECT
  SDL_Rect clientArea;
  clientArea.x = 0;
  clientArea.y = 0;
  clientArea.w = WINDOW_WIDTH -1 ;
  clientArea.h = WINDOW_HEIGHT - 1;

  SDL_SetClipRect(sdl_primary, &clientArea);

  // set colour keying here */
  //SDL_SetColorKey(sdl_back, SDL_SRCCOLORKEY, SDL_MapRGB(sdl_back->format, COLORKEY_R, COLORKEY_G, COLORKEY_B));

  // seed random number generator
  srand(SDL_GetTicks());

  // all other initialization code goes here...

  Map * staticMap = Map::getInstance();
  staticMap->init(sdl_primary);

  EntityRendererFactory * erf = EntityRendererFactory::getInstance();
  erf->initSurfaces(sdl_primary);

  engine = new GameEngine(sdl_primary);

  return 0;

} // end Game_Init

bool consoleMain()
{
  SDL_FillRect(sdl_primary, nullptr, SDL_MapRGB(sdl_primary->format, 200, 200, 100)); // as per ddbltfxClear

  Uint32 startTime = SDL_GetTicks();

  bool keepRunning = engine->runEngine();

  while ((SDL_GetTicks() - startTime) < 33)
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
    menu = new MenuRenderer(sdl_renderer, sdl_window);
    engine->setMenuSystem(menu);
  }
  catch(exception &e)
  {
    fprintf(stderr, "Exception occurred in initialisation code(): %s\n", e.what());
    quitkey = -1;
  }

  /*#ifdef LIBROCKET_TEST
    try
    {
      while(!menuDone)
      {
        menuDone = menu->showMenu();
      }
    }
    catch(exception &e)
    {
      fprintf(stderr, "Caught exception when rendering menu.");
      cerr << e.what();
    }
  #endif
  */
  // Do game loop
  try
  {
    while(!quitkey)
    {

      //The frame rate regulator
      //Timer fps;

      //Start the frame timer
      //fps.start();

      if(SDL_MUSTLOCK(sdl_primary))
      {
        if(SDL_LockSurface(sdl_primary) < 0)
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
      catch(InputException& e)
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

      if(sdl_primary != nullptr)
      {
        SDL_UpdateTexture(sdl_primary_texture, nullptr, sdl_primary->pixels, sdl_primary->pitch);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_primary_texture, nullptr, nullptr);
        SDL_RenderPresent(sdl_renderer);
      }
      else
      {
        cerr << "Something major went wrong. Lost primary buffer pointer.";
        break;
      }

      if(SDL_MUSTLOCK(sdl_primary))
      {
        SDL_UnlockSurface(sdl_primary);
      }

      //Cap the frame rate
      //if( fps.get_ticks() < 1000 / FRAMES_PER_SECOND )
      //{
      //     SDL_Delay( ( 1000 / FRAMES_PER_SECOND ) - fps.get_ticks() );
      //}
      //SDL_Delay(20);
    }

  }
  catch(exception& e)
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

    if(SDL_WasInit(SDL_INIT_EVERYTHING))
    {
      if(sdl_primary != nullptr)
      {
        SDL_FreeSurface(sdl_primary);
        sdl_primary = nullptr;
      }

      if(sdl_renderer != nullptr)
      {
        SDL_DestroyRenderer(sdl_renderer); // also frees sdl_primary_texture
        sdl_primary_texture = nullptr;
        sdl_renderer = nullptr;
      }

      if(sdl_window != nullptr)
      {
        SDL_DestroyWindow(sdl_window);
        sdl_window = nullptr;
      }

      SDL_Quit();
    }
  }
  catch(exception &e)
  {
    cerr << "Caught exception when de-initializing subsystems!" << endl;
    cerr << e.what();
  }

  if (engine)
  {
    delete engine;
  }

  delete MickSDLSound::getInstance(); // hmm...

  if (menu)
  {
    delete menu;
  }

  cout << "Exiting.. " << endl;
  return 0;
}


//////////////////////////////////////////////////////////
