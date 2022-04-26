/*
 * Logger.h
 *
 *  Created on: Apr 3, 2022
 *      Author: msaun
 */

#ifndef LOGGER_H_
#define LOGGER_H_

#include <exception>
#include <string>

namespace std
{
  typedef enum
  {
    DEBUG       = 0,
    INFO        = 1,
    WARN        = 2,
    ERROR       = 3
  } LogLevel;


  class Logger
  {
    public:
      virtual void debug(void* callingClass, std::string &message) = 0;
      virtual void info(void* callingClass, std::string &message) = 0;
      virtual void warn(void* callingClass, std::string &message) = 0;
      virtual void error(void* callingClass, std::string &message) = 0;

      virtual void debug(void* callingClass, const char *message) = 0;
      virtual void info(void* callingClass, const char *message) = 0;
      virtual void warn(void* callingClass, const char *message) = 0;
      virtual void error(void* callingClass, const char *message) = 0;
      virtual void log(void* callingClass, const char *message, LogLevel level) = 0;
  };
}; /* namespace std */

#endif /* LOGGER_H_ */
