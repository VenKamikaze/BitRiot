/*
 * GameRunningSDLInput.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */
#ifndef GAMERUNNINGSDLINPUT_H_
#define GAMERUNNINGSDLINPUT_H_

#include "MickBaseInput.h"
class InputHandler;
#include "PlayerCharacterEntity.h"

#include "MickLogger.h"
#include "SDL.h"
#include "SDL_error.h"

#include <SDL2/SDL.h>
#include <cstddef>
#include <string>
#include <map>
#include <set>
#include <memory>
#include <array>


class GameRunningSDLInput : public MickBaseInput, public std::enable_shared_from_this<GameRunningSDLInput>
{
  public:
    GameRunningSDLInput(std::shared_ptr<InputHandler> inputHandler);
    static std::shared_ptr<GameRunningSDLInput> create(std::shared_ptr<InputHandler> inputHandler);
    virtual ~GameRunningSDLInput();

    void resetInputEvents(); // reset input event arrays
    SDL_Event* popEvent();
    bool isKeyDown(KEY k);
    bool isKeyReleased(KEY k);

    void setKeyState(KEY k, bool down);

    std::set<KEY>* getKeysDown();
    std::shared_ptr<InputHandler> getInputHandler();

    int getPlayerIdForKey(SDL_Keycode key);
    int getKeyboardSetForActionKey(SDL_Keycode key); // keyboard-set index for an action keypress, or -1 (player-select keyboard-join)

    void updateEventQueue();
    // How many SDL events updateEventQueue() has polled since the last call to this
    // (which resets it). Lets the engine tell a frame where something may have
    // changed on screen (a join indicator, say) from one where nothing did.
    int takeEventCount() { int n = m_eventsPolled; m_eventsPolled = 0; return n; }

    static std::shared_ptr<GameRunningSDLInput> getInstance();
    static bool rumbleController(SDL_JoystickID joystickID, float strength, Uint32 length);
    static SDL_JoystickID getPrimaryJoystickID();

    SDL_Keycode getSDLKeycodeForKey(KEY k); // needed for libRocket key translation map
    static map<SDL_Keycode, KEY>* reverseTranslateMap;

  protected:
    void setControllerInput(SDL_JoystickID joystickID, Uint8 button, Uint8 state);

    void setupKeymap();
    void setupControllers();

    void setupReverseKeymap(std::map<KEY, SDL_Keycode>* forwardMap);

  private:
    SDL_Event event;
    int m_eventsPolled = 0;
    std::set<KEY> keysCurrentlyDown;
    std::set<KEY> keysRecentlyReleased;

    std::shared_ptr<InputHandler> m_pInputHandler;

};


#endif /* GAMERUNNINGSDLINPUT_H_ */
