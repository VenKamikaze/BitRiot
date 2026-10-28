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

    // Sim ticks run this round (GAME_RUNNING ticks); drives the scoreboard game length.
    unsigned long m_gameTicks = 0;
    int gameLengthSeconds() const;
    void resetGame();
    // Pre-game: if the menu changed the map size, push GameSettings dims into the
    // Map statics and resize the window/back-buffer/texture + rebuild the static
    // map. No-op when dimensions are unchanged.
    void applyMapDimensionsIfChanged();
    void initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers, std::vector<bool>* playerAIs);
    void seedBlocksOnMap(int blockPercentage);
    void uninitialise();
};

#endif
