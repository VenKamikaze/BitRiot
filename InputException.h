/*
 * InputException.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#ifndef INPUTEXCEPTION_H_
#define INPUTEXCEPTION_H_

#include <exception>

namespace std
{
	typedef enum
	{
		GOT_QUIT		= 1,
		GOT_OTHER		= 2
	} eType;

	class InputException: public std::exception
	{
		public:
			InputException();
			virtual ~InputException() throw ();

			void setType(eType type) {this->type = type;};
			bool gotQuit() { return type == GOT_QUIT; };

			virtual const char* what() const throw()
			{
				return "InputException occurred.";
			}
			eType type;
	};

} /* namespace std */
#endif /* INPUTEXCEPTION_H_ */
