/**
 * InputHandler could be part of the GameEngine class
 * but it is seperated here just to keep the classes smaller
 */
#ifndef _INPUTHANDLERCLASS
#define _INPUTHANDLERCLASS

#include "DynamicMap.h"
#include "PlayerCharacterEntity.h"
#include "InfoPanel.h"
#include <SDL2/SDL.h>
#include "MickSDLInput.h"
#include "InputException.h"


class InputHandler
{
  public:
    InputHandler();
    ~InputHandler();

    void setPointers(int numPlayers, PlayerCharacterEntity * player1,
                     PlayerCharacterEntity * player2, PlayerCharacterEntity * player3,
                     PlayerCharacterEntity * player4, DynamicMap * dynamicMap, InfoPanel * panel);

    void setKeys();

    void processKeyboardInput();

    void setPlayerDead(int player, bool flag);

    PlayerCharacterEntity *getPlayerAttachedToController(int controllerId);
    PlayerCharacterEntity *attachNewControllerToPlayer(int controllerId);
    void detachController(int controllerId);

    static const int NUM_ACTION_BUTTONS = 6;
    enum ButtonName { UP_KEY, DOWN_KEY, LEFT_KEY, RIGHT_KEY, ACTION1_KEY, ACTION2_KEY };

  public:
    int m_numPlayers;

    bool usingControllers=false;


    KEY m_keyMap[4][NUM_ACTION_BUTTONS]; // keymap of 6 action buttons for the 4 max players

  protected:

    PlayerCharacterEntity * p_players[4]; // pointers to player character entities
    DynamicMap * p_dynamicMap;
    InfoPanel * p_infoPanel;
    bool m_playerDead[4];


};


#endif
