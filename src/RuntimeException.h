/*
 * RuntimeException.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#ifndef RUNTIMEEXCEPTION_H_
#define RUNTIMEEXCEPTION_H_

#include <exception>
#include <stdio.h>
#include <string>

namespace std
{
	class RuntimeException: public std::exception
	{
		public:
			RuntimeException();
			virtual ~RuntimeException() throw ();

			void setMessage(string msg) {this->message = msg;};
			void printMessage() { printf(this->message.c_str()); };
			const char* getMessage() { return this->message.c_str(); };

			virtual const char* what() const throw()
			{
				string what = "RuntimeException occurred. Message follows:\n " + this->message + "\n";
				return what.c_str();
			}

        private:
            string message;
	};

} /* namespace std */
#endif /* RUNTIMEEXCEPTION_H_ */
