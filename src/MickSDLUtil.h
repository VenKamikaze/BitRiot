/*
 * MickSDLUtil.h
 *
 *  Created on: May 19, 2022
 *      Author: msaun
 */

#ifndef MICKSDLUTIL_H_
#define MICKSDLUTIL_H_

#include "MickLogger.h"
#include "SDL_joystick.h"
#include "SDL.h"
#include <map>


class MickSDLUtil
{
  public:
    MickSDLUtil();
    virtual ~MickSDLUtil();

    static void setupControllers();
    static bool rumbleController(SDL_JoystickID joystickID, float strength, Uint32 ms);
    static SDL_JoystickID getPrimaryJoystickID();
    static void closeHaptic(SDL_JoystickID joystickID);

  private:
    // Meyers singleton: guarantees the map is allocated before first use,
    // unlike the previous never-allocated file-static pointer.
    static std::map<SDL_JoystickID, SDL_Haptic*>& hapticDevices();
};

#endif /* MICKSDLUTIL_H_ */
