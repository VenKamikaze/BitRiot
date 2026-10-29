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
#include <cstddef>
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
  // hooks.canStep stays empty: locally a tick is always allowed. Netplay (brief 07)
  // sets it to hold a tick until the host's inputs for it have arrived.
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
}

void GameEngine::stepTick()
{
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
