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
#include "Map.h"
//#include "DDraw.h"
#include <SDL2/SDL.h>

#include "MenuRenderer.h"

class GameEngine
{
  public:
    GameEngine(SDL_Surface* lpddsback);
    void setMenuSystem(MenuRenderer* rocketMenu) {
    	menuSystem = rocketMenu;
    }
    ~GameEngine();

    void runEngine();


  private:
    enum GAME_STATE { GAME_INIT, MENU_RUNNING, GAME_RUNNING, GAME_OVER };
    GAME_STATE m_state;
	SDL_Surface* lpddsback;
	MenuRenderer* menuSystem = NULL;
	EntityManager* m_pEntityManager;
	InfoPanel* m_pPanel;
	InputHandler* m_pInputHandler;
	SpawningPool* m_pSpawningPool;
	PlayerCharacterEntity* m_pPlayers[NUM_TEAMS];
	int m_numHumanPlayers;
	void resetGame(int numHumanPlayers);
	void initHumanPlayers(int numPlayers, bool* maleGender, bool* botAI);
	void seedBlocksOnMap(int blockPercentage);

};

#endif
