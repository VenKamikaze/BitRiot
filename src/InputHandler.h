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
#include <functional>

class InputHandler : public std::enable_shared_from_this<InputHandler>
{
  public:
    InputHandler();
    ~InputHandler();

    void setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters, DynamicMap * dynamicMap, InfoPanel * panel);

    // Reads pending device and window events into the held/released key and pad
    // state (the quit key, a controller plug, the join selector included). The frame
    // loop calls it once per frame, whether or not a tick is about to run. Returns
    // true if any event was read.
    bool pumpEvents();
    // Applies the held/released input to the human players, once per stepped tick.
    // Throws InputException(GOT_QUIT) if the quit key was seen. (Brief 04a turns
    // this into a sampled bitmask handed to Simulation::step.)
    void applyInput();
    int getPlayerIdForKey(KEY k); // get the playerId associated with this key.
    int getPlayerIdForAction1Button(KEY k); // get player id for action1 button press, or -1 if no match
    int getPlayerIdForAction2Button(KEY k); // get player id for action1 button press, or -1 if no match
    int getKeyboardSetForActionKey(KEY k); // get the fixed keyboard-set index whose action1/action2 key is k, or -1. Used by the player-select menu (keyboard-join); searches the fixed m_availableKeySets table, not the per-game m_keyMap.

    shared_ptr<PlayerCharacterEntity> getPlayerAttachedToController(int controllerId);
    void detachController(int controllerId);

    // Phase 2.7: mid-game CPU-takeover selector. A device (keyboard set or
    // controller) that is not yet bound this game opens a selector to choose
    // which alive AI player to take over. Both return true if the event was
    // consumed by the selector (and should not fall through to gameplay).
    bool processJoinKeyboardEvent(KEY k);                      // a keyboard KEYDOWN
    bool processJoinControllerEvent(int controllerId, Uint8 button); // a controller button DOWN

    static const int NUM_ACTION_BUTTONS = 6;
    enum ButtonName { UP_KEY, DOWN_KEY, LEFT_KEY, RIGHT_KEY, ACTION1_KEY, ACTION2_KEY };

    // Controller movement/action input, independent of the keyboard m_keyMap
    // machinery entirely: every KEY_ enum value already corresponds to a real
    // physical key (see MickBaseInput.h + GameRunningSDLInput::setupKeymap),
    // so there is no spare "virtual" key tag a controller press could safely
    // borrow without risking collision with a real keyboard player's set.
    // Each controller-attached player instead gets a private per-player
    // button table here, written by GameRunningSDLInput::setControllerInput
    // and read by applyInput - so any number of simultaneous pads
    // can drive input with no shared-state collision risk.
    void setControllerButtonState(int playerId, ButtonName button, bool down);

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
    // Per-player controller button state (player ID -> which of the 6
    // ButtonName slots are currently held / were released this frame). See
    // setControllerButtonState. Released flags are consumed (cleared) on read,
    // mirroring GameRunningSDLInput's keysRecentlyReleased semantics.
    map<int, array<bool, NUM_ACTION_BUTTONS>> m_controllerDown;
    map<int, array<bool, NUM_ACTION_BUTTONS>> m_controllerReleased;

  private:
    void setAvailableKeys();
    vector<array<KEY, NUM_ACTION_BUTTONS>> m_availableKeySets; // fixed table of key sets, indexed by attachedKeyboardId (rebuilt each game).

    // Shared movement/action/item-selection logic for one alive human player
    // this frame, independent of whether the input source is a keyboard set
    // (isDown/isReleased backed by m_keyMap + the shared keysCurrentlyDown
    // singleton) or a controller (backed by m_controllerDown/m_controllerReleased).
    void processPlayerButtons(shared_ptr<PlayerCharacterEntity> player,
        const std::function<bool(ButtonName)>& isDown,
        const std::function<bool(ButtonName)>& isReleased);
    bool consumeControllerReleased(int playerId, ButtonName button);

    // Phase 2.7 mid-game-join selector state. One entry per device currently
    // choosing a CPU player to take over (supports several joiners at once).
    struct PendingJoin
    {
      bool isController;  // true: deviceId is a controllerId; false: a keyboard set index
      int  deviceId;
      int  targetSlot;    // 1-based team currently highlighted
    };
    vector<PendingJoin> m_pendingJoins;

    // Selector helpers (all index per-player vectors by getPlayerNumber()-1, never getID()).
    int  getKeyboardSetForKey(KEY k, ButtonName & outButton); // fixed-table set index for any button key, or -1
    bool isKeyboardSetBound(int setIndex);                    // does a live player already drive this set?
    int  findPendingJoinIndex(bool isController, int deviceId);
    shared_ptr<PlayerCharacterEntity> findPlayerByTeam(int team);
    vector<int> getTakeableSlots(int excludePendingIndex);    // alive AI slots free to take over
    bool openJoin(bool isController, int deviceId);           // start a selector; false if nothing to take
    void navigateJoin(int pendingIndex, int direction);       // -1 up / +1 down
    void confirmJoin(int pendingIndex);                       // take over the highlighted slot
    void cancelJoin(int pendingIndex);                        // abandon the selector
    void takeOverSlotWithController(int team, int controllerId);
    void takeOverSlotWithKeyboard(int team, int keyboardSetIndex);
};


#endif
