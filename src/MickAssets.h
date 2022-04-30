/*
 * MickAssets.h
 *
 *  Created on: Nov 18, 2012
 *      Author: msaun
 */

#ifndef MICKASSETS_H_
#define MICKASSETS_H_

#include <string>
#include "MickUtil.h"
#include "RuntimeException.h"

namespace std
{

class MickAssets
{
  public:
    enum class PlayerType { MALE, FEMALE }; // eventually: { MALE1, MALE2, FEMALE1, FEMALE2, MACHINE }; // 
    MickAssets();
    static string getPortraitBitmapFilename(int playerIndex, MickAssets::PlayerType type);
    static bool checkAssetAvailable(const char* pathfile);
    static PlayerType getNextPlayerType(PlayerType type);

    virtual ~MickAssets();
};

} /* namespace std */
#endif /* MICKASSETS_H_ */
