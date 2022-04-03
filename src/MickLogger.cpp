/*
 * MickLogger.cpp
 *
 *  Created on: Apr 3, 2022
 *      Author: msaun
 */

#include "MickLogger.h"
#include "Logger.h"
#include <iostream>
#include <string>

namespace std
{
  // super basic logging.
  // toggle DEBUG_ENABLED in MickLogger.h

  static Logger* loggerInstance = nullptr;

  MickLogger::MickLogger()
  {
    
  }

  Logger* MickLogger::getInstance()
  {
    if (!loggerInstance)
    {
	    loggerInstance = new MickLogger();
    }
    return loggerInstance;
  }

  void MickLogger::debug(void* callingClass, std::string &message)
  {
    log(callingClass, message.c_str(), DEBUG);
  }

  void MickLogger::info(void* callingClass, std::string &message)
  {
    log(callingClass, message.c_str(), INFO);
  }

  void MickLogger::warn(void* callingClass, std::string &message)
  {
    log(callingClass, message.c_str(), WARN);
  }

  void MickLogger::error(void* callingClass, std::string &message)
  {
    log(callingClass, message.c_str(), ERROR);
  }

  void MickLogger::debug(void* callingClass, const char *message)
  {
    log(callingClass, message, DEBUG);
  }

  void MickLogger::info(void* callingClass, const char *message)
  {
    log(callingClass, message, INFO);
  }

  void MickLogger::warn(void* callingClass, const char *message)
  {
    log(callingClass, message, WARN);
  }

  void MickLogger::error(void* callingClass, const char *message)
  {
    log(callingClass, message, ERROR);
  }

  void MickLogger::log(void* callingClass, const char *message, LogLevel level)
  {
    switch(level)
    {
      case DEBUG:
        if(DEBUG_ENABLED)
        {
          cout << "DEBUG: " << typeid(callingClass).name() << " " << message << endl;
        }
        break;

      case INFO:
        cout << "INFO: " << typeid(callingClass).name() << " " << message << endl;
        break;

      case WARN:
        cerr << "WARN: "  << typeid(callingClass).name() << " " << message << endl;
        break;

      case ERROR:
        cerr << "ERROR: " << typeid(callingClass).name() << " " << message << endl;
        break;

      default:
        cerr << "Unknown loglevel: " << level << endl;
        cerr << typeid(callingClass).name() << " " << message << endl;
        break;
    }
  }

  MickLogger::~MickLogger()
  {
  }

} /* namespace std */
