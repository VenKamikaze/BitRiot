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

#include "MenuSDLRenderer.h"
#include "GameSettings.h"
#include "GameOverTimer.h"
#include "GameScoreBoardTimer.h"

class GameEngine
{
  public:
    GameEngine(SDL_Surface* lpddsback);
    void setMenuSystem(MenuSDLRenderer* rocketMenu)
    {
      menuSystem = rocketMenu;
    }
    ~GameEngine();

    bool runEngine();


  private:
    SDL_Surface* m_surface = NULL;
    MenuSDLRenderer* menuSystem = NULL;
    EntityManager* m_pEntityManager = NULL;
    InfoPanel* m_pPanel = NULL;
    InputHandler* m_pInputHandler = NULL;
    SpawningPool* m_pSpawningPool = NULL;
    PlayerCharacterEntity* m_pPlayers[NUM_TEAMS];
    unsigned int m_gameStartedAtTicks = 0;
    void resetGame();
    void initHumanPlayers(int numPlayers, std::vector<bool>* malePlayers, std::vector<bool>* playerAIs);
    void seedBlocksOnMap(int blockPercentage);
    void uninitialise();
};

#endif
