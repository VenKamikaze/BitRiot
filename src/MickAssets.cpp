/*
 * MickAssets.cpp
 *
 *  Created on: Nov 18, 2012
 *      Author: msaun
 */

#include "MickAssets.h"

namespace std
{

MickAssets::MickAssets()
{
  // TODO Auto-generated constructor stub

}

//cacheBitmap?
//cacheSound?

bool checkAssetAvailable(const char* pathfile)
{
  return MickUtil::CheckFileExists(pathfile);
}

MickAssets::~MickAssets()
{
  // TODO Auto-generated destructor stub
}

} /* namespace std */
