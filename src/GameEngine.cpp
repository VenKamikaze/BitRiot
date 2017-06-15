#include "GameEngine.h"

GameEngine::GameEngine(SDL_Surface* back)
{
  m_pEntityManager = NULL;
  m_pSpawningPool = NULL;

  //lpdd = p_dd;
  lpddsback = back;

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

  for (int player = numPlayers-1; player >= (numPlayers - numHumanPlayers) ; player--)
  {
    // reverse iterate, player one is actually botAI[3]
    playerAIs->at(player) = false;
  }

  // after setting initialisation parameters
  m_pEntityManager = new EntityManager();
  m_pPanel = new InfoPanel(lpddsback, numPlayers, genders);
  m_pInputHandler = new InputHandler();
  m_pSpawningPool = new SpawningPool(numPlayers * 100);
  initHumanPlayers(numPlayers, genders, playerAIs);
  m_pInputHandler->setPointers(numPlayers, m_pPlayers[1], m_pPlayers[2], m_pPlayers[3], m_pPlayers[4],
      m_pEntityManager->getDynamicMap(), m_pPanel);
  m_pPanel->setPlayerPointers(numPlayers, m_pPlayers[1], m_pPlayers[2], m_pPlayers[3], m_pPlayers[4]);
  seedBlocksOnMap(blockPercentage);
}

bool GameEngine::runEngine()
{
  switch (GameSettings::getInstance()->getGameState())
  {
    case GameSettings::MENU_RUNNING:
    {
      if (menuSystem == NULL)
      {
        throw std::runtime_error(std::string("About to render menu, but no menu rendering system found!"));
      }

      if (!menuSystem->showMenu())
      {
        GameSettings::getInstance()->setGameState(GameSettings::GAME_INIT);
      }
      break;
    }
    case GameSettings::GAME_INIT:
    {
      resetGame();
      GameSettings::getInstance()->setGameState(GameSettings::GAME_RUNNING);
      break;
    }
    case GameSettings::GAME_RUNNING:
    {
      // read keyboard and other devices here
      for (int i = 0; i < GameSettings::getInstance()->getNumberOfPlayers(); i++)
      {
        m_pInputHandler->setPlayerDead(i, m_pEntityManager->getPlayerDead(i));
        m_pPanel->setPlayerDead(i, m_pEntityManager->getPlayerDead(i));
      }
      if(m_pEntityManager->allPlayersDead())
      {
        GameTimer* timer = GameOverTimer::getInstance();
        if(! timer->isTimerTriggered())
        {
          // If every player is dead, set game over state
          timer->setTimerTriggered();
        }
        else if(timer->getTimerCompleted())
        {
          // Timed delay during which we continue to update and runFrame for a few seconds.
          // This is useful when all human players are dead but AI is still alive, incase
          //   a human player wants to take over the AI.
          GameSettings::getInstance()->setGameState(GameSettings::GAME_OVER);
        }
      }
      m_pInputHandler->processKeyboardInput();

      // game logic here...

      m_pSpawningPool->update();

      m_pEntityManager->runFrame();

      m_pEntityManager->renderEntities(lpddsback);
      m_pPanel->renderSurfaceTo(lpddsback, (GameSettings::getInstance()->getMapWidth() * Map::TILE_WIDTH), 0); //Possible but very rare crash here, due to dangling pointers
      //as m_pPanel->setPlayerDead being updated one frame late
      break;
    }
    case GameSettings::GAME_OVER:
    {
      // TODO show a score for 10 seconds then set back to MENU_RUNNING ?
      // maybe number of seconds alive, number of bombs/eggs spawned etc?
      GameTimer* timer = GameScoreBoardTimer::getInstance();
      if(! timer->isTimerTriggered())
      {
        // If every player is dead, set game over state
        timer->setTimerTriggered();
      }
      else if(timer->getTimerCompleted())
      {
        // Timed delay during which we continue to update and runFrame for a few seconds.
        // This is useful when all human players are dead but AI is still alive, incase
        //   a human player wants to take over the AI.
        GameSettings::getInstance()->setGameState(GameSettings::MENU_RUNNING);
      }

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

void GameEngine::initHumanPlayers(int numPlayers, std::vector<bool>* malePlayers, std::vector<bool>* playerAIs)
{
  if (numPlayers > NUM_TEAMS)
  {
    numPlayers = NUM_TEAMS;
  }

  EntityMessageQueue * emq  = EntityMessageQueue::getInstance();

  for (int i = 1; i <= numPlayers; i++)
  {
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
      flags |= GameEntity::AI_CONTROLLED_BOT_FLAG;
    }

    emq->postCreateMessage(PLAYER_CHARACTER, i, atX, atY, flags);

    m_pEntityManager->processMessageQueue();

    m_pPlayers[i] = m_pEntityManager->getPlayerPointer(i);

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
    er->replaceTeamSurfaceWithImage(lpddsback, ss.str().c_str(), i);
  }

}
