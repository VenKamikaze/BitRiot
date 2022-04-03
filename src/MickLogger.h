/*
 * MickLogger.h
 *
 *  Created on: Apr 3, 2022
 *      Author: msaun
 */

#ifndef MICKLOGGER_H_
#define MICKLOGGER_H_

#include "Logger.h"
#include <exception>
#include <string>
#include <iostream>


namespace std
{
  static bool DEBUG_ENABLED = true;

  class MickLogger : public Logger
  {
    public:
      void debug(void* callingClass, std::string &message);
      void info(void* callingClass, std::string &message);
      void warn(void* callingClass, std::string &message);
      void error(void* callingClass, std::string &message);

      void debug(void* callingClass, const char *message);
      void info(void* callingClass, const char *message);
      void warn(void* callingClass, const char *message);
      void error(void* callingClass, const char *message);
      void log(void* callingClass, const char *message, LogLevel level);

      static Logger* getInstance();
    private:
      MickLogger();
      ~MickLogger();

  };
} /* namespace std */

#endif /* MICKLOGGER_H_ */
