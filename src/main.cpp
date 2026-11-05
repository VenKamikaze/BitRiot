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
#include "GameOptions.h"

#include <filesystem>
#include <system_error>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "web/WebHooks.h"
#endif

// DEFINES ////////////////////////////////////////////////

// defines for windows interface
#define WINDOW_TITLE      "Bit Riot Beta"
#define WINDOW_BPP        32    // bitdepth of window (8,16,24 etc.)

// Controls initialisation parameters, such as FULLSCREEN, etc
unsigned int initFlags = 0; // start with all turned off


// PROTOTYPES /////////////////////////////////////////////
FrameResult frame();
static void shutdownGame();

// GLOBALS ////////////////////////////////////////////////

// game object globals
unique_ptr<GameEngine> engine = nullptr;
// --record <dir> (GameOptions): handed to the engine when it is made.
static std::string recordDirectory;
// --host / --join (brief 07): handed to the engine once the menus exist.
static GameOptions netOptions;
unique_ptr<FrameLoop> frameLoop = nullptr;

// FUNCTIONS //////////////////////////////////////////////

// The frame loop's clock: a free-running millisecond counter. It reads SDL's
// monotonic performance counter, not SDL_GetTicks(), because SDL_GetTicks() on
// Linux can step 1 ms backwards and forwards at second boundaries (its seconds and
// nanoseconds are scaled separately), which showed up as a frame that came a
// millisecond early and so had no tick due.
static uint32_t clockMs()
{
#ifdef __EMSCRIPTEN__
  // The browser's own monotonic clock (performance.now()), which is also what
  // requestAnimationFrame timestamps come from.
  // (Through uint64_t: converting a double beyond uint32_t's range directly is undefined.)
  return (uint32_t) (uint64_t) emscripten_get_now();
#else
  const Uint64 perMs = SDL_GetPerformanceFrequency() / 1000;
  return (uint32_t) (SDL_GetPerformanceCounter() / (perMs ? perMs : 1));
#endif
}

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
  engine->setRecordDirectory(recordDirectory);

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

// Applies the parsed options to the game's globals; the --record directory is
// handed to the engine once it exists (consoleInit).
static void applyOptions(const GameOptions& options)
{
  if (options.fullscreen)
  {
    initFlags = initFlags | ASK_FULLSCREEN;
  }
  if (options.bigMap)
  {
    Map::MAP_WIDTH=25;
    Map::MAP_HEIGHT=19;
    // Keep the canonical model in sync. GameSettings is constructed pre-main
    // (a file-scope binder forces it), so it already captured the default 17x13;
    // without this the GAME_INIT resize guard would see a mismatch and shrink
    // the map back to 17x13, defeating -b (and it also fixes the pre-existing
    // render-25/logic-17 split).
    GameSettings::getInstance()->setMapWidth(25);
    GameSettings::getInstance()->setMapHeight(19);
  }
  recordDirectory = options.recordDir;
  netOptions = options;
}

// One pass of the main loop: lock the back buffer, run frame(), present, unlock.
// Returns false when the game should stop (frame() asked to quit, or the back
// buffer pointer was lost). Shared by the native while-loop and the browser's
// requestAnimationFrame callback.
static bool loopIteration()
{
  MickSDLRenderer *renderer = MickSDLRenderer::getInstance();
#ifdef BITRIOT_NETPLAY
  const bool hiddenTab = engine && engine->hidden();   // a background browser tab: step, but draw and present nothing
#else
  const bool hiddenTab = false;
#endif

  if(SDL_MUSTLOCK(renderer->getSurfaceBackBufferHandle()))
  {
    if(SDL_LockSurface(renderer->getSurfaceBackBufferHandle()) < 0)
    {
      return false;
    }
  }

  const FrameResult result = frame();
  const bool keepRunning = result.keepRunning;
  if(result.ticksStepped > 0 && engine)
  {
    // The inputs the steps just sampled go out now, not at the next pump (up to a frame or, in a hidden browser tab, a network
    // event later: that is a whole tick interval of the input delay).
    engine->flushNet();
  }

  if(renderer->getSurfaceBackBufferHandle() != nullptr)
  {
    if(!hiddenTab)
    {
      SDL_RenderPresent(renderer->getRendererHandle());
    }
  }
  else
  {
    cerr << "Something major went wrong. Lost primary buffer pointer.";
    return false;
  }

  if(SDL_MUSTLOCK(renderer->getSurfaceBackBufferHandle()))
  {
    SDL_UnlockSurface(renderer->getSurfaceBackBufferHandle());
  }

  return keepRunning;
}

// Un-init section: everything the game opened, in reverse order. Safe to call when
// initialisation stopped part way.
static void shutdownGame()
{
  try
  {
    if(engine)
    {
#ifdef BITRIOT_NETPLAY
      engine->stopNetplay(); // tell the others we are gone, and put the player's settings back
#endif
      engine->finishRecording(); // the window was closed mid-round: keep what was played
    }

    if(TTF_WasInit())
    {
      TTF_Quit(); //Quit SDL_ttf
    }

    delete MickSDLRenderer::getInstanceIfCreated();

    delete MickSDLSound::getInstance();
  }
  catch(const exception &e)
  {
    cerr << "Caught exception when de-initializing subsystems!" << endl;
    cerr << e.what();
  }

  cout << "Exiting.. " << endl;
}

#ifdef __EMSCRIPTEN__
static bool webLoopRunning = false;   // the main loop is on (between its start and the shutdown)

// The browser's main-loop callback (emscripten_set_main_loop). An exception must
// not escape into the browser's event loop: it is reported, and the game stops the
// same way as a quit.
static void webLoopIteration()
{
  bool keepRunning = false;
  try
  {
    keepRunning = loopIteration();
    webHooksAfterFrame();
  }
  catch(const exception& e)
  {
    cerr << "Caught exception in main game loop: " << e.what() << endl;
    webReportFatal(string("Bit Riot stopped: ").append(e.what()));
  }
  catch(...)
  {
    cerr << "Caught an unknown exception in main game loop." << endl;
    webReportFatal("Bit Riot stopped: unknown error");
  }

  if(!keepRunning)
  {
    webLoopRunning = false;
    emscripten_cancel_main_loop();
    shutdownGame();
    webReportQuit();
  }
}

// One pass of the main loop for a tab the browser has hidden (brief 12). A hidden tab gets no animation frames, so the page
// calls this from network events and a one-second timer instead (src/web/WebHooks.cpp); it does what webLoopIteration does,
// with the engine in its hidden mode (it steps every tick it holds, draws nothing).
void webHiddenPoll()
{
  if(!webLoopRunning || !engine || !engine->hiddenPassesWanted())
  {
    return;
  }
  webCountHiddenPoll();
  webLoopIteration();
}
#endif

int main(int argc, char* argv[])
{
  int quitkey = 0;

  if(argc > 0)
  {
    const GameOptions options = parseGameOptions(argc, argv, std::cerr);
    if(options.help)
    {
      printGameUsage(argv[0], std::cout);
      return 0;
    }
    if(options.error)
    {
      printGameUsage(argv[0], std::cerr);
      return 2;
    }
    if(!options.recordDir.empty())
    {
      // Fail now, not at the end of the first round, if records cannot be written.
      std::error_code ec;
      std::filesystem::create_directories(options.recordDir, ec);
      if(ec || !std::filesystem::is_directory(options.recordDir))
      {
        std::cerr << "Cannot use " << options.recordDir << " for match records: "
                  << (ec ? ec.message() : std::string("not a directory")) << std::endl;
        return 2;
      }
    }
#ifndef BITRIOT_NETPLAY
    if(options.networked())
    {
      // Before any window opens: this build cannot do what was asked.
      std::cerr << "This build has no network play (it was configured with BITRIOT_NETPLAY=OFF)." << std::endl;
      return 2;
    }
#endif
    applyOptions(options);
  }

  // Init sub-systems
  try
  {
    quitkey = consoleInit();

    engine->setMenuSystem(make_unique<MenuSDLRenderer>(MickSDLRenderer::getInstance()->getRendererHandle(), 
      MickSDLRenderer::getInstance()->getWindowHandle(), GameRunningSDLInput::getInstance()->getInputHandler()));

    frameLoop = make_unique<FrameLoop>(engine->frameHooks(clockMs),
                                       (uint32_t) GameSettings::NORMAL_TICKS_SPEED);

    if(netOptions.networked())
    {
#ifdef BITRIOT_NETPLAY
      NetGame::Params net;
      net.host = netOptions.host;
      net.port = (uint16_t) netOptions.hostPort;
      net.join = netOptions.join;
      net.name = netOptions.name;
      net.localPlayers = netOptions.localPlayers;
      net.totalSlots = netOptions.players;
      if(netOptions.viaRelay())
      {
        // --host --relay ADDR (a new room) or --relay ADDR --room CODE (join one).
        netplay::RelayTarget target;
        std::string relayError;
        std::string roomCode;
        if(!netplay::parseRelayAddress(netOptions.relay, false, &target, &relayError)
           || (!netOptions.room.empty() && !netplay::parseRoomCode(netOptions.room, &roomCode, &relayError)))
        {
          cerr << relayError << endl;
          shutdownGame();
          return 2;
        }
        net.relay = true;
        net.relayWs = target.ws;
        net.relayUdp = target.udp;
        net.relayDisplay = netOptions.relay;
        net.room = roomCode;
        net.join = netOptions.relay;
      }
      std::string netError;
      if(!engine->startNetplay(net, &netError))
      {
        cerr << "Cannot start network play: " << netError << endl;
        shutdownGame();
        return 2;
      }
      frameLoop->setHeldPollMs(4);
#else
      cerr << "This build has no network play (it was configured with BITRIOT_NETPLAY=OFF)." << endl;
      shutdownGame();
      return 2;
#endif
    }
  }
  catch(const exception &e)
  {
    MickLogger::getInstance()->error(nullptr, string("Exception occurred in initialisation code(): ").append(e.what()));
    quitkey = -1;
  }

#ifdef __EMSCRIPTEN__
  if(quitkey)
  {
    webReportFatal("Bit Riot could not start. See the browser console for the reason.");
    shutdownGame();
    return 1;
  }
  webHooksInit(engine.get());
  webLoopRunning = true;
  // The browser paces the loop (requestAnimationFrame, fps 0) and calls frame() once
  // per animation frame; frame() runs however many whole ticks are owed, so a 60 Hz
  // display and the 30 Hz tick rate need no sleep. Nothing here may block. The last
  // argument makes main() not return: the loop owns the program from here, and
  // webLoopIteration() tears everything down when the game asks to quit.
  emscripten_set_main_loop(webLoopIteration, 0, 1);
  return 0; // not reached
#else
  // Do game loop
  try
  {
    while(!quitkey)
    {
      // main game processing goes here. (ESC is a flag the engine checks, not an
      // exception; the outer catch below handles anything the frame throws.)
      if(! loopIteration())
      {
        quitkey = 1;
      }

      // Hold the tick rate: sleep until the next tick falls due (about 33 ms after
      // the last one). frame() itself never waits, so a slow frame is not followed
      // by a sleep, and the ticks it owes are run by the next frame.
      if(!quitkey)
      {
        const uint32_t sleepMs = frameLoop->sleepHintMs();
        if(sleepMs > 0)
        {
#ifdef BITRIOT_NETPLAY
          // In a network game the sleep ends when a datagram arrives, so a tick the host has
          // just confirmed is not left waiting for the next frame.
          if(!engine->netplayWait(sleepMs))
#endif
          {
            SDL_Delay(sleepMs);
          }
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

  shutdownGame();
  return 0;
#endif
}


//////////////////////////////////////////////////////////
