/**
 * InputHandler could be part of the GameEngine class
 * but it is seperated here just to keep the classes smaller
 */
#ifndef _INPUTHANDLERCLASS
#define _INPUTHANDLERCLASS

#include "DynamicMap.h"
#include "GameSettings.h"
#include "PlayerCharacterEntity.h"
#include "InfoPanel.h"
#include <SDL2/SDL.h>
#include "MickSDLInput.h"
#include "InputException.h"


class InputHandler : public std::enable_shared_from_this<InputHandler>
{
  public:
    InputHandler();
    ~InputHandler();

    void setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters, DynamicMap * dynamicMap, InfoPanel * panel);


    void processKeyboardInput();

    void setPlayerDead(int player, bool flag);

    shared_ptr<PlayerCharacterEntity> getPlayerAttachedToController(int controllerId);
    shared_ptr<PlayerCharacterEntity> attachNewControllerToPlayer(int controllerId);
    void detachController(int controllerId);

    static const int NUM_ACTION_BUTTONS = 6;
    enum ButtonName { UP_KEY, DOWN_KEY, LEFT_KEY, RIGHT_KEY, ACTION1_KEY, ACTION2_KEY };

  public:
    //int m_numPlayers;
    bool usingControllers=false;
    //KEY m_keyMap[GameSettings::MAX_PLAYERS][NUM_ACTION_BUTTONS]; // keymap of 6 action buttons for the max players
    map<int, array<KEY, NUM_ACTION_BUTTONS>> m_keyMap; // player ID, keys available to use

  protected:

    vector<shared_ptr<PlayerCharacterEntity>> p_players; // pointers to player character entities
    DynamicMap * p_dynamicMap;
    InfoPanel * p_infoPanel;
    //vector<bool> m_playerDead;
    // static vectors that determine the chosen spawn entity for each human character.
    map<int, bool> m_action1Down; // player ID, action1Down
    map<int, bool> m_action2Down; // player ID, action2Down
    map<int, EntityType> m_spawnSelection; // player ID, Entity selected
  
  private:
    void setAvailableKeys();
    vector<array<KEY, NUM_ACTION_BUTTONS>> m_availableKeySets; // pop from here as we add human players, recreate on new InputHandler.
};


#endif
