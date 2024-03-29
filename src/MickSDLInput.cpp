/*
 * MickSDLInput.cpp
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#include "MickSDLInput.h"
#include "InputHandler.h"
#include "MickLogger.h"
#include "SDL.h"
#include "SDL_error.h"
#include <memory>
#include <string>


using namespace std;

static map<KEY, SDL_Keycode>* translateMap;
static map<SDL_Keycode, KEY>* reverseTranslateMap;
static map<SDL_JoystickID, SDL_Haptic*>* hapticDevices;

MickSDLInput::MickSDLInput(InputHandler *inputHandler) : MickBaseInput()
{
  m_pInputHandler=inputHandler;

  // TODO Auto-generated constructor stub
  aKeyIsDown = false;
  aKeyIsUp = false;
  quitEvent = false;
  translateMap = new map<KEY, SDL_Keycode>();
  reverseTranslateMap = new map<SDL_Keycode, KEY>();
  hapticDevices = new map<SDL_JoystickID, SDL_Haptic*>();
  setupKeymap();
  setupControllers();
}

shared_ptr<MickSDLInput> MickSDLInput::getInstance(InputHandler *inputHandler)
{
  static shared_ptr<MickSDLInput> s_instance = make_shared<MickSDLInput>(inputHandler);
  return s_instance;
}
/*
SDL_Event* MickSDLInput::popEvent()
{
  if(SDL_PollEvent(&event) != 0)
  {
    return &event;
  }

  return NULL;
}
*/

/**
 * Fix this. Shouldn't use isKeyDown to set aKeyIsDown or the vector of keysDown

bool MickSDLInput::isKeyDown(KEY k)
{
  if (keysCurrentlyDown.end() == keysCurrentlyDown.find(k))
    return true;

  if(! event.type == SDL_KEYDOWN)
    return false;

  SDLKey keySDL = translateMap->find(k)->second;
  if(keySDL == event.key.keysym.sym)
  {
    aKeyIsDown = true;
    keysCurrentlyDown.insert(k);
    return true;
  }
  return false;

}

bool MickSDLInput::isKeyReleased(KEY k)
{
  if(! event.type == SDL_KEYUP)
    return false;

  SDLKey keySDL = translateMap->find(k)->second;
  if(keySDL == event.key.keysym.sym)
  {
    keysCurrentlyDown.erase(k);
    aKeyIsDown = (!keysCurrentlyDown.empty());
    return true;

  }
  return false;
}
*/
/**
 * Useless.. started function and decided it didn't work.
 * Figure out if I can use it in some way...
 */
void MickSDLInput::keyEvent(KEY k)
{
  if(event.type == SDL_KEYUP)
  {
    isKeyReleased(k);
  }

  if(event.type == SDL_KEYDOWN)
  {
    if (keysCurrentlyDown.end() == keysCurrentlyDown.find(k))
    {
      return;
    }
  }
}

bool MickSDLInput::isKeyDown(KEY k)
{
  return ( !(keysCurrentlyDown.end() == keysCurrentlyDown.find(k)) );
}

/**
 * Pop off the event when we detect a key was released
 */
bool MickSDLInput::isKeyReleased(KEY k)
{
  bool keyWasReleased = (keysRecentlyReleased.end() != keysRecentlyReleased.find(k));
  if(keyWasReleased)
  {
    keysRecentlyReleased.erase(k);
    aKeyIsUp = (!keysRecentlyReleased.empty());
  }
  return keyWasReleased;
}



void MickSDLInput::setKeyState(KEY key, bool down)
{
  if (down)
  {
    if (!isKeyDown(key))
    {
      aKeyIsDown = true;
      keysCurrentlyDown.insert(key);
      // DEBUG printf("kcd size = %ld\n", keysCurrentlyDown.size());
    }
  }
  else
  {
    if (isKeyDown(key))
    {
      keysRecentlyReleased.insert(key);
      aKeyIsUp = true;
      keysCurrentlyDown.erase(key);
      aKeyIsDown = (!keysCurrentlyDown.empty());
      // DEBUG printf("krr size = %ld\n", keysRecentlyReleased.size());
    }
  }

}

void MickSDLInput::setupControllers()
{
  if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
  {
    MickLogger::getInstance()->warn(this, string("Unable to initialise game controller subsystem. Got error: ").append(SDL_GetError()));
    return;
  }
  m_pInputHandler->usingControllers=true;

  SDL_GameControllerAddMappingsFromFile("data/gamecontrollerdb.txt");

  int numJoysticks=SDL_NumJoysticks();

  MickLogger::getInstance()->debug(this, string("Found controllers: ").append(to_string(numJoysticks)));

  for (int i = 0; i < numJoysticks; ++i)
  {
    if (SDL_IsGameController(i))
    {
      SDL_GameController *controller = SDL_GameControllerOpen(i);
      if (controller)
      {
        MickLogger::getInstance()->debug(this, string("Found a valid controller, named: ").append(SDL_GameControllerName(controller)));
      }
      else
      {
        MickLogger::getInstance()->warn(this, string("Could not open controller: ").append(to_string(i)).append("error: ").append(SDL_GetError()));
      }

    }
  }
}

/**
  * Will try to rumble a joystick at given strength and for milliseconds in time
  * Also sets up a joystick as a haptic device on first call
 **/
bool MickSDLInput::rumbleController(SDL_JoystickID joystickID, float strength, Uint32 ms)
{
  SDL_Haptic* haptic = NULL;
  bool newDevice = false;
  bool rumbled = false;

  if( hapticDevices->find(joystickID) != hapticDevices->end() )
  {
    haptic = hapticDevices->find(joystickID)->second;
  }
  else
  {
    if(SDL_JoystickIsHaptic(SDL_JoystickFromInstanceID(joystickID)) == SDL_TRUE)
    {
      haptic = SDL_HapticOpenFromJoystick(SDL_JoystickFromInstanceID(joystickID));
      if (haptic != NULL && SDL_HapticRumbleInit(haptic) != 0) // 0 == success
      {
        SDL_HapticClose(haptic);
        haptic = NULL;
      }
      newDevice = true;
    }
  }

  if(haptic)
  {
    if (SDL_HapticRumbleSupported(haptic) == SDL_TRUE)
    {
      rumbled = (SDL_HapticRumblePlay(haptic, strength, ms) == 0); // 0 == success
      if (!rumbled)
      {
        printf( "Warning: Unable to play rumble! %s\n", SDL_GetError() );
      }
      else if(newDevice)
      {
        hapticDevices->insert(std::pair<SDL_JoystickID, SDL_Haptic*>(joystickID, haptic));
        printf( "Added controller: %i to haptics! \n", joystickID);
      }
    }
    else
    {
      // Cleaning this up means that it would be re-openned every time rumbleController is called, which is inefficient.
      // Instead, leave it as an available device; hopefully 'SDL_HapticRumbleSupported' is a quick call
      // If not, we can make hapticDevices hold a NULL value for a joystickID where we have a haptic device without rumble (?)
      //SDL_HapticClose(haptic);
      //haptic = NULL;
      //hapticDevices->remove(...);
    }
  }
  return rumbled;
}

void MickSDLInput::setControllerInput(SDL_JoystickID joystickID, Uint8 button, Uint8 state)
{

  shared_ptr<PlayerCharacterEntity> attachedPlayer = m_pInputHandler->getPlayerAttachedToController(joystickID);
  if (!attachedPlayer)
  {
    MickSDLInput::rumbleController(joystickID, 0.75f, 300);
    attachedPlayer = m_pInputHandler->attachNewControllerToPlayer(joystickID);
    if (attachedPlayer)
    {
      MickLogger::getInstance()->info(this, std::string("Attached controller: ").append(to_string(joystickID)).append(" to player: ").append(to_string(attachedPlayer->getTeam())));
    }
    else
    {
      MickLogger::getInstance()->warn(this, std::string("No free players to attach controller: ").append(to_string(joystickID)) );
    }
    return; //eat attaching button press
  }

  int i = attachedPlayer->getID();
  bool pressed=state==SDL_PRESSED;
  KEY keyIndex=KEY_UNKNOWN;

  if (button==SDL_CONTROLLER_BUTTON_A || button==SDL_CONTROLLER_BUTTON_X)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::ACTION1_KEY];
  }
  if (button==SDL_CONTROLLER_BUTTON_B || button==SDL_CONTROLLER_BUTTON_Y)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::ACTION2_KEY];
  }
  if (button==SDL_CONTROLLER_BUTTON_DPAD_UP)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::UP_KEY];
  }
  if (button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::DOWN_KEY];
  }
  if (button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::LEFT_KEY];
  }
  if (button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
  {
    keyIndex=m_pInputHandler->m_keyMap[i][InputHandler::RIGHT_KEY];
  }
  if (keyIndex!=KEY_UNKNOWN)
  {
    setKeyState(keyIndex,pressed); //TODO: mapping directly into the keyboard array, but this is not the best
    //but will require a refactor of processKeyboardInput() to fix
  }
}

void MickSDLInput::updateEventQueue()
{

  while(SDL_PollEvent(&event))
  {
    if(event.type == SDL_KEYUP)
    {
      MickEvent myevent;
      myevent.type = A_KEY_IS_UP;
      myevent.key  = reverseTranslateMap->find(event.key.keysym.sym)->second;
      //newEvents.push(myevent);

      setKeyState(myevent.key, false);

      //printf("keyup: %d, sdl: %d\n", myevent.key, event.key.keysym.sym);
    }
    else if (event.type == SDL_KEYDOWN)
    {
      // add ESC here for now to be able to quit fullscreen
      if (event.key.keysym.sym == SDLK_ESCAPE)
      {
        quitEvent = true;
      }

      MickEvent myevent;
      myevent.type = A_KEY_IS_DOWN;
      myevent.key  = reverseTranslateMap->find(event.key.keysym.sym)->second;
      //printf("keydown: %d, sdl: %d\n", myevent.key, event.key.keysym.sym);
      //newEvents.push(myevent);

      setKeyState(myevent.key, true);
    }
    else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
    {
      MickLogger::getInstance()->debug(this, string("Got controller button event: ").append(to_string(event.type)));
      setControllerInput(event.jbutton.which, event.jbutton.button, event.jbutton.state);
    }
    else if (event.type == SDL_CONTROLLERDEVICEADDED)
    {
      MickLogger::getInstance()->debug(this, string("New gamecontroller found: ").append(to_string(event.jbutton.which)));
      if (SDL_IsGameController(event.jbutton.which))
      {
        SDL_GameControllerOpen(event.jbutton.which);
      }
    }
    else if (event.type == SDL_CONTROLLERDEVICEREMOVED)
    {
      MickLogger::getInstance()->debug(this, string("Gamecontroller removed: ").append(to_string(event.jbutton.which)));
      m_pInputHandler->detachController(event.jbutton.which);
      if( hapticDevices->find(event.jbutton.which) != hapticDevices->end() )
      {
        SDL_Haptic* haptic = hapticDevices->find(event.jbutton.which)->second;
        SDL_HapticClose(haptic);
        haptic = nullptr;
        hapticDevices->erase(event.jbutton.which);
        MickLogger::getInstance()->debug(this, string("Haptics removed: ").append(to_string(event.jbutton.which)));
      }
    }
    else if (event.type == SDL_QUIT)
    {
      quitEvent = true;
    }
  }
}

set<KEY>* MickSDLInput::getKeysDown()
{
  return &keysCurrentlyDown;
}

SDL_Keycode MickSDLInput::getSDLKeycodeForKey(KEY k)
{
  return translateMap->find(k)->second;
}

void MickSDLInput::setupKeymap()
{
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_UNKNOWN, SDLK_UNKNOWN  ));
  //translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_FIRST    , SDLK_FIRST    ));  // not available in SDL2 ?
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_BACKSPACE, SDLK_BACKSPACE  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_TAB, SDLK_TAB    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_CLEAR, SDLK_CLEAR    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RETURN, SDLK_RETURN   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_PAUSE, SDLK_PAUSE    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_ESCAPE, SDLK_ESCAPE   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_SPACE, SDLK_SPACE    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_EXCLAIM, SDLK_EXCLAIM  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_QUOTE, SDLK_QUOTEDBL ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_HASH, SDLK_HASH   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_DOLLAR, SDLK_DOLLAR   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_AMPERSAND, SDLK_AMPERSAND  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_APOSTROPHE, SDLK_QUOTE    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LEFTPAREN, SDLK_LEFTPAREN  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RIGHTPAREN, SDLK_RIGHTPAREN ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_ASTERISK, SDLK_ASTERISK ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_PLUS, SDLK_PLUS   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_COMMA, SDLK_COMMA    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_MINUS, SDLK_MINUS    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_PERIOD, SDLK_PERIOD   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_SLASH, SDLK_SLASH    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_0, SDLK_0      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_1, SDLK_1      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_2, SDLK_2      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_3, SDLK_3      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_4, SDLK_4      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_5, SDLK_5      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_6, SDLK_6      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_7, SDLK_7      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_8, SDLK_8      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_9, SDLK_9      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_COLON, SDLK_COLON    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_SEMICOLON, SDLK_SEMICOLON  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LESS, SDLK_LESS   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_EQUALS, SDLK_EQUALS   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_GREATER, SDLK_GREATER  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_QUESTION, SDLK_QUESTION ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_AT, SDLK_AT));

  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LEFTBRACKET, SDLK_LEFTBRACKET  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_BACKSLASH, SDLK_BACKSLASH    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RIGHTBRACKET, SDLK_RIGHTBRACKET ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_CARET, SDLK_CARET    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_UNDERSCORE, SDLK_UNDERSCORE ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_BACKQUOTE, SDLK_BACKQUOTE  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_a, SDLK_a      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_b, SDLK_b      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_c, SDLK_c      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_d, SDLK_d      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_e, SDLK_e      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_f, SDLK_f      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_g, SDLK_g      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_h, SDLK_h      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_i, SDLK_i      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_j, SDLK_j      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_k, SDLK_k      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_l, SDLK_l      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_m, SDLK_m      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_n, SDLK_n      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_o, SDLK_o      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_p, SDLK_p      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_q, SDLK_q      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_r, SDLK_r      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_s, SDLK_s      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_t, SDLK_t      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_u, SDLK_u      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_v, SDLK_v      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_w, SDLK_w      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_x, SDLK_x      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_y, SDLK_y      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_z, SDLK_z      ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_DELETE, SDLK_DELETE   ));

  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP0, SDLK_KP_0   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP1, SDLK_KP_1   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP2, SDLK_KP_2   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP3, SDLK_KP_3   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP4, SDLK_KP_4   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP5, SDLK_KP_5   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP6, SDLK_KP_6   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP7, SDLK_KP_7   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP8, SDLK_KP_8   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP9, SDLK_KP_9   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_PERIOD, SDLK_KP_PERIOD  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_DIVIDE, SDLK_KP_DIVIDE  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_MULTIPLY, SDLK_KP_MULTIPLY));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_MINUS, SDLK_KP_MINUS ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_PLUS, SDLK_KP_PLUS  ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_ENTER, SDLK_KP_ENTER ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_KP_EQUALS, SDLK_KP_EQUALS  ));

  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_UP, SDLK_UP     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_DOWN, SDLK_DOWN   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RIGHT, SDLK_RIGHT    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LEFT, SDLK_LEFT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_INSERT, SDLK_INSERT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_HOME, SDLK_HOME   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_END, SDLK_END    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_PAGEUP, SDLK_PAGEUP   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_PAGEDOWN, SDLK_PAGEDOWN ));

  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F1, SDLK_F1     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F2, SDLK_F2     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F3, SDLK_F3     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F4, SDLK_F4     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F5, SDLK_F5     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F6, SDLK_F6     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F7, SDLK_F7     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F8, SDLK_F8     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F9, SDLK_F9     ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F10, SDLK_F10    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F11, SDLK_F11    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F12, SDLK_F12    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F13, SDLK_F13    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F14, SDLK_F14    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_F15, SDLK_F15    ));

  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_NUMLOCK, SDLK_NUMLOCKCLEAR ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_CAPSLOCK, SDLK_CAPSLOCK ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_SCROLLOCK, SDLK_SCROLLLOCK ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RSHIFT, SDLK_RSHIFT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LSHIFT, SDLK_LSHIFT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RCTRL, SDLK_RCTRL    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LCTRL, SDLK_LCTRL    ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RALT, SDLK_RALT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LALT, SDLK_LALT   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RMETA, SDLK_RGUI   ));
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LMETA, SDLK_LGUI   ));
  //translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_LSUPER   , SDLK_LSUPER   ));  // not available in SDL2 ?
  //translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_RSUPER   , SDLK_RSUPER   ));  // not available in SDL2 ?
  translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_MODE, SDLK_MODE   ));
  //translateMap->insert(std::pair<KEY, SDL_Keycode>(KEY_COMPOSE  , SDLK_COMPOSE  ));  // not available in SDL2 ?

  setupReverseKeymap(translateMap);
}

void MickSDLInput::setupReverseKeymap(map<KEY, SDL_Keycode>* forwardMap)
{
  map<KEY, SDL_Keycode>::reverse_iterator rTransIt;
  for ( rTransIt=translateMap->rbegin() ; rTransIt != translateMap->rend(); rTransIt++ )
  {
    reverseTranslateMap->insert(std::pair<SDL_Keycode, KEY>(rTransIt->second, rTransIt->first));
    //printf("reversemap size: %lu\n",reverseTranslateMap->size());
  }
}

MickSDLInput::~MickSDLInput()
{
  translateMap->clear();
  delete translateMap;
  translateMap = NULL;

  reverseTranslateMap->clear();
  delete reverseTranslateMap;
  reverseTranslateMap = NULL;
}

