#include "GameEngine.h"
#include "SimRandom.h"
#include "GameSettings.h"
#include "MickBaseInput.h"
#include "GameRunningSDLInput.h"
#include "MickSDLRenderer.h"
#include "PlayerCharacterEntity.h"
#include <cstddef>
#include <string>

GameEngine::GameEngine(SDL_Surface* back, shared_ptr<InputHandler> inputHandler)
{
  m_pInputHandler = inputHandler;

  m_surface = back;

  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
}

GameEngine::~GameEngine()
{
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

void GameEngine::applyMapDimensionsIfChanged()
{
  int newWidth = GameSettings::getInstance()->getMapWidth();
  int newHeight = GameSettings::getInstance()->getMapHeight();

  if (Map::MAP_WIDTH == newWidth && Map::MAP_HEIGHT == newHeight)
  {
    return; // nothing to do
  }

  // GameSettings is canonical; the Map statics are derived from it.
  Map::MAP_WIDTH = newWidth;
  Map::MAP_HEIGHT = newHeight;

  // Resize the startup-once resources (window, back-buffer surface, texture) to the
  // new content size. InfoPanel::WIDTH is the fixed side-panel width.
  int contentWidth = (newWidth * Map::TILE_WIDTH) + InfoPanel::WIDTH;
  int contentHeight = newHeight * Map::TILE_HEIGHT;

  MickSDLRenderer* renderer = MickSDLRenderer::getInstance();
  renderer->resize(contentWidth, contentHeight);

  // The old back buffer was freed in resize(); re-point our handle.
  m_surface = renderer->getSurfaceBackBufferHandle();

  // Rebuild the static map (walls/pillars) at the new size against the new surface.
  Map::getInstance()->init(m_surface);
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
  m_sim.start(config);

  // after setting initialisation parameters
  m_pPlayers.resize(numPlayers);

  m_pPanel = new InfoPanel(m_surface, numPlayers, genders);
  GameRunningSDLInput::getInstance()->resetInputEvents();
  initPlayerCharacters(numPlayers, genders);
  m_pInputHandler->setPointers(m_pPlayers, m_sim.entityManager().getDynamicMap(), m_pPanel);
  m_pPanel->setPlayerPointers(m_pPlayers);
}

int GameEngine::gameLengthSeconds() const
{
  return (int) ((m_sim.tick() * GameSettings::NORMAL_TICKS_SPEED) / 1000);
}

bool GameEngine::runEngine()
{
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
      applyMapDimensionsIfChanged();
      resetGame();
      GameSettings::getInstance()->setGameState(GameSettings::GAME_RUNNING);
      break;
    }
    case GameSettings::GAME_RUNNING:
    {
      // One sim tick. Both the game-over countdown and the scoreboard's game
      // length count ticks, never wall-clock time.

      // Human input is applied to the player entities before the step. (Brief
      // 04a turns this into step(inputs).) ESC ends the round on this machine
      // only: the rest of the tick still runs, as it always has.
      bool quitRequested = false;
      std::shared_ptr<PlayerCharacterEntity> winnerAtQuit;
      try
      {
        m_pInputHandler->processKeyboardInput();
      }
      catch(const InputException &e)
      {
        if(e.gotQuit())
        {
          quitRequested = true;
          winnerAtQuit = m_sim.currentWinner();
        }
      }

      // The win check and game-over countdown (a timed delay during which we
      // continue to update and runFrame for a few seconds; useful when all
      // human players are dead but AI is still alive, in case a human player
      // wants to take over the AI), the spawning pool and runFrame. The round
      // is over only once this whole step has run.
      m_sim.step();

      if (m_sim.roundOver())
      {
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
        menuSystem->loadScoreBoard(m_sim.winner(), m_sim.roster(), gameLengthSeconds());
      }
      else if (quitRequested)
      {
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
        menuSystem->loadScoreBoard(winnerAtQuit, m_sim.roster(), gameLengthSeconds());
      }

      m_sim.entityManager().renderEntities(m_surface);

      m_pPanel->renderSurfaceTo(m_surface, (GameSettings::getInstance()->getMapWidth() * Map::TILE_WIDTH), 0); 
      
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

    // Phase 2.3: seed the live entity from the canonical menu-intent model.
    // Entities are 1-based (uniqueID == i); the canonical model is 0-based, so
    // player i maps to getPlayer(i - 1). For keyboard-only play this writes the
    // default -1 (no-op); the menu controller-join choice only becomes visible
    // here once a gamepad is attached. NOTE: until 2.4 switches InputHandler to
    // keyboard-by-id, a controller player is still non-AI so setPointers also
    // pops a keyset for them -> they respond to both pad and keyboard. Benign
    // (distinct key sets, no collision); 2.4 closes it.
    Player* setup = GameSettings::getInstance()->getPlayer(i - 1);
    m_pPlayers.at(i - 1)->attachedController = setup->attachedControllerId;

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
