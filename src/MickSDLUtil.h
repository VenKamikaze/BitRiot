/*
 * MickSDLUtil.h
 *
 *  Created on: May 19, 2022
 *      Author: msaun
 */

#ifndef MICKSDLUTIL_H_
#define MICKSDLUTIL_H_

#include "MickLogger.h"
#include "RumbleSink.h"
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

// The game's RumbleSink: forwards the simulation's rumble requests to SDL haptics.
// Install it once at start-up with RumbleSink::setInstance(&sink).
class MickSDLRumbleSink : public RumbleSink
{
  public:
    void rumble(int controllerId, int strengthPermille, int durationMs) override
    {
      MickSDLUtil::rumbleController(controllerId, strengthPermille / 1000.0f, durationMs);
    }
};

#endif /* MICKSDLUTIL_H_ */
