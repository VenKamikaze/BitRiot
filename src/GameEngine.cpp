#include "GameEngine.h"
#include "SimRandom.h"
#include "GameSettings.h"
#include "MickBaseInput.h"
#include "GameRunningSDLInput.h"
#include "MickSDLRenderer.h"
#include "PlayerCharacterEntity.h"
#include "EntityRendererFactory.h"
#include "MapRenderer.h"
#include "WorldRenderer.h"
#ifdef __EMSCRIPTEN__
#include "web/WebHooks.h"
#endif
#ifdef BITRIOT_NETPLAY
#include "RmlUI/binders/NetLobbyBinder.h"
#include "RmlUI/events/EventHandlerNetLobby.h"
#endif
#include "RmlUI/events/EventManager.h"
#include <cstddef>
#include <sstream>
#include <ctime>
#include <filesystem>
#include <string>

GameEngine::GameEngine(SDL_Surface* back, shared_ptr<InputHandler> inputHandler)
{
  m_pInputHandler = inputHandler;

  m_surface = back;
  // The window was made from the Map's statics (main.cpp, including -b).
  m_windowMapWidth = Map::MAP_WIDTH;
  m_windowMapHeight = Map::MAP_HEIGHT;

  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
}

GameEngine::~GameEngine()
{
#ifdef BITRIOT_NETPLAY
  stopNetplay();
#endif
  // (A round still being recorded is written by main() through finishRecording()
  // before the engine goes: this runs during static destruction, too late to hash.)
  uninitialise();
}

void GameEngine::uninitialise()
{
  if (m_pPanel)
  {
    delete m_pPanel;
    m_pPanel = NULL;
  }
  m_pInputHandler = nullptr;
}

void GameEngine::resizeWindowIfMapChanged()
{
  // Simulation::start() has already rebuilt the Map at the round's size and set
  // Map::MAP_WIDTH/HEIGHT; only the startup-once resources are left.
  const int newWidth = Map::MAP_WIDTH;
  const int newHeight = Map::MAP_HEIGHT;

  if (m_windowMapWidth == newWidth && m_windowMapHeight == newHeight)
  {
    return; // nothing to do
  }

  m_windowMapWidth = newWidth;
  m_windowMapHeight = newHeight;

  // Resize the startup-once resources (window, back-buffer surface, texture) to the
  // new content size. InfoPanel::WIDTH is the fixed side-panel width.
  int contentWidth = (newWidth * Map::TILE_WIDTH) + InfoPanel::WIDTH;
  int contentHeight = newHeight * Map::TILE_HEIGHT;

  MickSDLRenderer* renderer = MickSDLRenderer::getInstance();
  renderer->resize(contentWidth, contentHeight);

  // The old back buffer was freed in resize(); re-point our handle.
  m_surface = renderer->getSurfaceBackBufferHandle();

  // The tile pictures are converted to the primary surface's format; reload them
  // against the new surface.
  MapRenderer::getInstance()->init(m_surface);
}

void GameEngine::resetGame()
{
#ifdef BITRIOT_NETPLAY
  if (m_net)
  {
    resetNetGame();
    return;
  }
#endif
  GameSettings* settings = GameSettings::getInstance();
  int numHumanPlayers = settings->getNumberOfHumanPlayers();
  int numPlayers = settings->getNumberOfPlayers();
  std::vector<bool>* genders = settings->getPlayerGenders();

  // Everything that must be identical on every peer, including the seed, goes
  // in the config; Simulation::start() reseeds the sim RNG from it before
  // anything that draws random numbers is built.
  MatchConfig config = MatchConfig::fromSettings(*settings);
  config.seed = settings->pickRoundSeed();
  std::MickLogger::getInstance()->info(this, std::string("Round seed=").append(std::to_string(config.seed)));

  std::MickLogger::getInstance()->debug(this, std::string("numHumanPlayers=").append(std::to_string(numHumanPlayers)).append(",numPlayers=").append(std::to_string(numPlayers)));

  if (m_pPanel)
  {
    delete m_pPanel;
    m_pPanel = nullptr;
  }

  // The old world goes, the new one is built: players, spawning pool and blocks.
  // (The game-over countdown is un-latched by the new EntityManager's constructor
  // inside; the tick count restarts at 0.)
  finishRecording(); // a round that was never finished (should not happen) is kept
  m_sim.start(config);
  startRecording();

  // The Map is now the round's size; make the window match (before the HUD is built).
  resizeWindowIfMapChanged();

  // after setting initialisation parameters
  m_pPlayers.resize(numPlayers);

  m_pPanel = new InfoPanel(m_surface, numPlayers, genders);
  m_pPanel->setSimulation(&m_sim); // the HUD reads each slot's selection from the sim
  GameRunningSDLInput::getInstance()->resetInputEvents();
  initPlayerCharacters(numPlayers, genders);
  m_pInputHandler->setPointers(m_pPlayers, m_pPanel);
#ifdef BITRIOT_NETPLAY
  m_pInputHandler->setPadsMayBindOwnSlots(false); // a local round: START always opens the selector
#endif
  m_pPanel->setPlayerPointers(m_pPlayers);
  m_drawNeeded = true; // a new HUD and (maybe) a new back buffer: nothing is drawn yet
}

void GameEngine::startRecording()
{
  m_recorder.reset();
  if (m_recordDir.empty())
  {
    return;
  }
  try
  {
    m_recorder.reset(new MatchRecorder(30, true));
    m_recorder->begin(m_sim);
    // The file is named for when the round started, whenever it ends up written.
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm utc;
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
    m_recordStamp = stamp;
  }
  catch (const std::exception& e)
  {
    std::MickLogger::getInstance()->error(this, std::string("Cannot record this round: ").append(e.what()));
    m_recorder.reset();
  }
}

void GameEngine::finishRecording()
{
  if (!m_recorder)
  {
    return;
  }
  try
  {
    const MatchRecord& record = m_recorder->finish(m_sim);

    // round-<UTC date>-<UTC time the round started>-seed<seed>.brmr, with -2, -3... if
    // that name exists.
    const std::string base = m_recordDir + "/round-" + m_recordStamp + "-seed" + std::to_string(record.config.seed);
    std::string path = base + ".brmr";
    for (int n = 2; std::filesystem::exists(path); n++)
    {
      path = base + "-" + std::to_string(n) + ".brmr";
    }
    record.writeFile(path);
    std::MickLogger::getInstance()->info(this, std::string("Match record written: ").append(path));
  }
  catch (const std::exception& e)
  {
    std::MickLogger::getInstance()->error(this, std::string("Cannot write the match record: ").append(e.what()));
  }
  m_recorder.reset();
}

bool GameEngine::startAllAiMatch(int numPlayers)
{
  GameSettings* settings = GameSettings::getInstance();
  if (numPlayers < GameSettings::MIN_PLAYERS || numPlayers > GameSettings::MAX_PLAYERS || !menuSystem
      || settings->getGameState() != GameSettings::MENU_RUNNING)
  {
    return false;
  }
  // Everything is checked above, so nothing has been changed if we returned false.
  // Five or six players need a taller map than the default 13 tiles to show all
  // their portraits (the player-select page refuses to start otherwise); raise the
  // height rather than refuse. With every slot a CPU player nothing else can stop
  // the game starting.
  settings->setNumberOfPlayers(numPlayers);
  for (int i = 0; i < numPlayers; i++)
  {
    settings->setPlayerAI(i, true);
  }
  if (settings->getMapHeight() < settings->requiredMapHeightFor(numPlayers))
  {
    settings->setMapHeight(settings->requiredMapHeightFor(numPlayers));
  }
  menuSystem->closeAllMenus();
  settings->setGameState(GameSettings::GAME_INIT);
  return true;
}

int GameEngine::gameLengthSeconds() const
{
  return (int) ((m_sim.tick() * GameSettings::NORMAL_TICKS_SPEED) / 1000);
}

FrameLoop::Hooks GameEngine::frameHooks(std::function<uint32_t()> clockMs)
{
  FrameLoop::Hooks hooks;
  hooks.nowMs = std::move(clockMs);
  hooks.simulating = [this] { return simulating(); };
  hooks.pumpEvents = [this] { pumpEvents(); };
  hooks.step = [this] { stepTick(); };
  hooks.render = [this] { return renderFrame(); };
  // Locally a tick is always allowed. In a network game a tick is held until the host's
  // inputs for it have arrived (canStep), and a machine that holds more than it should
  // steps extra ticks to catch up (extraTicks). Both are asked every frame, so they look
  // at m_net then (it can appear and go away while the loop runs).
#ifdef BITRIOT_NETPLAY
  hooks.canStep = [this] { return !m_net || m_net->match().canStep(); };
  hooks.extraTicks = [this] {
    if (!m_net) return 0;
    // A hidden browser tab has no animation frames and so no clock to speak of: it steps whatever it holds (the visible
    // machines set the pace). The frame loop gates every one of these on canStep() and clamps the number.
    return m_hidden ? FrameLoop::DEFAULT_MAX_EXTRA_TICKS_PER_FRAME : m_net->session().extraTicksWanted();
  };
#endif
  return hooks;
}

bool GameEngine::simulating() const
{
  return GameSettings::getInstance()->getGameState() == GameSettings::GAME_RUNNING && !m_roundEnding;
}

void GameEngine::pumpEvents()
{
  m_pumpedQuietly = false;
  if (simulating())
  {
    // Quiet means: the poll ran, and found nothing. (If nothing polls, the frame is
    // never "quiet", and renderFrame() redraws: a front end that replaces this hook
    // loses the optimisation, not the picture.)
    m_pumpedQuietly = !m_pInputHandler->pumpEvents();
  }
#ifdef BITRIOT_NETPLAY
  if (m_net)
  {
    netFrame();   // after the devices: it reads the quit key they may have seen
  }
  else
  {
    netIdleFrame();
  }
#endif
}

void GameEngine::stepTick()
{
#ifdef BITRIOT_NETPLAY
  if (m_net)
  {
    stepNetTick();
    return;
  }
#endif
  // One sim tick. Both the game-over countdown and the scoreboard's game
  // length count ticks, never wall-clock time.

  // ESC ends the round on this machine only: the rest of the tick still runs, as
  // it always has. It is a local flag, not a sim input (and not an exception).
  const bool quitRequested = m_pInputHandler->quitRequested();
  std::shared_ptr<PlayerCharacterEntity> winnerAtQuit;
  if (quitRequested)
  {
    winnerAtQuit = m_sim.currentWinner();
  }

  // The local devices become this tick's inputs: a byte per human slot, sampled
  // once per stepped tick (so a tick held back by canStep() loses no key press), and
  // handed to the simulation, which applies them and then runs the tick: the win
  // check and game-over countdown (a timed delay during which we continue to update
  // and runFrame for a few seconds; useful when all human players are dead but AI is
  // still alive, in case a human player wants to take over the AI), the spawning
  // pool and runFrame. The round is over only once this whole step has run.
  const TickInputs inputs = m_pInputHandler->sampleTick();
  m_sim.step(inputs);
  if (m_recorder)
  {
    try
    {
      m_recorder->onStep(inputs, m_sim);
    }
    catch (const std::exception& e)
    {
      // Never let the recorder stop a game.
      std::MickLogger::getInstance()->error(this, std::string("Match record abandoned: ").append(e.what()));
      m_recorder.reset();
    }
  }
  m_ticksThisFrame++;
  // The sim only records who was hit this tick; the local layer buzzes the pads.
  m_pInputHandler->rumbleForHits(m_sim);

  if (m_sim.roundOver())
  {
    m_roundEnding = true;
    m_scoreboardWinner = m_sim.winner();
  }
  else if (quitRequested)
  {
    m_roundEnding = true;
    m_scoreboardWinner = winnerAtQuit;
  }
}

bool GameEngine::renderFrame()
{
  // The per-frame counters are reset here, at the end of every render, and not in
  // pumpEvents(): a front end that replaces the pump hook (netplay) must not be able
  // to leave a stale "a tick ran this frame" behind.
  struct EndOfFrame
  {
    GameEngine& engine;
    ~EndOfFrame()
    {
      engine.m_ticksThisFrame = 0;
      engine.m_pumpedQuietly = false;
    }
  } endOfFrame{*this};

  switch (GameSettings::getInstance()->getGameState())
  {
    case GameSettings::MENU_RUNNING:
    {
      if (! menuSystem)
      {
        throw std::runtime_error(std::string("About to render menu, but no menu rendering system found!"));
      }

      if (!menuSystem->showMenu())
      {
        GameSettings::getInstance()->setGameState(GameSettings::GAME_QUIT); // ALT+F4?
      }
      break;
    }
    case GameSettings::GAME_INIT:
    {
#ifdef BITRIOT_NETPLAY
      if (m_net && !m_netRoundPending)
      {
        // A local Play Again in a network game: only the host's START begins a round.
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
        break;
      }
#endif
      menuSystem->clearScoreBoard(); // does nothing if scoreboard does not exist.
      resetGame();
      GameSettings::getInstance()->setGameState(GameSettings::GAME_RUNNING);
      break;
    }
    case GameSettings::GAME_RUNNING:
    {
      // ESC (or closing the window) seen on a frame that ran no tick, because the
      // frame loop held it back: the round must still be quittable. When a tick did
      // run, stepTick() has already taken the quit.
      if (!m_roundEnding && m_ticksThisFrame == 0 && GameRunningSDLInput::getInstance()->doQuit())
      {
        m_roundEnding = true;
        m_scoreboardWinner = m_sim.currentWinner();
      }

      m_endingThisFrame = m_roundEnding;
      if (m_roundEnding)
      {
        // The round's last frame: go to the scoreboard (the menu takes over on the
        // next frame, after this one draws the final world below, as it always did).
        m_roundEnding = false;
        finishRecording();
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
#ifdef BITRIOT_NETPLAY
        menuSystem->hideNetOverlay();   // the scoreboard is drawn by the same context
        m_netToasts.clear();
        m_netOverlaySignature.clear();
#endif
        menuSystem->loadScoreBoard(m_scoreboardWinner, m_sim.roster(), gameLengthSeconds());
        m_scoreboardWinner = nullptr;
#ifdef BITRIOT_NETPLAY
        if (m_net && m_net->session().state() != netplay::LockstepSession::ENDED)
        {
          // Only the host starts the next round (its START is what every machine waits for), or takes everyone back
          // to the lobby.
          menuSystem->setScoreboardNet(m_net->isHost(), m_net->isHost() ? "Next round" : "Waiting for the host...",
                                       m_net->isHost(), m_net->isHost());
          m_hostWantsRound = false;
          m_hostWantsLobby = false;
        }
#endif
      }

      // Draw the world and the HUD onto the CPU surface only when something may have
      // changed: a tick ran, the round is ending, an event was read (a join
      // indicator, a window event), or the surface is new. A frame with none of
      // those (a tick held back, a frame that came early, a browser's 60 Hz frame
      // between 30 Hz ticks) would draw the same picture again. The surface is still
      // pushed to the texture every frame: the renderer's back buffers alternate, so
      // each must be given the picture again or the screen would flicker between
      // an old frame and the new one.
#ifdef BITRIOT_NETPLAY
      if (m_hidden)
      {
        // Nobody is looking (a background browser tab): no drawing at all. The next visible frame redraws everything.
        m_drawNeeded = true;
        m_endingThisFrame = false;
        break;
      }
#endif
      const bool redraw = m_drawNeeded || m_endingThisFrame || m_ticksThisFrame > 0 || !m_pumpedQuietly;
      m_drawNeeded = false;
      m_endingThisFrame = false;
      if (redraw)
      {
        WorldRenderer::renderWorld(m_sim.entityManager(), m_surface);

        m_pPanel->renderSurfaceTo(m_surface, (GameSettings::getInstance()->getMapWidth() * Map::TILE_WIDTH), 0);
      }

      MickSDLRenderer::getInstance()->pushCpuBufferToHardwareBuffer();
#ifdef BITRIOT_NETPLAY
      if (m_net)
      {
        menuSystem->renderNetOverlay();   // RmlUi, over the picture just pushed (never part of the CPU surface)
      }
#endif
      break;
    }
    case GameSettings::GAME_QUIT:
    {
      return false;
      break;
    }
  }

  return true;
}


void GameEngine::initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers)
{
  if (numPlayers > NUM_TEAMS)
  {
    numPlayers = NUM_TEAMS;
  }

  // The Simulation has already spawned the characters (its start()); this is the
  // local, presentation side of each.
  // FIXME inconsistency between starting at '1' or '0' for player characters is really confusing.
  for (unsigned int i = 1; i <= numPlayers; i++)
  {
    std::MickLogger::getInstance()->debug(this, std::string("initPlayerCharacters, i=").append(std::to_string(i)));

    if(m_pPlayers.size() < i)
    {
      m_pPlayers.resize(i);
    }
    m_pPlayers.at(i - 1) = m_sim.entityManager().getPlayerPointer(i);

    // Which pad or keyboard set drives each slot is seeded from the canonical menu
    // model by InputHandler::setPointers (it is the input layer's, not the entity's).

    // set image filename with "extension"
    stringstream ss;
    ss << "assets/bitmaps/player" << i;
    if (malePlayers->at(i-1) == true)
    {
      ss << "male.bmp";
    }
    else
    {
      ss << "female.bmp";
    }

    // set animation image for male or female
    EntityRenderer * er = EntityRendererFactory::getInstance()->getEntityRenderer(PLAYER_CHARACTER);
    er->replaceTeamSurfaceWithImage(m_surface, ss.str().c_str(), i);
  }

}


#ifdef BITRIOT_NETPLAY
// ---------------------------------------------------------------------------------------
// Network play (briefs 07 and 08)
// ---------------------------------------------------------------------------------------

void GameEngine::installNetInterceptor()
{
  // The scoreboard's buttons: only the host starts a round or goes back to the lobby, and leaving the scoreboard for
  // the main menu leaves the session. (The hook stays installed; it does nothing once m_net is gone. It is never cleared
  // from inside an event, which would destroy the function that is running.)
  EventManager::SetCommandInterceptor([this](const Rml::String& command) -> bool
  {
    if (!m_net)
    {
      return false;
    }
    if (command == "start")
    {
      if (m_net->isHost())
      {
        m_hostWantsRound = true;
        menuSystem->setScoreboardNet(true, "Starting...", false, false);
      }
      return true;
    }
    if (command == "netbacktolobby")
    {
      if (m_net->isHost())
      {
        m_hostWantsLobby = true;
        menuSystem->setScoreboardNet(true, "Next round", false, false);
      }
      return true;
    }
    if (command.rfind("open mainmenu", 0) == 0 || command == "exit")
    {
      stopNetplay();
      return false;   // and the manager carries on: the main menu, or quit
    }
    return false;
  });
}

bool GameEngine::startNetplay(const NetGame::Params& params, std::string* error)
{
  if (!menuSystem)
  {
    if (error) *error = "the menus are not up yet";
    return false;
  }
  NetGame::Params p = params;
  p.recordDir = m_recordDir;
  m_net = NetGame::create(p, m_sim, error);
  if (!m_net)
  {
    return false;
  }
  m_netRoundPending = false;
  m_netEndedShown = false;
  m_hostWantsRound = false;
  m_hostWantsLobby = false;
  m_netDesyncHandled = false;
  m_netRoomAnnounced = false;
  m_netMessage.clear();
  m_netToasts.clear();
  m_netOverlaySignature.clear();
  m_netStallTimer = netplay::StallTimer();
  m_netLocalErrorShown = false;
  m_netResolve.reset();
  m_netBackPage = p.host ? "mainmenu.rml" : "netjoin.rml";
  installNetInterceptor();

  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
  menuSystem->clearScoreBoard();
  menuSystem->showNetLobby();
  refreshNetPage();
  return true;
}

void GameEngine::stopNetplay()
{
  m_netResolve.reset();
  m_netLocalErrorShown = false;
  if (!m_net)
  {
    return;
  }
  m_net->restoreSettings();
  m_net.reset();   // tells the others we are gone
  m_netRoundPending = false;
  m_hostWantsRound = false;
  m_hostWantsLobby = false;
  m_netMessage.clear();
  m_netToasts.clear();
  m_netOverlaySignature.clear();
  if (menuSystem)
  {
    menuSystem->hideNetOverlay();
  }
}

void GameEngine::leaveNetplayToMenu(const std::string& page)
{
  stopNetplay();
  m_roundEnding = false;
  m_scoreboardWinner = nullptr;
  menuSystem->clearScoreBoard();
  menuSystem->showPage(page.empty() ? "mainmenu.rml" : page);
  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
  GameRunningSDLInput::getInstance()->resetInputEvents(); // the quit key that got us here
}

void GameEngine::showNetLocalError(const netplay::EndMessage& message, const std::string& backPage)
{
  m_netLocalError = message;
  m_netLocalErrorShown = true;
  m_netBackPage = backPage;
  menuSystem->clearScoreBoard();
  menuSystem->showNetLobby();
  if (Rml::ElementDocument* doc = menuSystem->netLobbyDocument())
  {
    NetLobbyBinder::applyEnd(doc, message, "Back");
  }
}

// Fills the lobby page for whatever the session is doing now.
void GameEngine::refreshNetPage()
{
  using netplay::LockstepSession;
  Rml::ElementDocument* doc = menuSystem->netLobbyDocument();
  if (!doc || !m_net)
  {
    return;
  }
  const LockstepSession& s = m_net->session();
  if (s.state() == LockstepSession::ENDED)
  {
    netplay::EndMessage m = m_net->endMessage();
    if (!m_netMessage.empty())
    {
      // A desync: the words and the files written, under the headline.
      m.title = "Out of sync";
      m.lines.clear();
      std::istringstream in(m_netMessage);
      std::string line;
      while (std::getline(in, line))
      {
        if (!line.empty()) m.lines.push_back(line);
      }
    }
    NetLobbyBinder::applyEnd(doc, m, m_net->isHost() ? "Leave" : "Back");
  }
  else if (s.state() == LockstepSession::CONNECTING)
  {
    if (m_net->viaRelay())
    {
      const bool relayUp = m_net->transportState() == VNET_STATUS_READY;
      NetLobbyBinder::applyConnecting(doc, "Joining room " + m_net->params().room + "...",
                                      relayUp ? std::string("In the room (") + m_net->transport() + "). Asking the host to let us in..."
                                              : "Connecting to the relay " + m_net->params().relayDisplay + "...");
    }
    else
    {
      NetLobbyBinder::applyConnecting(doc, "Connecting...", "Asking " + m_net->params().join + " to let us in...");
    }
  }
  else
  {
    NetLobbyBinder::applyLobby(doc, m_net->lobbyScreen());
  }
}

void GameEngine::adoptNetGame(std::unique_ptr<NetGame> net)
{
  m_net = std::move(net);
  m_netRoundPending = false;
  m_netEndedShown = false;
  m_hostWantsRound = false;
  m_hostWantsLobby = false;
  m_netDesyncHandled = false;
  m_netRoomAnnounced = false;
  m_netMessage.clear();
  m_netToasts.clear();
  m_netOverlaySignature.clear();
  m_netStallTimer = netplay::StallTimer();
  installNetInterceptor();
  menuSystem->showNetLobby();
  refreshNetPage();
}

bool GameEngine::startRelayFromText(bool host, const std::string& name, const std::string& relay, const std::string& room,
                                    int localPlayers, bool pageIsSecure, std::string* error)
{
  if (m_net || m_netResolve || !menuSystem || GameSettings::getInstance()->getGameState() != GameSettings::MENU_RUNNING)
  {
    if (error) *error = "a network game is already on, or the menus are not up";
    return false;
  }
  netplay::RelayTarget target;
  std::string code;
  if (!netplay::parseRelayAddress(relay, pageIsSecure, &target, error) || (!host && !netplay::parseRoomCode(room, &code, error)))
  {
    return false;
  }
  NetGame::Params p = m_netDev;
  p.relay = true;
  p.host = host;
  p.name = netplay::cleanPlayerName(name);
  p.localPlayers = localPlayers;
  p.requireReady = true;
  p.recordDir = m_recordDir;
  p.relayWs = target.ws;
  p.relayUdp = target.udp;
  p.relayDisplay = relay;
  p.room = code;
  p.join = relay;
  startRelayGame(p);
  return true;
}

// Starts a game through the relay in `params` (a room of our own, or the one params.room names). A failure that comes
// before there is a session is an error page; one that comes later is the session's own ending.
void GameEngine::startRelayGame(const NetGame::Params& params)
{
  const std::string backPage = params.host ? "nethostrelay.rml" : "netjoinrelay.rml";
  std::string error;
  if (params.host)
  {
    if (startNetplay(params, &error))
    {
      m_netBackPage = backPage;
    }
    else
    {
      netplay::EndMessage m;
      m.title = "Cannot open a room";
      m.lines.push_back(error);
      showNetLocalError(m, backPage);
    }
    return;
  }
  std::unique_ptr<NetGame> net = NetGame::create(params, m_sim, &error);
  if (!net)
  {
    netplay::EndMessage m;
    m.title = "Cannot start the network";
    m.lines.push_back(error);
    showNetLocalError(m, backPage);
    return;
  }
  m_netBackPage = backPage;
  adoptNetGame(std::move(net));
}

// The menus' network flow when there is no session yet: the host and join forms, a host name being looked up, a failure.
void GameEngine::netIdleFrame()
{
  NetUiRequests& rq = EventHandlerNetLobby::requests();
  if (!rq.hostGo && !rq.joinGo && !rq.relayHostGo && !rq.relayJoinGo && !rq.leave && !m_netResolve && !m_netLocalErrorShown)
  {
    return;
  }
  if (GameSettings::getInstance()->getGameState() != GameSettings::MENU_RUNNING || !menuSystem)
  {
    rq = NetUiRequests();
    return;
  }

  if (rq.leave)
  {
    // Cancel while looking up, or Back on the error page.
    rq.leave = false;
    const bool wasErrorPage = m_netLocalErrorShown;
    m_netResolve.reset();
    m_netResolveForRelay = false;
    m_netLocalErrorShown = false;
    menuSystem->showPage(wasErrorPage || m_netBackPage != "mainmenu.rml" ? m_netBackPage : std::string("mainmenu.rml"));
    return;
  }
  if (rq.relayHostGo || rq.relayJoinGo)
  {
    // Host via relay / Join by room code (brief 12): the same flow as the direct forms, with the relay's address instead of the
    // host's. A native build looks the relay's name up on a worker thread first; a browser has no use for a number.
    const bool hosting = rq.relayHostGo;
    rq.relayHostGo = rq.relayJoinGo = false;
    NetGame::Params p = m_netDev;
    p.relay = true;
    p.host = hosting;
    p.name = rq.form.name;
    p.localPlayers = rq.form.localPlayers;
    p.requireReady = true;
    p.recordDir = m_recordDir;
    p.relayWs = rq.form.relayWs;
    p.relayUdp = rq.form.relayUdp;
    p.relayDisplay = rq.form.relay;
    p.room = rq.form.room;
    p.join = rq.form.relay;   // what the words of an error name
    EventHandlerNetForm::rememberRelay(rq.form.name, rq.form.relay, rq.form.room, rq.form.localPlayers);
    m_netBackPage = hosting ? "nethostrelay.rml" : "netjoinrelay.rml";
    m_netLocalErrorShown = false;
#ifdef __EMSCRIPTEN__
    startRelayGame(p);
#else
    vnet_addr numeric;
    if (vnet_resolve(nullptr, p.relayUdp.c_str(), &numeric) == 0)
    {
      startRelayGame(p);   // already a number
    }
    else
    {
      m_netJoinParams = p;
      m_netResolveForRelay = true;
      m_netResolve.reset(new NetResolve(p.relayUdp));
      menuSystem->showNetLobby();
      if (Rml::ElementDocument* doc = menuSystem->netLobbyDocument())
      {
        NetLobbyBinder::applyConnecting(doc, "Connecting...", "Looking up the relay " + p.relayDisplay + "...");
      }
    }
#endif
    return;
  }
  if (rq.hostGo)
  {
    rq.hostGo = false;
    NetGame::Params p;
    p.host = true;
    p.port = (uint16_t) rq.form.port;
    p.name = rq.form.name;
    p.localPlayers = rq.form.localPlayers;
    p.requireReady = true;
    std::string error;
    if (startNetplay(p, &error))
    {
      EventHandlerNetForm::rememberHost(rq.form.name, rq.form.port, rq.form.localPlayers);
    }
    else
    {
      menuSystem->setFormError(error);
    }
    return;
  }
  if (rq.joinGo)
  {
    rq.joinGo = false;
    m_netJoinParams = NetGame::Params();
    m_netJoinParams.host = false;
    m_netJoinParams.join = rq.form.address;
    m_netJoinParams.name = rq.form.name;
    m_netJoinParams.localPlayers = rq.form.localPlayers;
    m_netJoinParams.requireReady = true;
    m_netJoinParams.recordDir = m_recordDir;
    EventHandlerNetForm::rememberJoin(rq.form.name, rq.form.address, rq.form.localPlayers);
    m_netResolve.reset(new NetResolve(rq.form.address));   // on a worker thread: the menu keeps drawing
    m_netBackPage = "netjoin.rml";
    m_netLocalErrorShown = false;
    menuSystem->showNetLobby();
    if (Rml::ElementDocument* doc = menuSystem->netLobbyDocument())
    {
      NetLobbyBinder::applyConnecting(doc, "Connecting...", "Looking up " + rq.form.address + "...");
    }
    return;
  }
  if (m_netResolve && m_netResolve->done())
  {
    const int rc = m_netResolve->result();
    const vnet_addr addr = m_netResolve->address();
    const std::string shown = m_netResolve->hostPort();
    m_netResolve.reset();
    std::string error;
    if (rc == 0)
    {
      if (m_netResolveForRelay)
      {
        // The relay's name is now a number: start the game the form asked for.
        m_netResolveForRelay = false;
        char numeric[128];
        NetGame::Params p = m_netJoinParams;
        if (vnet_addr_to_string(&addr, numeric, sizeof(numeric)) > 0)
        {
          p.relayUdp = numeric;
        }
        startRelayGame(p);
        return;
      }
      std::unique_ptr<NetGame> net = NetGame::createResolved(m_netJoinParams, m_sim, addr, &error);
      if (net)
      {
        adoptNetGame(std::move(net));
        return;
      }
      netplay::EndMessage m;
      m.title = "Cannot start the network";
      m.lines.push_back(error);
      showNetLocalError(m, "netjoin.rml");
      return;
    }
    netplay::EndMessage m;
    if (m_netResolveForRelay)
    {
      m_netResolveForRelay = false;
      m.title = "Cannot find the relay";
      m.lines.push_back("'" + shown + "' could not be looked up: " + vnet_strerror(rc) + ".");
      m.lines.push_back("Check the relay's address and your connection.");
      showNetLocalError(m, m_netJoinParams.host ? "nethostrelay.rml" : "netjoinrelay.rml");
      return;
    }
    m.title = "Cannot find the host";
    m.lines.push_back("'" + shown + "' could not be looked up: " + vnet_strerror(rc) + ".");
    m.lines.push_back("Check the spelling and your connection, or use the host's IP address.");
    showNetLocalError(m, "netjoin.rml");
  }
}

void GameEngine::netFrame()
{
  using netplay::LockstepSession;
  GameSettings* gs = GameSettings::getInstance();
  m_net->pump();
  LockstepSession& s = m_net->session();
  if (m_net->viaRelay() && !m_netRoomAnnounced)
  {
    const std::string code = m_net->roomCode();
    if (!code.empty())
    {
      // Where a player running the game from a terminal (and the tests) can read it.
      m_netRoomAnnounced = true;
      std::cout << "room " << code << " (" << m_net->transport() << ")" << std::endl;
    }
  }
  NetUiRequests& rq = EventHandlerNetLobby::requests();
  rq.hostGo = false;
  rq.joinGo = false;

  // Leave: the page's button (Leave, or Back on the error page), or the quit key in a round. A peer's slots turn
  // into bots on the other machines; a host that leaves ends the session for everybody.
  if (rq.leave)
  {
    rq.leave = false;
    // The error page of a join that failed goes back to the join form; every other leave goes to the main menu.
    // Cancel while the session is still asking the host (a numeric address) is the same Cancel as during the name lookup.
    const bool failedJoin = !m_net->isHost()
                            && ((s.state() == LockstepSession::ENDED && s.endReason() != LockstepSession::END_LEFT && m_netEndedShown)
                                || s.state() == LockstepSession::CONNECTING);
    // A relay host whose room could not be made goes back to its form; other hosts to the main menu.
    const bool failedRelayHost = m_net->isHost() && m_net->viaRelay() && s.state() == LockstepSession::ENDED
                                 && s.endReason() == LockstepSession::END_NETWORK && m_netEndedShown;
    const bool relay = m_net->viaRelay();
    leaveNetplayToMenu(failedJoin ? (relay ? "netjoinrelay.rml" : "netjoin.rml") : (failedRelayHost ? "nethostrelay.rml" : "mainmenu.rml"));
    return;
  }
  if (gs->getGameState() == GameSettings::GAME_RUNNING && !m_roundEnding && m_pInputHandler->quitRequested())
  {
    leaveNetplayToMenu("mainmenu.rml");
    return;
  }

  // A desync: keep the evidence, give the others a moment to be told, then stop.
  if (s.desynced() && !m_netDesyncHandled && (s.state() == LockstepSession::RUNNING || s.state() == LockstepSession::BETWEEN_ROUNDS))
  {
    m_netDesyncHandled = true;
    netplay::NetMatch::DesyncFiles files;
    std::string error;
    const netplay::DesyncInfo& d = s.desyncInfo();
    std::ostringstream msg;
    msg << "The game state is not the same on every machine (round " << d.round << ", tick " << d.tick << "; machine "
        << (int) d.offender << " differs from the host).\n";
    if (m_net->match().writeDesyncFiles(files, &error))
    {
      msg << "Desync at tick " << d.tick << " - dumps written to:\n  "
          << (files.dumpAtTick.empty() ? std::string("(state at that tick: no longer kept)") : files.dumpAtTick)
          << "\n  " << files.dumpNow << "\n  " << files.record;
    }
    else
    {
      msg << "The evidence could not be written: " << error;
    }
    m_netMessage = msg.str();
    std::MickLogger::getInstance()->error(this, m_netMessage);
    m_netDesyncDeadline = SDL_GetTicks() + 1500;
  }
  if (m_netDesyncHandled && (s.state() == LockstepSession::RUNNING || s.state() == LockstepSession::BETWEEN_ROUNDS) && SDL_GetTicks() >= m_netDesyncDeadline)
  {
    s.leave(netplay::LEAVE_DESYNC);   // the DESYNC has had time to reach everybody
  }

  // The session is over (the host went, we were dropped or removed, a desync ended it): say so, on a page.
  if (s.ended())
  {
    if (!m_netEndedShown)
    {
      m_netEndedShown = true;
      m_roundEnding = false;
      m_scoreboardWinner = nullptr;
      m_hostWantsRound = false;
      m_hostWantsLobby = false;
      menuSystem->hideNetOverlay();
      menuSystem->clearScoreBoard();
      gs->setGameState(GameSettings::MENU_RUNNING);
      menuSystem->showNetLobby();
    }
    refreshNetPage();
    return;
  }

  // A round begins (the host's START, or our own as the host): build it next.
  netplay::RoundStart rs;
  if (s.takeRoundStart(rs))
  {
    m_netRound = rs;
    m_netRoundPending = true;
    m_hostWantsRound = false;
    m_hostWantsLobby = false;
    m_netDesyncHandled = false;
    m_netMessage.clear();
    m_netToasts.clear();
    m_netOverlaySignature.clear();
    m_netStallTimer = netplay::StallTimer();
    m_roundEnding = false;
    menuSystem->closeAllMenus();
    gs->setGameState(GameSettings::GAME_INIT);
  }

  if (rq.copyRoom)
  {
    rq.copyRoom = false;
    const std::string code = m_net->roomCode();
    if (!code.empty())
    {
      SDL_SetClipboardText(code.c_str());
#ifdef __EMSCRIPTEN__
      webCopyToClipboard(code);   // the page's clipboard, which SDL's clipboard does not reach from a click
#endif
    }
  }

  // The lobby's buttons.
  if (rq.startMatch)
  {
    rq.startMatch = false;
    if (m_net->isHost() && s.state() == LockstepSession::LOBBY)
    {
      std::string error;
      if (m_net->startRound(&error))
      {
        m_netMessage.clear();
      }
      else
      {
        std::MickLogger::getInstance()->info(this, std::string("Start refused: ").append(error));
      }
    }
    else if (!m_net->isHost() && s.state() == LockstepSession::LOBBY && s.lobby().phase == netplay::PHASE_LOBBY)
    {
      s.setReady(!s.wantsReady());
    }
  }
  if (m_net->isHost() && s.state() == LockstepSession::LOBBY)
  {
    if (rq.setting >= 0)
    {
      m_net->cycleSetting((NetGame::HostSetting) rq.setting);
    }
    if (rq.teamSlot >= 0)
    {
      m_net->cycleTeam(rq.teamSlot);
    }
    if (rq.kickSlot >= 0)
    {
      const netplay::LobbyView& v = s.lobby();
      if (rq.kickSlot < (int) v.slots.size() && v.slots[rq.kickSlot].owner != netplay::NO_PEER && v.slots[rq.kickSlot].owner != netplay::HOST_ID)
      {
        m_net->kick(v.slots[rq.kickSlot].owner);
      }
    }
  }
  rq.setting = -1;
  rq.teamSlot = -1;
  rq.kickSlot = -1;

  // The scoreboard's host buttons, once every peer holds the last tick.
  if (m_hostWantsRound && s.state() == LockstepSession::BETWEEN_ROUNDS && s.canStartRound())
  {
    m_hostWantsRound = false;
    std::string error;
    if (!m_net->startRound(&error))
    {
      std::MickLogger::getInstance()->error(this, error);
      menuSystem->setScoreboardNet(true, "Cannot start: " + error, true, true);
    }
  }
  if (m_hostWantsLobby && s.state() == LockstepSession::BETWEEN_ROUNDS && s.canStartRound())
  {
    m_hostWantsLobby = false;
    if (m_net->returnToLobby())
    {
      menuSystem->clearScoreBoard();
      menuSystem->showNetLobby();
    }
  }

  // The lobby page: shown whenever the session is in its lobby and no round or scoreboard is up (a peer that has finished
  // the scoreboard follows the host back to the lobby here).
  if (gs->getGameState() == GameSettings::MENU_RUNNING && (s.state() == LockstepSession::LOBBY || s.state() == LockstepSession::CONNECTING))
  {
    if (!menuSystem->netLobbyOpen())
    {
      menuSystem->clearScoreBoard();
      menuSystem->showNetLobby();
    }
    refreshNetPage();
  }

  updateNetOverlay();
}

// The overlay over a running round: who the match waits for (after half a second), who left, a desync, the connection.
void GameEngine::updateNetOverlay()
{
  GameSettings* gs = GameSettings::getInstance();
  if (gs->getGameState() != GameSettings::GAME_RUNNING || m_roundEnding)
  {
    return;
  }
  const uint32_t now = SDL_GetTicks();
  while (!m_netToasts.empty() && (int32_t) (now - m_netToasts.front().second) >= 0)
  {
    m_netToasts.pop_front();
  }
  MenuSDLRenderer::NetOverlay o;
  o.stall = netplay::stallSentence(m_netStallTimer.update(m_net->stallText(), now));
  for (const auto& t : m_netToasts)
  {
    o.toasts.push_back(t.first);
  }
  if (m_net->session().desynced())
  {
    o.desync = m_netMessage.empty() ? std::string("Desync detected - writing the evidence...") : m_netMessage;
  }
  o.status = m_net->statusLine();
  std::string signature = o.stall + "\n" + o.desync + "\n" + o.status + "\n";
  for (const std::string& t : o.toasts) signature += t + "\n";
  if (signature != m_netOverlaySignature)
  {
    m_netOverlaySignature = signature;
    menuSystem->setNetOverlay(o);
  }
}

void GameEngine::stepNetTick()
{
  netplay::NetMatch& match = m_net->match();
  const uint8_t mask = (uint8_t) m_pInputHandler->localSlotMask();
  // Who left in this tick (every machine sees the same events in the same tick): the overlay says so.
  if (match.canStep())
  {
    for (const ControlEvent& e : m_net->session().stepInputs().control)
    {
      if (e.slot < 0 || e.slot >= 6) continue;
      const std::string toast = netplay::eventToast(e, m_netSlotNames[e.slot]);
      if (!toast.empty())
      {
        m_netToasts.push_back(std::make_pair(toast, SDL_GetTicks() + 6000u));
        while (m_netToasts.size() > 3) m_netToasts.pop_front();
      }
      m_netSlotNames[e.slot].clear();   // a bot now, or a player we cannot name
    }
  }
  // What this machine's players did since the last tick: handed to the session, which
  // gets it to the host; the tick itself steps with the host's confirmed inputs.
  const TickInputs sample = m_pInputHandler->sampleTick();
  match.step(sample, mask);
  m_ticksThisFrame++;
  if (match.lastCheckpointTick() != m_netLoggedCheckpoint)
  {
    m_netLoggedCheckpoint = match.lastCheckpointTick();
    char hash[32];
    std::snprintf(hash, sizeof(hash), "0x%016llx", (unsigned long long) match.lastCheckpointHash());
    logNet("checkpoint " + std::to_string(match.round().round) + " " + std::to_string(m_netLoggedCheckpoint) + " " + hash);
  }
  // A takeover the host did not grant (someone else took that bot at the same moment).
  for (const int slot : m_net->session().takeRejectedClaims())
  {
    m_pInputHandler->revokeSlot(slot);
  }
  m_pInputHandler->rumbleForHits(m_sim);
  if (m_sim.roundOver())
  {
    m_roundEnding = true;
    m_scoreboardWinner = m_sim.winner();
    {
      char hash[32];
      std::snprintf(hash, sizeof(hash), "0x%016llx", (unsigned long long) m_sim.stateHash());
      logNet("round " + std::to_string(match.round().round) + ": over at tick " + std::to_string(m_sim.tick()) + ", winner "
             + (m_scoreboardWinner ? "player " + std::to_string(m_scoreboardWinner->getPlayerNumber()) : std::string("none"))
             + ", final hash " + hash);
    }
    if (!m_recordDir.empty())
    {
      std::string path, error;
      if (match.writeRoundRecord(&path, &error))
      {
        std::MickLogger::getInstance()->info(this, std::string("Match record written: ").append(path));
      }
      else
      {
        std::MickLogger::getInstance()->error(this, error);
      }
    }
  }
}

void GameEngine::logNet(const std::string& line)
{
  m_netLog += line;
  m_netLog += '\n';
  if (m_netLog.size() > 65536)
  {
    const size_t cut = m_netLog.find('\n', m_netLog.size() - 32768);
    m_netLog.erase(0, cut == std::string::npos ? m_netLog.size() : cut + 1);
  }
}

void GameEngine::resetNetGame()
{
  m_netLoggedCheckpoint = 0;
  GameSettings* settings = GameSettings::getInstance();
  const netplay::RoundStart rs = m_netRound;
  m_netRoundPending = false;

  if (m_pPanel)
  {
    delete m_pPanel;
    m_pPanel = nullptr;
  }
  finishRecording();
  std::MickLogger::getInstance()->info(this, std::string("Network round ").append(std::to_string(rs.round))
    .append(", seed=").append(std::to_string(rs.config.seed)).append(", delay=").append(std::to_string(rs.delay)));

  // Builds the Simulation from the host's MatchConfig; that overwrites GameSettings (the
  // roster, the map), which NetGame saved and puts back when the game is left.
  m_net->match().beginRound(rs);
  resizeWindowIfMapChanged();

  // The names the overlay uses for "<name> left - now AI".
  for (int i = 0; i < 6; i++)
  {
    m_netSlotNames[i].clear();
    if (i < (int) rs.owner.size() && rs.owner[i] != netplay::NO_PEER)
    {
      for (const netplay::LobbyEntry& e : m_net->session().lobby().peers)
      {
        if (e.id == rs.owner[i]) m_netSlotNames[i] = e.name;
      }
    }
  }

  const int numPlayers = settings->getNumberOfPlayers();
  std::vector<bool>* genders = settings->getPlayerGenders();
  // Which keyboard set or pad a slot had in the menu means nothing here: the menu's indexes are
  // not the host's slot numbers, and a pad chosen for slot 2 there may not even be plugged in.
  // Devices are assigned deliberately instead: every slot starts with none, and this machine's
  // players (in slot order) get the keyboard sets 1, 2, ... (setPointers seeds the lowest free
  // set for a human with no device). A pad's START binds it to one of this machine's own slots (lowest without a pad; the keyboard set
  // is released), and only then to a bot (InputHandler::setPadsMayBindOwnSlots).
  for (int i = 0; i < numPlayers; i++)
  {
    settings->setPlayerKeyboard(i, -1);
    settings->setPlayerController(i, -1);
  }

  m_pPlayers.resize(numPlayers);
  m_pPanel = new InfoPanel(m_surface, numPlayers, genders);
  m_pPanel->setSimulation(&m_sim);
  GameRunningSDLInput::getInstance()->resetInputEvents();
  initPlayerCharacters(numPlayers, genders);
  m_pInputHandler->setPointers(m_pPlayers, m_pPanel, rs.localSlotMask);
  m_pInputHandler->setPadsMayBindOwnSlots(true);
  m_pPanel->setPlayerPointers(m_pPlayers);
  m_drawNeeded = true;
}
#endif // BITRIOT_NETPLAY
