/*
 * MickSDLInput.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#ifndef MICKSDLINPUT_H_
#define MICKSDLINPUT_H_

#include "MickBaseInput.h"
class InputHandler;

#include <SDL2/SDL.h>
#include <map>
#include <set>
#include <memory>

class MickSDLInput : public MickBaseInput, std::enable_shared_from_this<MickSDLInput>
{
  public:
    MickSDLInput(std::shared_ptr<InputHandler> inputHandler);
    virtual ~MickSDLInput();

    void resetInputEvents(); // reset input event arrays
    SDL_Event* popEvent();
    bool isKeyDown(KEY k);
    bool isKeyReleased(KEY k);

    void setKeyState(KEY k, bool down);

    std::set<KEY>* getKeysDown();
    inline std::shared_ptr<InputHandler> getInputHandler()
    {
      return m_pInputHandler;
    }

    void keyEvent(KEY k);

    //KEY_EVENT newEvent();
    void updateEventQueue();

    static std::shared_ptr<MickSDLInput> getInstance(std::shared_ptr<InputHandler> inputHandler);
    static bool rumbleController(SDL_JoystickID joystickID, float strength, Uint32 length);

    SDL_Keycode getSDLKeycodeForKey(KEY k); // needed for libRocket key translation map

  protected:

    void setControllerInput(SDL_JoystickID joystickID, Uint8 button, Uint8 state);

    void setupKeymap();
    void setupControllers();

    void setupReverseKeymap(std::map<KEY, SDL_Keycode>* forwardMap);

  private:
    SDL_Event event;
    std::set<KEY> keysCurrentlyDown;
    std::set<KEY> keysRecentlyReleased;

    std::shared_ptr<InputHandler> m_pInputHandler;

};


#endif /* MICKSDLINPUT_H_ */
