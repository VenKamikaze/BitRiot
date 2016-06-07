/*
 * SDLHelper.h
 *
 *  Created on: Mar 4, 2012
 *      Author: msaun
 */

#ifndef SDLHELPER_H_
#define SDLHELPER_H_

#include "SDL/SDL.h"

namespace std {

class SDLHelper
{
	public:
		SDLHelper();
		virtual ~SDLHelper();

		// SURFACE MUST BE LOCKED BEFORE CALLING
		static Uint32 GetPixel(SDL_Surface* surface, int x, int y );

		// SURFACE MUST BE LOCKED BEFORE CALLING
		static void PutPixel(SDL_Surface* surface, int x, int y, Uint32 pixel );

};

} /* namespace std */
#endif /* SDLHELPER_H_ */
