/*
 * GameSettings.h
 *
 *  Created on: May 25, 2017
 *      Author: msaun
 */

#ifndef GAMESETTINGS_H_
#define GAMESETTINGS_H_

#include <string>
#include "RuntimeException.h"
#include <vector>
#include "Map.h"
#include "MickLogger.h"
#include "PlayerSetupStruct.h"

namespace std
{

class GameSettings
{
  public:
    GameSettings();
    static GameSettings* getInstance();
    enum GAME_STATE { GAME_INIT, MENU_RUNNING, GAME_RUNNING, GAME_QUIT };

    inline int getNumberOfHumanPlayers()
    {
      int human = 0;
      for(int i = 0; i < numPlayers; i++)
      {
        if(! playerAIs.at(i))
        {
          ++human;
        }
      }
      return human;
    }

    inline int getNumberOfPlayers()
    {
      return numPlayers;
    }
    inline int getBlockSpawnPercentage()
    {
      return blockPercentage;
    }
    inline std::vector<bool>* getPlayerGenders()
    {
      return &genders;
    }
    inline bool getPlayerGender(int playerIndex)
    {
      return genders.at(playerIndex);
    }
    inline std::vector<bool>* getPlayerAIs()
    {
      return &playerAIs;
    }
    inline int getMapWidth()
    {
      return mapWidth;
    }
    inline int getMapHeight()
    {
      return mapHeight;
    }

    // Canonical model accessors. m_players is the single source of truth for
    // per-player setup; genders/playerAIs are derived mirrors kept in sync by
    // syncMirrors() so existing pointer-returning accessors stay valid.
    inline std::vector<Player>* getPlayers()
    {
      return &m_players;
    }
    inline Player* getPlayer(int playerIndex)
    {
      return &m_players.at(playerIndex);
    }

    // These mutate the canonical model and re-derive the mirrors. Out-of-line
    // in the .cpp because they build Player records / iterate.
    void setNumberOfPlayers(int allPlayers);
    void setPlayerGender(int playerIndex, bool isMale);
    void setPlayerAI(int playerIndex, bool isAI);

    inline void setPlayerController(int playerIndex, int controllerId)
    {
      m_players.at(playerIndex).attachedControllerId = controllerId;
    }
    inline void setPlayerKeyboard(int playerIndex, int keyboardSetIndex)
    {
      m_players.at(playerIndex).attachedKeyboardId = keyboardSetIndex;
    }

    inline void setBlockSpawnPercentage(int blockSpawnPercentage)
    {
      this->blockPercentage = blockSpawnPercentage;
    }

    // NPC Spawner toggle (Options menu). When off, the purple CPU spawning pool
    // stops dripping enemy eggs but still spawns power-ups (EN+/IQ+). The pool
    // re-reads this live each tick, so it applies immediately and persists across
    // "Play again" (never reset by resetGame). Default on.
    inline bool isNpcSpawnerEnabled()
    {
      return npcSpawnerEnabled;
    }
    inline void setNpcSpawnerEnabled(bool enabled)
    {
      this->npcSpawnerEnabled = enabled;
    }

    // Map size is changed pre-game via the Options menu; GameEngine pushes these
    // into Map::MAP_WIDTH/HEIGHT and resizes the window/buffers at GAME_INIT.
    inline void setMapWidth(int width)
    {
      this->mapWidth = width;
    }
    inline void setMapHeight(int height)
    {
      this->mapHeight = height;
    }

    // Minimum map HEIGHT (in tiles) needed to fit numPlayers InfoPanel portraits.
    // Portraits stack vertically: player N's portrait bottom sits at 3*N tiles
    // (InfoPanel yOffset = ((i*3)+1), spanning 2 tiles), and the panel is
    // mapHeight tiles tall. Rounded up to an odd value (map dims are always odd).
    // Width never constrains player count. Single source of truth for the
    // player-select gate, the on-screen message, and the start-game guard.
    inline int requiredMapHeightFor(int numberOfPlayers)
    {
      int needed = 3 * numberOfPlayers;
      if (needed % 2 == 0) needed++; // keep odd, matching the map-size step
      return needed;
    }

    // The game may start unless (a) more than MAX_KEYBOARD_PLAYERS players are
    // configured on a map too short to show all their portraits, or (b) a
    // controller-only slot (index >= MAX_KEYBOARD_PLAYERS) is set to human
    // with no device attached - it would sit frozen at game start (defense in
    // depth: the player-select UI shouldn't be able to reach this state, but
    // this is the single source of truth for the start gate).
    inline bool canStartGame()
    {
      if (hasUndrivableHuman())
      {
        return false;
      }
      if (numPlayers <= MAX_KEYBOARD_PLAYERS)
      {
        return true;
      }
      return mapHeight >= requiredMapHeightFor(numPlayers);
    }

    // True if some controller-only slot (index >= MAX_KEYBOARD_PLAYERS) is
    // human with neither a keyboard set nor a controller attached.
    inline bool hasUndrivableHuman()
    {
      for(int i = MAX_KEYBOARD_PLAYERS; i < numPlayers; i++)
      {
        Player &p = m_players.at(i);
        if(p.human && p.attachedControllerId == -1 && p.attachedKeyboardId == -1)
        {
          return true;
        }
      }
      return false;
    }

    inline GAME_STATE getGameState()
    {
      return gameState;
    }
    inline void setGameState(GAME_STATE state)
    {
      this->gameState = state;
    }

    static const int MAX_PLAYERS = 6;
    static const int MIN_PLAYERS = 2; // 2 player characters minimum plus the default purple non player PC.
    // Only the first 4 player slots can be keyboard-driven (there are exactly 4
    // fixed keyboard sets). Slots 5 & 6 (index >= 4) are controller-only: they
    // go human via a controller (menu controller-join or the in-game takeover),
    // never via the keyboard "Control: CPU/Human" toggle.
    static const int MAX_KEYBOARD_PLAYERS = 4;

    static const int MAX_BLOCK_PCT = 75;
    static const int MIN_BLOCK_PCT = 20;

    // Map dimensions step by 2 so they stay ODD (the outer-wall + pillar layout in
    // Map::init assumes odd width/height). Bounds chosen with the user.
    static const int MIN_MAP_WIDTH = 13;
    static const int MAX_MAP_WIDTH = 33;
    static const int MIN_MAP_HEIGHT = 11;
    static const int MAX_MAP_HEIGHT = 25;
    static const int MAP_SIZE_STEP = 2;

    static const int NORMAL_TICKS_SPEED = 33;
    static const int FAST_TICKS_SPEED = 5; // quick games for debugging purposes

  private:
    ~GameSettings();
    // Rewrites the genders/playerAIs mirror vectors from the canonical
    // m_players. Call after any mutation of m_players.
    void syncMirrors();

    GAME_STATE gameState;

    int numPlayers = 4;
    int blockPercentage = 55;
    bool npcSpawnerEnabled = true; // NPC Spawner toggle; default on.
    // Canonical per-player setup. Genders/playerAIs below are derived mirrors.
    std::vector<Player> m_players;
    std::vector<bool> genders;
    std::vector<bool> playerAIs;

    int mapWidth = Map::MAP_WIDTH;   // not changeable outside of runtime flags for now
    int mapHeight = Map::MAP_HEIGHT; // not changeable outside of runtime flags for now

    float mapRatio = 1.3077;
    

};

} /* namespace std */
#endif /* GAMESETTINGS_H_ */
