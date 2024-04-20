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

};

#endif /* MICKSDLUTIL_H_ */
