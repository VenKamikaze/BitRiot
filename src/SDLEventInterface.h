/*
 * SDLEventInterface.h
 *
 *  Created on: May 4, 2022
 *      Author: msaun
 */

#ifndef SDLEVENTINTERFACE_H_
#define SDLEVENTINTERFACE_H_

#include <exception>
#include <string>

namespace std
{

  enum class EventState
  {
    CONTINUE    = 0,
    QUIT        = 1,
    ERROR       = 2
  };

  class SDLEventInterface
  {
    public:
      virtual EventState SDLEventPollAndProcess();
  };
}; /* namespace std */

#endif /* SDLEVENTINTERFACE_H_ */
