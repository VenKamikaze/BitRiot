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
#include "RmlUI/events/EventHandlerNetLobby.h"
#include "RmlUI/events/EventManager.h"
#include <SDL2/SDL_ttf.h>
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
  hooks.extraTicks = [this] { return m_net ? m_net->session().extraTicksWanted() : 0; };
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
        menuSystem->loadScoreBoard(m_scoreboardWinner, m_sim.roster(), gameLengthSeconds());
        m_scoreboardWinner = nullptr;
#ifdef BITRIOT_NETPLAY
        if (m_net && m_net->session().state() != netplay::LockstepSession::ENDED)
        {
          // Only the host starts the next round (its START is what every machine waits for).
          menuSystem->setScoreboardPlayAgain(m_net->isHost() ? "Play Again" : "Waiting for the host to play again...", m_net->isHost());
          m_hostWantsRound = false;
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
      const bool redraw = m_drawNeeded || m_endingThisFrame || m_ticksThisFrame > 0 || !m_pumpedQuietly;
      m_drawNeeded = false;
      m_endingThisFrame = false;
      if (redraw)
      {
        WorldRenderer::renderWorld(m_sim.entityManager(), m_surface);

        m_pPanel->renderSurfaceTo(m_surface, (GameSettings::getInstance()->getMapWidth() * Map::TILE_WIDTH), 0);
#ifdef BITRIOT_NETPLAY
        if (m_net && !m_netLastStall.empty())
        {
          drawNetOverlay(m_netLastStall);
        }
#endif
      }

      MickSDLRenderer::getInstance()->pushCpuBufferToHardwareBuffer();
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
// Network play (brief 07)
// ---------------------------------------------------------------------------------------

namespace
{
const char* const BASE_TITLE = "Bit Riot Beta";
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
  m_netDesyncHandled = false;
  m_netMessage.clear();
  m_netLastPage.clear();
  m_netLastStall.clear();

  // The scoreboard's buttons: only the host starts a round, and leaving the scoreboard for
  // the main menu leaves the session. (The hook stays installed; it does nothing once
  // m_net is gone. It is never cleared from inside an event, which would destroy the
  // function that is running.)
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
        menuSystem->setScoreboardPlayAgain("Starting...", false);
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

  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
  menuSystem->showNetLobby();
  showNetPage();
  return true;
}

void GameEngine::stopNetplay()
{
  if (!m_net)
  {
    return;
  }
  m_net->restoreSettings();
  m_net.reset();   // tells the others we are gone
  m_netRoundPending = false;
  m_hostWantsRound = false;
  m_netMessage.clear();
  m_netLastStall.clear();
  if (MickSDLRenderer* r = MickSDLRenderer::getInstanceIfCreated())
  {
    SDL_SetWindowTitle(r->getWindowHandle(), BASE_TITLE);
  }
}

void GameEngine::leaveNetplayToMenu(const std::string&)
{
  stopNetplay();
  m_roundEnding = false;
  m_scoreboardWinner = nullptr;
  menuSystem->clearScoreBoard();
  menuSystem->showMainMenu();
  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
  GameRunningSDLInput::getInstance()->resetInputEvents(); // the quit key that got us here
}

void GameEngine::showNetPage()
{
  using netplay::LockstepSession;
  const LockstepSession& s = m_net->session();
  std::string text = m_net->lobbyText();
  if (!m_netMessage.empty())
  {
    text = (s.state() == LockstepSession::ENDED ? m_netMessage + "\n\nPress Leave to go back to the menu." : text + "\n\n" + m_netMessage);
  }
  const bool showStart = m_net->isHost() && s.state() == LockstepSession::LOBBY;
  const std::string title = m_net->lobbyTitle();
  const std::string page = title + "\n" + text + (showStart ? "\nS" : "\n-");
  if (page != m_netLastPage)
  {
    m_netLastPage = page;
    menuSystem->updateNetLobby(title, text, showStart, "Start match");
  }
}

void GameEngine::netFrame()
{
  using netplay::LockstepSession;
  GameSettings* gs = GameSettings::getInstance();
  m_net->pump();
  LockstepSession& s = m_net->session();
  NetUiRequests& rq = EventHandlerNetLobby::requests();

  // Leave: the page's button, or the quit key in a round. A peer's slots turn into bots on
  // the other machines; a host that leaves ends the session for everybody.
  if (rq.leave)
  {
    rq.leave = false;
    leaveNetplayToMenu("");
    return;
  }
  if (gs->getGameState() == GameSettings::GAME_RUNNING && !m_roundEnding && m_pInputHandler->quitRequested())
  {
    leaveNetplayToMenu("");
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
      msg << "Written here, for the bug report:\n  " << (files.dumpAtTick.empty() ? std::string("(state at that tick: no longer kept)") : files.dumpAtTick)
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

  // The session is over (the host went, we were dropped, a desync ended it): say so, on a page.
  if (s.ended())
  {
    if (!m_netEndedShown)
    {
      m_netEndedShown = true;
      m_roundEnding = false;
      m_scoreboardWinner = nullptr;
      m_hostWantsRound = false;
      m_netLastStall.clear();
      menuSystem->clearScoreBoard();
      gs->setGameState(GameSettings::MENU_RUNNING);
      menuSystem->showNetLobby();
      m_netLastPage.clear();
      if (MickSDLRenderer* r = MickSDLRenderer::getInstanceIfCreated())
      {
        SDL_SetWindowTitle(r->getWindowHandle(), BASE_TITLE);
      }
    }
    showNetPage();
    return;
  }

  // A round begins (the host's START, or our own as the host): build it next.
  netplay::RoundStart rs;
  if (s.takeRoundStart(rs))
  {
    m_netRound = rs;
    m_netRoundPending = true;
    m_hostWantsRound = false;
    m_netDesyncHandled = false;
    m_netMessage.clear();
    m_netLastStall.clear();
    m_roundEnding = false;
    menuSystem->closeAllMenus();
    gs->setGameState(GameSettings::GAME_INIT);
  }

  // The host's buttons.
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
        m_netMessage = error;
      }
    }
  }
  if (m_hostWantsRound && s.state() == LockstepSession::BETWEEN_ROUNDS && s.canStartRound())
  {
    m_hostWantsRound = false;
    std::string error;
    if (!m_net->startRound(&error))
    {
      std::MickLogger::getInstance()->error(this, error);
      menuSystem->setScoreboardPlayAgain("Cannot start: " + error, true);
    }
  }

  if (gs->getGameState() == GameSettings::MENU_RUNNING && (s.state() == LockstepSession::LOBBY || s.state() == LockstepSession::CONNECTING))
  {
    if (!menuSystem->netLobbyOpen())
    {
      menuSystem->showNetLobby();
      m_netLastPage.clear();
    }
    showNetPage();
  }

  // What the title bar and the stall overlay say.
  const uint32_t now = SDL_GetTicks();
  if (now - m_netLastTitleAt >= 250)
  {
    m_netLastTitleAt = now;
    const std::string title = std::string(BASE_TITLE) + " - " + m_net->statusLine();
    if (title != m_netLastTitle)
    {
      m_netLastTitle = title;
      if (MickSDLRenderer* r = MickSDLRenderer::getInstanceIfCreated())
      {
        SDL_SetWindowTitle(r->getWindowHandle(), title.c_str());
      }
    }
  }
  const std::string stall = gs->getGameState() == GameSettings::GAME_RUNNING && !m_roundEnding ? m_net->stallText() : std::string();
  if (stall != m_netLastStall)
  {
    m_netLastStall = stall;
    requestRedraw();   // the overlay is painted onto the world's picture
  }
}

void GameEngine::stepNetTick()
{
  netplay::NetMatch& match = m_net->match();
  const uint8_t mask = (uint8_t) m_pInputHandler->localSlotMask();
  // What this machine's players did since the last tick: handed to the session, which
  // gets it to the host; the tick itself steps with the host's confirmed inputs.
  const TickInputs sample = m_pInputHandler->sampleTick();
  match.step(sample, mask);
  m_ticksThisFrame++;
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

void GameEngine::resetNetGame()
{
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

  const int numPlayers = settings->getNumberOfPlayers();
  std::vector<bool>* genders = settings->getPlayerGenders();
  // Which keyboard set or pad a slot had in the menu means nothing here: the slots are
  // the host's. This machine's players get the keyboard sets in slot order, and the others
  // nothing (a human on another machine is not read here).
  for (int i = 0; i < numPlayers; i++)
  {
    settings->setPlayerKeyboard(i, -1);
    if (!(rs.localSlotMask & (1u << i)))
    {
      settings->setPlayerController(i, -1);
    }
  }

  m_pPlayers.resize(numPlayers);
  m_pPanel = new InfoPanel(m_surface, numPlayers, genders);
  m_pPanel->setSimulation(&m_sim);
  GameRunningSDLInput::getInstance()->resetInputEvents();
  initPlayerCharacters(numPlayers, genders);
  m_pInputHandler->setPointers(m_pPlayers, m_pPanel, rs.localSlotMask);
  m_pPanel->setPlayerPointers(m_pPlayers);
  m_drawNeeded = true;
}

void GameEngine::drawNetOverlay(const std::string& text)
{
  static TTF_Font* font = TTF_OpenFont("assets/fonts/FreeMono.ttf", 16);
  if (!font || text.empty())
  {
    return;
  }
  SDL_Color white = { 255, 255, 255, 255 };
  SDL_Surface* message = TTF_RenderText_Solid(font, text.c_str(), white);
  if (!message)
  {
    return;
  }
  SDL_Rect box = { 4, 4, message->w + 12, message->h + 8 };
  SDL_FillRect(m_surface, &box, SDL_MapRGB(m_surface->format, 150, 0, 0));
  SDL_Rect dest = { 10, 8, message->w, message->h };
  SDL_BlitSurface(message, NULL, m_surface, &dest);
  SDL_FreeSurface(message);
}
#endif // BITRIOT_NETPLAY
