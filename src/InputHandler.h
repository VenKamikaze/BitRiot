/**
 * InputHandler could be part of the GameEngine class
 * but it is seperated here just to keep the classes smaller
 */
#ifndef _INPUTHANDLERCLASS
#define _INPUTHANDLERCLASS

#include "GameSettings.h"
#include "PlayerCharacterEntity.h"
#include "InfoPanel.h"
#include "InputSampler.h"
#include "TickInputs.h"

#include <SDL2/SDL.h>
#include "GameRunningSDLInput.h"

class InputHandler : public std::enable_shared_from_this<InputHandler>
{
  public:
    InputHandler();
    ~InputHandler();

    void setPointers(vector<shared_ptr<PlayerCharacterEntity>> playerCharacters, InfoPanel * panel);

    // Reads pending device and window events into the sampler (every key and pad
    // button change of a driven slot, the quit key, a controller plug and the join
    // selector included). The frame loop calls it once per frame, whether or not a
    // tick is about to run. Returns true if any event was read.
    bool pumpEvents();
    // The input of the human slots for the tick being stepped now, one PlayerInput
    // per slot (SLOT = playerNumber - 1), for Simulation::step(). Call it once per
    // stepped tick: it consumes the released edges accumulated since the last call.
    // The local slot mask is every human slot, as every human is local today; a
    // netplay client would restrict it to its own players.
    TickInputs sampleTick();
    // True once the quit key (or the window's close button) has been seen. It is a
    // local flag, never a sim input; GameEngine ends the round on this machine.
    bool quitRequested() const;

    // Device events, called by GameRunningSDLInput as they are polled. A key that
    // belongs to a driven slot's keyboard set, or a controller button of the pad
    // attached to a slot, becomes a setButton() on the sampler.
    void keyboardEvent(KEY k, bool down);
    int getPlayerIdForKey(KEY k); // get the SLOT (playerNumber - 1) whose keyboard set holds this key, or -1.
    int getPlayerIdForAction1Button(KEY k); // get the slot for an action1 key, or -1 if no match
    int getPlayerIdForAction2Button(KEY k); // get the slot for an action2 key, or -1 if no match
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
    // A controller button goes straight to the sampler for the slot of the
    // player the pad is attached to, written by
    // GameRunningSDLInput::setControllerInput - so any number of simultaneous
    // pads can drive input with no shared-state collision risk. SLOT is
    // playerNumber - 1.
    void setControllerButtonState(int slot, ButtonName button, bool down);

    // The sampler (read-only), for tests.
    const InputSampler& sampler() const { return m_sampler; }

  public:
    bool usingControllers=false;
    map<int, array<KEY, NUM_ACTION_BUTTONS>> m_keyMap; // SLOT (playerNumber - 1), keys available to use

  protected:
    vector<shared_ptr<PlayerCharacterEntity>> p_players; // pointers to player character entities
    InfoPanel * p_infoPanel = nullptr;
    // The local half of input: what the devices did since the last tick. No game
    // state lives here any more (the held action and the selected item are the
    // simulation's, per slot; see Simulation::inputState).
    InputSampler m_sampler;

  private:
    void setAvailableKeys();
    vector<array<KEY, NUM_ACTION_BUTTONS>> m_availableKeySets; // fixed table of key sets, indexed by attachedKeyboardId (rebuilt each game).

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
