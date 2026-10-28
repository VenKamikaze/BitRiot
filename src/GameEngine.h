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
#include "Simulation.h"
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
    // The deterministic world: setup (start) and the per-tick step. Everything the
    // sim needs lives in there; this class keeps rendering, the InfoPanel, sound,
    // the menu and input, and drives the Simulation.
    Simulation m_sim;
    InfoPanel* m_pPanel = nullptr;
    shared_ptr<InputHandler> m_pInputHandler = nullptr;
    //PlayerCharacterEntity* m_pPlayers[NUM_TEAMS];
    vector<shared_ptr<PlayerCharacterEntity>> m_pPlayers;

    // Scoreboard game length: the simulation's tick count x NORMAL_TICKS_SPEED.
    int gameLengthSeconds() const;
    void resetGame();
    // Pre-game: if the menu changed the map size, push GameSettings dims into the
    // Map statics and resize the window/back-buffer/texture + rebuild the static
    // map. No-op when dimensions are unchanged.
    void applyMapDimensionsIfChanged();
    // The presentation side of setting up the players the Simulation spawned:
    // which controller drives each, and their team-coloured sprites.
    void initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers);
    void uninitialise();
};

#endif
