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
  m_pEntityManager = nullptr;
  m_pSpawningPool = nullptr;
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
  if (m_pEntityManager)
  {
    delete m_pEntityManager;
    m_pEntityManager = NULL;
  }
  if (m_pSpawningPool)
  {
    delete m_pSpawningPool;
    m_pSpawningPool = NULL;
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
  // Must stay the very first thing here: the SpawningPool and player
  // constructors below draw from the sim RNG, and the same seed has to give the
  // same round on every machine.
  const uint64_t roundSeed = GameSettings::getInstance()->beginRoundSeed();
  std::MickLogger::getInstance()->info(this, std::string("Round seed=").append(std::to_string(roundSeed)));

  int numHumanPlayers = GameSettings::getInstance()->getNumberOfHumanPlayers();
  int numPlayers =  GameSettings::getInstance()->getNumberOfPlayers();
  int blockPercentage =  GameSettings::getInstance()->getBlockSpawnPercentage();
  std::vector<bool>* genders = GameSettings::getInstance()->getPlayerGenders();
  std::vector<bool>* playerAIs = GameSettings::getInstance()->getPlayerAIs();

  std::MickLogger::getInstance()->debug(this, std::string("numHumanPlayers=").append(std::to_string(numHumanPlayers)).append(",numPlayers=").append(std::to_string(numPlayers)));

  // New round: restart the tick count. (The game-over countdown is un-latched by
  // the new EntityManager's constructor below.)
  m_gameTicks = 0;

  if (m_pEntityManager)
  {
    delete m_pEntityManager;
    m_pEntityManager = nullptr;
  }
  if (m_pPanel)
  {
    delete m_pPanel;
    m_pPanel = nullptr;
  }
  if (m_pSpawningPool)
  {
    delete m_pSpawningPool;
    m_pSpawningPool = nullptr;
  }

  // after setting initialisation parameters
  m_pPlayers.resize(numPlayers);
  
  m_pEntityManager = new EntityManager();
  m_pPanel = new InfoPanel(m_surface, numPlayers, genders);
  GameRunningSDLInput::getInstance()->resetInputEvents();
  m_pSpawningPool = new SpawningPool(numPlayers * 100);
  initPlayerCharacters(numPlayers, genders, playerAIs);
  m_pInputHandler->setPointers(m_pPlayers, m_pEntityManager->getDynamicMap(), m_pPanel);
  m_pPanel->setPlayerPointers(m_pPlayers);
  seedBlocksOnMap(blockPercentage);
}

int GameEngine::gameLengthSeconds() const
{
  return (int) ((m_gameTicks * GameSettings::NORMAL_TICKS_SPEED) / 1000);
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
      m_gameTicks++;

      // Timed delay during which we continue to update and runFrame for a few seconds.
      // This is useful when all human players are dead but AI is still alive, incase
      //   a human player wants to take over the AI.
      if(GameOverTimer::getInstance()->advance(m_pEntityManager->oneOrZeroPlayersRemain(m_pSpawningPool->canSpawnMore())))
      {
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
        menuSystem->loadScoreBoard(m_pEntityManager->getWinningPlayer(m_pSpawningPool->canSpawnMore()), m_pEntityManager->getRoundPlayerRoster(), gameLengthSeconds());
      }
      try
      {
        m_pInputHandler->processKeyboardInput();
      }
      catch(const InputException &e)
      {
        if(e.gotQuit())
	      {
          GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
          menuSystem->loadScoreBoard(m_pEntityManager->getWinningPlayer(m_pSpawningPool->canSpawnMore()), m_pEntityManager->getRoundPlayerRoster(), gameLengthSeconds());
	      }
      }

      // game logic here...

      m_pSpawningPool->update();

      m_pEntityManager->runFrame();

      m_pEntityManager->renderEntities(m_surface);

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


void GameEngine::seedBlocksOnMap(int blockPercentage)
{
  //places cpu team blocks over most of the map
  // density is percentage of map to be covered (not including player spawn location)

  // clear existing static map blocks
  Map * staticMap = Map::getInstance();
  staticMap->clearBlocks();

  if (blockPercentage < 0)
  {
    blockPercentage = 0;
  }
  if (blockPercentage > 100)
  {
    blockPercentage = 100;
  }

  EntityMessageQueue * emq = EntityMessageQueue::getInstance();

  for (int y = 1; y < GameSettings::getInstance()->getMapHeight() - 1; y++)
  {
    for (int x = 1; x < GameSettings::getInstance()->getMapWidth() - 1; x++)
    {
      bool badSpot = false;
      // check if in player spawn corners
      if ((x < 4) && (y < 4))
      {
        badSpot = true;
      }
      if ((x < 4) && (y > GameSettings::getInstance()->getMapHeight() - 5))
      {
        badSpot = true;
      }
      if ((x > GameSettings::getInstance()->getMapWidth() - 5) && (y < 4))
      {
        badSpot = true;
      }
      if ((x > GameSettings::getInstance()->getMapWidth() - 5) && (y > GameSettings::getInstance()->getMapHeight() - 5))
      {
        badSpot = true;
      }

      // now ok to place blocks in non-wall tiles
      // check for static map wall piece
      if (staticMap->staticTileAt(x, y) == Map::WALL)
      {
        badSpot = true;
      }

      int team = 0;
      if (x < 6 && y < 5)
      {
        team = 1;
      }
      if (x >= GameSettings::getInstance()->getMapWidth() - 6 && y >= GameSettings::getInstance()->getMapHeight() - 5)
      {
        team = 2;
      }
      if (x >= GameSettings::getInstance()->getMapWidth() - 6 && y < 5)
      {
        team = 3;
      }
      if (x < 6 && y >= GameSettings::getInstance()->getMapHeight() - 5)
      {
        team = 4;
      }

      if (!badSpot)
      {
        int roll = SimRandom::sim().nextInt(100);
        if (roll < blockPercentage)
        {
          emq->postCreateMessage(BLOCK, team, x, y, 0);
        }
      }
    } // end for x
  }// end for y

  // deal with block creation messages
  m_pEntityManager->processMessageQueue();

}

void GameEngine::initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers, std::vector<bool>* playerAIs)
{
  if (numPlayers > NUM_TEAMS)
  {
    numPlayers = NUM_TEAMS;
  }

  EntityMessageQueue * emq  = EntityMessageQueue::getInstance();

  // FIXME inconsistency between starting at '1' or '0' for player characters is really confusing.
  for (unsigned int i = 1; i <= numPlayers; i++) 
  {
    std::MickLogger::getInstance()->debug(this, std::string("initPlayerCharacters, i=").append(std::to_string(i)));

    int atX = 0, atY = 0;
    // set x and y co-ords
    switch (i)
    {
      case 1:
      case 4:
        atX = 1;
        break;
      case 2:
      case 3:
        atX = GameSettings::getInstance()->getMapWidth() - 2;
        break;
      case 5:
      case 6:
        atX = GameSettings::getInstance()->getMapWidth() / 2;
        break;
    }
    switch (i)
    {
      case 1:
      case 3:
      case 5:
        atY = 1;
        break;
      case 2:
      case 4:
      case 6:
        atY = GameSettings::getInstance()->getMapHeight() - 2;
        break;
    }

    unsigned int flags = 0;

    if (playerAIs->at(i - 1) == true)
    {
      std::MickLogger::getInstance()->debug(this, std::string("player ").append(std::to_string(i)).append(" is AI controlled"));
      flags |= GameEntity::AI_CONTROLLED_BOT_FLAG;
    }

    emq->postCreateMessage(PLAYER_CHARACTER, i, atX, atY, flags);

    m_pEntityManager->processMessageQueue();

    if(m_pPlayers.size() < i)
    {
      m_pPlayers.resize(i);
    }
    m_pPlayers.at(i - 1) = m_pEntityManager->getPlayerPointer(i);

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
