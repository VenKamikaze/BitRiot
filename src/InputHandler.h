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
#include "GameRunningSDLInput.h"
#include "InputException.h"

class InputHandler : public std::enable_shared_from_this<InputHandler>
{
  public:
    InputHandler();
    ~InputHandler();

    void setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters, DynamicMap * dynamicMap, InfoPanel * panel);

    void processKeyboardInput();
    int getPlayerIdForKey(KEY k); // get the playerId associated with this key.
    int getPlayerIdForAction1Button(KEY k); // get player id for action1 button press, or -1 if no match
    int getPlayerIdForAction2Button(KEY k); // get player id for action1 button press, or -1 if no match
    int getKeyboardSetForActionKey(KEY k); // get the fixed keyboard-set index whose action1/action2 key is k, or -1. Used by the player-select menu (keyboard-join); searches the fixed m_availableKeySets table, not the per-game m_keyMap.

    shared_ptr<PlayerCharacterEntity> getPlayerAttachedToController(int controllerId);
    shared_ptr<PlayerCharacterEntity> attachNewControllerToPlayer(int controllerId);
    void detachController(int controllerId);

    static const int NUM_ACTION_BUTTONS = 6;
    enum ButtonName { UP_KEY, DOWN_KEY, LEFT_KEY, RIGHT_KEY, ACTION1_KEY, ACTION2_KEY };

  public:
    bool usingControllers=false;
    map<int, array<KEY, NUM_ACTION_BUTTONS>> m_keyMap; // player ID, keys available to use

  protected:
    vector<shared_ptr<PlayerCharacterEntity>> p_players; // pointers to player character entities
    DynamicMap * p_dynamicMap;
    InfoPanel * p_infoPanel;
    map<int, bool> m_action1Down; // player ID, action1Down
    map<int, bool> m_action2Down; // player ID, action2Down
    map<int, EntityType> m_spawnSelection; // player ID, Entity selected
  
  private:
    void setAvailableKeys();
    vector<array<KEY, NUM_ACTION_BUTTONS>> m_availableKeySets; // fixed table of key sets, indexed by attachedKeyboardId (rebuilt each game).
};


#endif
