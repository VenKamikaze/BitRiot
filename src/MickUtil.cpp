/*
 * MickUtil.cpp
 *
 *  Created on: Mar 6, 2012
 *      Author: msaun
 */

#include "MickUtil.h"

namespace std {

MickUtil::MickUtil() {
	// TODO Auto-generated constructor stub

}

bool MickUtil::CheckFileExists_nix(string pathfile)
{
#if defined (__GNUC__) && defined(__unix__)
//    struct stat buf;
//    return (stat(pathfile.c_str(), &buf) != -1);
	return (access( pathfile.c_str(), R_OK ) != -1);
#else
	return false;
#endif
}

bool MickUtil::CheckFileExists_win32(string pathfile)
{
#if not defined (WIN32)
	return false;
#else
	return PathFileExists(pathfile.c_str());
#endif

}


MickUtil::~MickUtil() {
	// TODO Auto-generated destructor stub
}

} /* namespace std */
