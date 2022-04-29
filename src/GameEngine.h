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
#include "PlayerCharacterEntity.h"
#include "SpawningPool.h"
#include "Map.h"
#include <SDL2/SDL.h>

#include "MenuSDLRenderer.h"
#include "GameSettings.h"
#include "GameOverTimer.h"
#include "GameScoreBoardTimer.h"
#include "MickSDLRenderer.h"

class GameEngine
{
  public:
    GameEngine(SDL_Surface* lpddsback, shared_ptr<InputHandler> inputHandler);
    void setMenuSystem(unique_ptr<MenuSDLRenderer> rmlMenu)
    {
      menuSystem = std::move(rmlMenu);
    }
    ~GameEngine();

    bool runEngine();


  private:
    SDL_Surface* m_surface = nullptr;
    unique_ptr<MenuSDLRenderer> menuSystem = nullptr;
    EntityManager* m_pEntityManager = nullptr;
    InfoPanel* m_pPanel = nullptr;
    shared_ptr<InputHandler> m_pInputHandler = nullptr;
    SpawningPool* m_pSpawningPool = nullptr;
    //PlayerCharacterEntity* m_pPlayers[NUM_TEAMS];
    vector<shared_ptr<PlayerCharacterEntity>> m_pPlayers;

    unsigned int m_gameStartedAtTicks = 0;
    void resetGame();
    void initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers, std::vector<bool>* playerAIs);
    void seedBlocksOnMap(int blockPercentage);
    void uninitialise();
};

#endif
