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

namespace std {

	class MickAssets
	{
		public:
			MickAssets();
			static bool checkAssetAvailable(const char* pathfile);

			virtual ~MickAssets();
	};

} /* namespace std */
#endif /* MICKASSETS_H_ */
