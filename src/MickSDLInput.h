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


class MickSDLInput : MickBaseInput
{
  public:
    MickSDLInput(InputHandler *inputHandler);
    virtual ~MickSDLInput();

    SDL_Event* popEvent();
    bool isKeyDown(KEY k);
    bool isKeyReleased(KEY k);
    
    void setKeyState(KEY k, bool down);
    
    std::set<KEY>* getKeysDown();

    void keyEvent(KEY k);

    //KEY_EVENT newEvent();
    void updateEventQueue();

    static MickBaseInput* getInstance(InputHandler *inputHandler);

  protected:
    
    void setControllerInput(SDL_JoystickID joystickID, Uint8 button, Uint8 state);
    
    
    void setupKeymap();
    void setupControllers();
    
    void setupReverseKeymap(std::map<KEY, SDL_Keycode>* forwardMap);
    
  private:
    SDL_Event event;
    std::set<KEY> keysCurrentlyDown;
    std::set<KEY> keysRecentlyReleased;
    
    InputHandler *m_pInputHandler;
    
};


#endif /* MICKSDLINPUT_H_ */
