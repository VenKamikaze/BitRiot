/*
 * MickSDLAssets.cpp
 *
 *  Created on: Nov 18, 2012
 *      Author: msaun
 */

#include "MickSDLAssets.h"

namespace std {

	MickSDLAssets::MickSDLAssets() {

	}

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

  SDL_Texture* MickSDLAssets::LoadTexture_BMP(SDL_Renderer* renderer, const char* pathfile)
  {
    SDL_Texture* tex = NULL;
    SDL_Surface* surface = Load_BMP(pathfile);
    if(NULL != surface)
    {
      tex = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_FreeSurface(surface);
    }
    return tex;
  }

	MickSDLAssets::~MickSDLAssets() {

	}

} /* namespace std */
