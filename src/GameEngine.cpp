#include "GameEngine.h"

GameEngine::GameEngine(SDL_Surface* back)
{
	m_pEntityManager = NULL;
	m_pSpawningPool = NULL;

	//lpdd = p_dd;
	lpddsback = back;

	m_state = GAME_INIT;
}

GameEngine::~GameEngine()
{
	if (m_pInputHandler)
		delete m_pInputHandler;
	if (m_pPanel)
		delete m_pPanel;
	if (m_pEntityManager)
		delete m_pEntityManager;
	if (m_pSpawningPool)
		delete m_pSpawningPool;

}

void GameEngine::runEngine()
{
	switch (m_state)
	{
	case GAME_INIT:
	{
		int numPlayers = 4;

		int blockPercentage = 55;

		bool gender[5] = { true, true, false, false, true };
		bool botAI[4] = { true, true, true, false };
		// after setting initialisation parameters

		m_pEntityManager = new EntityManager();
		m_pPanel = new InfoPanel(lpddsback, numPlayers, &gender[1]);
		m_pInputHandler = new InputHandler();
		m_pSpawningPool = new SpawningPool(numPlayers * 100);

		initHumanPlayers(numPlayers, &gender[0], &botAI[0]);

		m_pInputHandler->setPointers(numPlayers, m_pPlayers[1],
			m_pPlayers[2], m_pPlayers[3], m_pPlayers[4],
			m_pEntityManager->getDynamicMap(), m_pPanel);
		m_pPanel->setPlayerPointers(numPlayers, m_pPlayers[1],
			m_pPlayers[2], m_pPlayers[3], m_pPlayers[4]);
		seedBlocksOnMap(blockPercentage);
		////M2S SOUND DSound::getInstance()->setMuted(false); // M2S SOUND
		m_state = GAME_RUNNING;
		runEngine();
	}
		break;
	case GAME_RUNNING:
	{

		// read keyboard and other devices here
		for (int i = 0; i < 4; i++)
		{
			m_pInputHandler->setPlayerDead(i, m_pEntityManager->getPlayerDead(i + 1));
			m_pPanel->setPlayerDead(i, m_pEntityManager->getPlayerDead(i + 1));
		}
		m_pInputHandler->processKeyboardInput(); 

		// game logic here...

		m_pSpawningPool->update();

		m_pEntityManager->runFrame();

		m_pEntityManager->renderEntities(lpddsback);
		m_pPanel->renderSurfaceTo(lpddsback, (Map::MAP_WIDTH * Map::TILE_WIDTH), 0);
		
	}
		break;
	case GAME_OVER:


	break;
	}
}


void GameEngine::seedBlocksOnMap(int blockPercentage)
{
	//places cpu team blocks over most of the map
	// density is percentage of map to be covered (not including player spawn location)

	// clear existing static map blocks
    Map * staticMap = Map::getInstance();
	staticMap->clearBlocks();

	if (blockPercentage < 0)
		blockPercentage = 0;
	if (blockPercentage > 100)
		blockPercentage = 100;

	EntityMessageQueue * emq = EntityMessageQueue::getInstance();

	for (int y = 1; y < Map::MAP_HEIGHT - 1; y++)
	{
		for (int x = 1; x < Map::MAP_WIDTH - 1; x++)
		{
			bool badSpot = false;
			// check if in player spawn corners
			if ((x < 4) && (y < 4))
				badSpot = true;
			if ((x < 4) && (y > Map::MAP_HEIGHT - 5))
				badSpot = true;
			if ((x > Map::MAP_WIDTH - 5) && (y < 4))
				badSpot = true;
			if ((x > Map::MAP_WIDTH - 5) && (y > Map::MAP_HEIGHT - 5))
				badSpot = true;

			// now ok to place blocks in non-wall tiles
			// check for static map wall piece
			if (staticMap->staticTileAt(x, y) == Map::WALL)
				badSpot = true;
			
			int team = 0;
			if (x < 6 && y < 5)
				team = 1;
			if (x >= Map::MAP_WIDTH - 6 && y >= Map::MAP_HEIGHT - 5)
				team = 2;
			if (x >= Map::MAP_WIDTH - 6 && y < 5)
				team = 3;
			if (x < 6 && y >= Map::MAP_HEIGHT - 5)
				team = 4;
	
			if (!badSpot)
			{
				int roll = rand()%100;
				if (roll < blockPercentage)
                    emq->postCreateMessage(BLOCK, team, x, y, 0);
			}
		} // end for x
	}// end for y

	// deal with block creation messages
	m_pEntityManager->processMessageQueue();

}

void GameEngine::initHumanPlayers(int numPlayers, bool * maleGender, bool * botAI)
{
	if (numPlayers > NUM_TEAMS)
		numPlayers = NUM_TEAMS;

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
			atX = Map::MAP_WIDTH - 2;
			break;
		}
		switch (i)
		{
		case 1:
		case 3:
			atY = 1;
			break;
		case 2:
		case 4:
			atY = Map::MAP_HEIGHT - 2;
			break;
		}

		unsigned int flags = 0;
		
		if (botAI[i - 1] == true)
            flags |= GameEntity::AI_CONTROLLED_BOT_FLAG;

		emq->postCreateMessage(PLAYER_CHARACTER, i, atX, atY, flags);

		m_pEntityManager->processMessageQueue();

		m_pPlayers[i] = m_pEntityManager->getPlayerPointer(i);

		// set image filename with "extension"
		stringstream ss;
		ss << "bitmaps/player" << i;
		if (maleGender[i] == true)
			ss << "male.bmp";
		else
			ss << "female.bmp";

		// set animation image for male or female
		EntityRenderer * er = EntityRendererFactory::getInstance()->getEntityRenderer(PLAYER_CHARACTER);
		er->replaceTeamSurfaceWithImage(ss.str().c_str(), i);
	}

}
