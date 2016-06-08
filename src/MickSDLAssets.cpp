/*
 * MickSDLAssets.cpp
 *
 *  Created on: Nov 18, 2012
 *      Author: msaun
 */

#include "MickSDLAssets.h"

namespace std {

	MickSDLAssets::MickSDLAssets() {
		// TODO Auto-generated constructor stub

	}

//	SDL_Surface* Load_BMP(string pathfile)
//	{
//		return Load_BMP(pathfile.c_str());
//	}

	SDL_Surface* MickSDLAssets::Load_BMP(const char* pathfile)
	{
		if(! MickUtil::CheckFileExists(pathfile))
		{
			string fname(pathfile);
			RuntimeException e;
			e.setMessage( "MickSDLAssets::Load_BMP-Resource not found: " + fname);
			throw e;
		}

		return SDL_LoadBMP(pathfile);
	}

	MickSDLAssets::~MickSDLAssets() {
		// TODO Auto-generated destructor stub
	}

} /* namespace std */
