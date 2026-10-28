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
    // The map size the window and back buffer were last made for (the Map's own
    // size follows each round's MatchConfig; the window follows it here).
    int m_windowMapWidth = 0;
    int m_windowMapHeight = 0;
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
    // After Simulation::start() has given the Map the round's size: if that differs
    // from the size the window was made for, resize the window, back buffer and
    // texture and reload the tile pictures against the new surface. No-op when the
    // dimensions are unchanged.
    void resizeWindowIfMapChanged();
    // The presentation side of setting up the players the Simulation spawned:
    // which controller drives each, and their team-coloured sprites.
    void initPlayerCharacters(unsigned int numPlayers, std::vector<bool>* malePlayers);
    void uninitialise();
};

#endif
