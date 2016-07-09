/*
 * MickUtil.h
 *
 *  Created on: Mar 6, 2012
 *      Author: msaun
 */

#ifndef MICKUTIL_H_
#define MICKUTIL_H_

#include "string"

#if defined (__GNUC__) && defined(__unix__)
#define CheckFileExists CheckFileExists_nix
#include <sys/stat.h>
#include <unistd.h>
#elif defined (WIN32)
#define CheckFileExists CheckFileExists_win32
#include "windows.h"
#include "shlwapi.h"
#endif

namespace std
{

class MickUtil
{
  public:
    MickUtil();
    virtual ~MickUtil();

    static bool CheckFileExists_nix(string pathfile);
    static bool CheckFileExists_win32(string pathfile);
};

} /* namespace std */
#endif /* MICKUTIL_H_ */
