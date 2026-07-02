/*
 * MickSDLUtil.cpp
 *
 *  Created on: May 19, 2022
 *      Author: msaun
 */


#include "MickSDLUtil.h"

using namespace std;

map<SDL_JoystickID, SDL_Haptic*>& MickSDLUtil::hapticDevices()
{
  static map<SDL_JoystickID, SDL_Haptic*> devices;
  return devices;
}

MickSDLUtil::MickSDLUtil()
{

}

void MickSDLUtil::setupControllers()
{
  if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
  {
    MickLogger::getInstance()->warn(nullptr, string("Unable to initialise game controller subsystem. Got error: ").append(SDL_GetError()));
    return;
  }

  SDL_GameControllerAddMappingsFromFile("data/gamecontrollerdb.txt");

  int numJoysticks=SDL_NumJoysticks();

  MickLogger::getInstance()->debug(nullptr, string("Found controllers: ").append(to_string(numJoysticks)));

  for (int i = 0; i < numJoysticks; ++i)
  {
    if (SDL_IsGameController(i))
    {
      if (SDL_GameControllerOpen(i))
      {
        MickLogger::getInstance()->debug(nullptr, string("Found a valid controller, named: ").append(SDL_GameControllerNameForIndex(i)));
      }
      else
      {
        MickLogger::getInstance()->warn(nullptr, string("Could not open controller: ").append(to_string(i)).append("error: ").append(SDL_GetError()));
      }

    }
  }
}

/**
  * Will try to rumble a joystick at given strength and for milliseconds in time
  * Also sets up a joystick as a haptic device on first call
 **/
bool MickSDLUtil::rumbleController(SDL_JoystickID joystickID, float strength, Uint32 ms)
{
  SDL_Haptic* haptic = NULL;
  bool newDevice = false;
  bool rumbled = false;

  auto& devices = hapticDevices();
  if( devices.find(joystickID) != devices.end() )
  {
    haptic = devices.find(joystickID)->second;
  }
  else
  {
    SDL_Joystick* joystick = SDL_JoystickFromInstanceID(joystickID);
    if(joystick != NULL && SDL_JoystickIsHaptic(joystick) == SDL_TRUE)
    {
      haptic = SDL_HapticOpenFromJoystick(joystick);
      if (haptic != NULL && SDL_HapticRumbleInit(haptic) != 0) // 0 == success
      {
        SDL_HapticClose(haptic);
        haptic = NULL;
      }
      newDevice = true;
    }
    else if (joystick == NULL)
    {
      return false; // device unplugged since the caller looked it up
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
        devices.insert(std::pair<SDL_JoystickID, SDL_Haptic*>(joystickID, haptic));
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

void MickSDLUtil::closeHaptic(SDL_JoystickID joystickID)
{
  auto& devices = hapticDevices();
  auto it = devices.find(joystickID);
  if(it != devices.end())
  {
    SDL_HapticClose(it->second);
    devices.erase(it);
  }
}

SDL_JoystickID MickSDLUtil::getPrimaryJoystickID()
{
  for(int i = 0; i < SDL_NumJoysticks(); i++)
  {
    if(SDL_IsGameController(i) && SDL_GameControllerOpen(i))
    {
      MickLogger::getInstance()->info(nullptr, std::string("Primary joystickID: ").append(to_string(SDL_JoystickGetDeviceInstanceID(i))));
      return SDL_JoystickGetDeviceInstanceID(i); // get the first that is open and a game controller
    }
  }
  return -1;
}

MickSDLUtil::~MickSDLUtil()
{

}
