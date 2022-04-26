#include "GameEngine.h"
#include "GameSettings.h"
#include "MickSDLRenderer.h"
#include "PlayerCharacterEntity.h"
#include "SDL_timer.h"
#include <string>

GameEngine::GameEngine(SDL_Surface* back)
{
  m_pEntityManager = NULL;
  m_pSpawningPool = NULL;

  m_surface = back;

  GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
}

GameEngine::~GameEngine()
{
  uninitialise();
}

void GameEngine::uninitialise()
{
  if (m_pInputHandler)
  {
    delete m_pInputHandler;
    m_pInputHandler = NULL;
  }
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
}

void GameEngine::resetGame()
{
  int numHumanPlayers = GameSettings::getInstance()->getNumberOfHumanPlayers();
  int numPlayers =  GameSettings::getInstance()->getNumberOfPlayers();
  int blockPercentage =  GameSettings::getInstance()->getBlockSpawnPercentage();
  std::vector<bool>* genders = GameSettings::getInstance()->getPlayerGenders();
  std::vector<bool>* playerAIs = GameSettings::getInstance()->getPlayerAIs();

  std::MickLogger::getInstance()->debug(this, std::string("numHumanPlayers=").append(std::to_string(numHumanPlayers)).append(",numPlayers=").append(std::to_string(numPlayers)));

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
  if (m_pInputHandler)
  {
    delete m_pInputHandler;
    m_pInputHandler = nullptr;
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
  m_pInputHandler = new InputHandler();
  m_pSpawningPool = new SpawningPool(numPlayers * 100);
  initPlayerCharacters(numPlayers, genders, playerAIs);
  m_pInputHandler->setPointers(m_pPlayers, m_pEntityManager->getDynamicMap(), m_pPanel);
  m_pPanel->setPlayerPointers(m_pPlayers);
  seedBlocksOnMap(blockPercentage);
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
      resetGame();
      GameSettings::getInstance()->setGameState(GameSettings::GAME_RUNNING);
      m_gameStartedAtTicks = SDL_GetTicks();
      break;
    }
    case GameSettings::GAME_RUNNING:
    {
      // PlayerCharacterEntity knows if it is alive, so we don't need to synchronise with input or panel now.
      //for (int i = 0; i < GameSettings::getInstance()->getNumberOfPlayers(); i++)
      //{
        //m_pInputHandler->setPlayerDead(i, m_pEntityManager->getPlayerDead(i)); // TODO check isAlive on the PCE
        //m_pPanel->setPlayerDead(i, m_pEntityManager->getPlayerDead(i));
      //}
      // read keyboard and other devices here
      if(m_pEntityManager->oneOrZeroPlayersRemain())
      {
        GameTimer* timer = GameOverTimer::getInstance();
        if(! timer->isTimerTriggered())
        {
          // If competitor players are dead, set game over state
          timer->setTimerTriggered();
        }
        else if(timer->getTimerCompleted())
        {
          // Timed delay during which we continue to update and runFrame for a few seconds.
          // This is useful when all human players are dead but AI is still alive, incase
          //   a human player wants to take over the AI.
          GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
          menuSystem->loadScoreBoard(m_pEntityManager->getWinningPlayer(), (SDL_GetTicks() - m_gameStartedAtTicks) / 1000);
        }
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
          menuSystem->loadScoreBoard(m_pEntityManager->getWinningPlayer(), (SDL_GetTicks() - m_gameStartedAtTicks) / 1000);
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
        int roll = rand()%100;
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
