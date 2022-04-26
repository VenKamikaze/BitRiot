/*
 * InputException.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#ifndef INPUTEXCEPTION_H_
#define INPUTEXCEPTION_H_

#include <stdexcept>

namespace std
{
  typedef enum
  {
    GOT_QUIT    = 1,
    GOT_OTHER   = 2
  } eType;

  class InputException: virtual public std::runtime_error
  {
    protected:
      eType type;
    public:
      explicit InputException(const std::string& msg, const eType& etype):
        std::runtime_error(msg)
        {
          type = etype;
        }

      virtual ~InputException() throw () {}

      virtual bool gotQuit() const throw()
      {
        if(type)
	{
          return GOT_QUIT == type;
	}
	return false;
      };

  };

} /* namespace std */
#endif /* INPUTEXCEPTION_H_ */
