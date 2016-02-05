/**
 * GameEngine.h
 * To handle being in gameInit,
 * gameRunning, and GameOver states
 *
 */
#ifndef _GAMEENGINECLASS
#define _GAMEENGINECLASS

#include "EntityManager.h"
#include "InfoPanel.h"
#include "InputHandler.h"
#include "SpawningPool.h"
#include "DDraw.h"

class GameEngine
{
public:
	GameEngine(LPDIRECTDRAW7 lpdd, LPDIRECTDRAWSURFACE7 lpddsback);
	~GameEngine();

	void runEngine();


private:
	enum GAME_STATE { GAME_INIT, GAME_RUNNING, GAME_OVER };
	GAME_STATE m_state;

	LPDIRECTDRAW7 lpdd;
	LPDIRECTDRAWSURFACE7 lpddsback;

	EntityManager * m_pEntityManager;
	InfoPanel * m_pPanel;
	InputHandler * m_pInputHandler;
	SpawningPool * m_pSpawningPool;

	PlayerCharacterEntity * m_pPlayers[NUM_TEAMS];	
	int m_numHumanPlayers; // number of human players, 1 - 4
	void initHumanPlayers(int numPlayers, bool * maleGender, bool * botAI);


	void seedBlocksOnMap(int blockPercentage); //places cpu team blocks over most of the map
		// blockPercentage is value from 0 to 100

};

#endif