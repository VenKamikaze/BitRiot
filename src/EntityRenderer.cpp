// implementation of EntityRenderer class

#include "EntityRenderer.h"

// following used to change the colour of entities to their team colours
//static const unsigned int TRANSPARENT_COLOR = (255 << 8) + (255 << 0);

static const unsigned int TEAM_ORIGINAL_HIGHLIGHT_COLOR = (238 << 16) + (166 << 8) + 247;
static const unsigned int TEAM_ORIGINAL_BASE_COLOR = (200 << 16) + (23 << 8) + 221;
static const unsigned int TEAM_ORIGINAL_SHADOW_COLOR = (142 << 16) + (16 << 8) + 158;

static bool teamColoursInitialised = false;

Uint32 EntityRenderer::TEAM_HIGHLIGHT_COLOR[] = {0,0,0,0,0};
Uint32 EntityRenderer::TEAM_SHADOW_COLOR[] = {0,0,0,0,0};
Uint32 EntityRenderer::TEAM_BASE_COLOR[] = {0,0,0,0,0};



EntityRenderer::EntityRenderer(SDL_Surface* sdl_primary, const char * filename)
{
	/*
	TEAM_HIGHLIGHT_COLOR[NUM_TEAMS] =
	{
		SDL_MapRGB(sdl_primary->format, 238, 166, 247), // team 0 - computer team highlight
		SDL_MapRGB(sdl_primary->format, 247, 166, 166), // team 1 - red team
		SDL_MapRGB(sdl_primary->format, 166, 185, 247), // team 2 - blue team
		SDL_MapRGB(sdl_primary->format, 186, 247, 192), // team 3 - green team
		SDL_MapRGB(sdl_primary->format, 241, 237, 166) // team 4 - yellow team
	};
	*/

	/*
	TEAM_BASE_COLOR[NUM_TEAMS] =
	{
		SDL_MapRGB(sdl_primary->format, 200, 23, 221), // team 0 - computer team base colour
		SDL_MapRGB(sdl_primary->format, 221, 23, 23), // team 1 - red team
		SDL_MapRGB(sdl_primary->format, 23, 69, 221), // team 2 - blue team
		SDL_MapRGB(sdl_primary->format, 72, 221, 86), // team 3 - green team
		SDL_MapRGB(sdl_primary->format, 208, 196, 23) // team 4 - yellow team
	};
*/
	/*
	TEAM_SHADOW_COLOR[NUM_TEAMS] =
	{
		SDL_MapRGB(sdl_primary->format, 142, 16, 158), // team 0 - computer team shadow colour
		SDL_MapRGB(sdl_primary->format, 158, 16, 16), // team 1 - red team
		SDL_MapRGB(sdl_primary->format, 16, 49, 158), // team 2 - blue team
		SDL_MapRGB(sdl_primary->format, 51, 158, 61), // team 3 - green team
		SDL_MapRGB(sdl_primary->format, 148, 140, 16) // team 4 - yellow team
	};
*/

	if (! teamColoursInitialised)
	{
		TEAM_HIGHLIGHT_COLOR[0] = SDL_MapRGB(sdl_primary->format, 238, 166, 247); // team 0 - computer team highlight
		TEAM_HIGHLIGHT_COLOR[1] = SDL_MapRGB(sdl_primary->format, 247, 166, 166); // team 1 - red team
		TEAM_HIGHLIGHT_COLOR[2] = SDL_MapRGB(sdl_primary->format, 166, 185, 247); // team 2 - blue team
		TEAM_HIGHLIGHT_COLOR[3] = SDL_MapRGB(sdl_primary->format, 186, 247, 192); // team 3 - green team
		TEAM_HIGHLIGHT_COLOR[4] = SDL_MapRGB(sdl_primary->format, 241, 237, 166); // team 4 - yellow team

		TEAM_BASE_COLOR[0] = SDL_MapRGB(sdl_primary->format, 200, 23, 221); // team 0 - computer team base colour
		TEAM_BASE_COLOR[1] = SDL_MapRGB(sdl_primary->format, 221, 23, 23); // team 1 - red team
		TEAM_BASE_COLOR[2] = SDL_MapRGB(sdl_primary->format, 23, 69, 221); // team 2 - blue team
		TEAM_BASE_COLOR[3] = SDL_MapRGB(sdl_primary->format, 72, 221, 86); // team 3 - green team
		TEAM_BASE_COLOR[4] = SDL_MapRGB(sdl_primary->format, 208, 196, 23); // team 4 - yellow team

		TEAM_SHADOW_COLOR[0] = SDL_MapRGB(sdl_primary->format, 142, 16, 158); // team 0 - computer team shadow colour
		TEAM_SHADOW_COLOR[1] = SDL_MapRGB(sdl_primary->format, 158, 16, 16); // team 1 - red team
		TEAM_SHADOW_COLOR[2] = SDL_MapRGB(sdl_primary->format, 16, 49, 158); // team 2 - blue team
		TEAM_SHADOW_COLOR[3] = SDL_MapRGB(sdl_primary->format, 51, 158, 61); // team 3 - green team
		TEAM_SHADOW_COLOR[4] = SDL_MapRGB(sdl_primary->format, 148, 140, 16); // team 4 - yellow team
		teamColoursInitialised = true;

	}

	// load anim surface from file using ddutil.h
	//m_animSurface[0] = DDLoadBitmap(lpdd, filename, 0, 0);
	SDL_Surface* tempSurface = SDL_LoadBMP(filename);

	//DDSURFACEDESC2 ddsd;
	//memset(&ddsd,0,sizeof(ddsd));
	//ddsd.dwSize = sizeof(ddsd);

	//m_animSurface[0]->GetSurfaceDesc(&ddsd);

	if(tempSurface)
	{
		m_animSurface[0] = SDL_DisplayFormat( tempSurface );

		SDL_FreeSurface(tempSurface);
		tempSurface = NULL;
	}

	// set number of frames based on width
	for (int i = 0; i < NUM_TEAMS; ++i)
	{
		m_numFrames[i] = m_animSurface[0]->w / Map::TILE_WIDTH;
	}

	// copy loaded bitmap to other surfaces
	// set surface to offscreen type
	//ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY; // M2S not sure..

	//RECT sourceRect, destRect;
	/*sourceRect.top = destRect.top = 0;
	sourceRect.left = destRect.left = 0;
	sourceRect.bottom = destRect.bottom = ddsd.dwHeight;
	sourceRect.right = destRect.right = ddsd.dwWidth;*/

	SDL_Rect sourceRect, destRect;
	sourceRect.y = destRect.y = 0;
	sourceRect.x = destRect.x = 0;
	sourceRect.h = destRect.h = m_animSurface[0]->h;  // M2S check this, seems like it will show all frames on one surface
	sourceRect.w = destRect.w = m_animSurface[0]->w;  // M2S check this, seems like it will show all frames on one surface

	for (int i = 1; i < NUM_TEAMS;++i)
	{
		// create surface
	    //lpdd->CreateSurface(&ddsd, &(m_animSurface[i]), NULL);

		SDL_Surface* tempSurface = SDL_CreateRGBSurface(SDL_SWSURFACE, m_animSurface[0]->w, m_animSurface[0]->h, m_animSurface[0]->format->BitsPerPixel, m_animSurface[0]->format->Rmask, m_animSurface[0]->format->Gmask, m_animSurface[0]->format->Bmask, 0 );
		// copy to this surface
		//m_animSurface[i]->Blt(&destRect, m_animSurface[0], &sourceRect, DDBLT_WAIT, NULL);
		SDL_BlitSurface(m_animSurface[0], &sourceRect, tempSurface, &destRect);
		m_animSurface[i] = tempSurface;
	}

	// set transparent color key
	//DDCOLORKEY key;
	//key.dwColorSpaceLowValue = TRANSPARENT_COLOR;
	//key.dwColorSpaceHighValue = TRANSPARENT_COLOR;

	// change to team colours and apply color key
	for (int i = 0; i < NUM_TEAMS; ++i)
	{
		changeToTeamColour(m_animSurface[i], i);

		SDL_SetColorKey( m_animSurface[i], SDL_RLEACCEL | SDL_SRCCOLORKEY, SDL_MapRGB(m_animSurface[i]->format, 0, 255, 255) );
		//m_animSurface[i]->SetColorKey(DDCKEY_SRCBLT, &key);
	}	

}


EntityRenderer::~EntityRenderer()
{
	// if pointer is valid, release the surfaces
	for (int i = 0; i < NUM_TEAMS; ++i)
		if (m_animSurface[i])
		{
			SDL_FreeSurface(m_animSurface[i]);
			m_animSurface[i] = NULL;
			//m_animSurface[i]->Release();
		}

}

bool EntityRenderer::render(SDL_Surface* dest, int destX, int destY, int state, int animFrame, int team)
{
	if (state < 0 || state >= MAX_ANIM_STATES)
		return false;
	if (animFrame < 0 || animFrame >= m_numFrames[team])
		return false;
	if (team < 0 || team >= NUM_TEAMS)
		return false;

	//RECT sourceRect, destRect;
	SDL_Rect sourceRect, destRect;

	sourceRect.x = animFrame * Map::TILE_WIDTH;
	//sourceRect.w = (animFrame + 1) * Map::TILE_WIDTH;
	sourceRect.w = Map::TILE_WIDTH;
	sourceRect.y = state * Map::TILE_HEIGHT;
	//sourceRect.h = (state + 1) * Map::TILE_HEIGHT;
	sourceRect.h = Map::TILE_HEIGHT;

	destRect.x = destX;
	destRect.w = destX + Map::TILE_WIDTH;
	destRect.y = destY;
	destRect.h = destY + Map::TILE_HEIGHT;

	//HRESULT result = dest->Blt(&destRect, m_animSurface[team], &sourceRect, DDBLT_WAIT | DDBLT_KEYSRC, NULL);
	//printf("EntityRenderer::render - m_animSurface[%d]-w: %d, sourceRect.w=%d\n", team, m_animSurface[team]->w, sourceRect.w );
	if (SDL_BlitSurface(m_animSurface[team], &sourceRect, dest, &destRect) != 0)
	{
		printf("EntityRenderer::render - SDL_BlitSurface failed: %s\n", SDL_GetError());
		return false;
	}

	return true;

}

void EntityRenderer::changeToTeamColour(SDL_Surface* surface, int team)
{
	//DDSURFACEDESC2 ddsd;
	//memset(&ddsd,0,sizeof(ddsd));
	//ddsd.dwSize = sizeof(ddsd);

	// lock surface for manipulation
	//surface->Lock(NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);
	if(SDL_MUSTLOCK(surface))
	{
		SDL_LockSurface(surface);
	}
	//surface->Lock(NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);

	//unsigned int * buffer = (unsigned int *)ddsd.lpSurface;
	//Uint32* buffer = (Uint32* )surface->pixels;

	// loop through all of image
	for (int32_t x = 0; x < surface->w; ++x)
	{
		for (int32_t y = 0; y < surface->h; ++y)
		{
			int32_t pixel = SDLHelper::GetPixel(surface, x, y);
			//Uint32 pixel = buffer[x + y*(ddsd.lPitch >> 2)];

			if (pixel == TEAM_ORIGINAL_HIGHLIGHT_COLOR)
			{
				SDLHelper::PutPixel(surface, x, y, TEAM_HIGHLIGHT_COLOR[team]);
				//buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_HIGHLIGHT_COLOR[team];
			}
			else
			{
				if (pixel == TEAM_ORIGINAL_BASE_COLOR)
				{
					SDLHelper::PutPixel(surface, x, y, TEAM_BASE_COLOR[team]);
					//buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_BASE_COLOR[team];
				}
				else if (pixel == TEAM_ORIGINAL_SHADOW_COLOR)
				{
					SDLHelper::PutPixel(surface, x, y, TEAM_SHADOW_COLOR[team]);
					//buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_SHADOW_COLOR[team];
				}
			}
		}
	}

	if(SDL_MUSTLOCK(surface))
	{
		SDL_UnlockSurface(surface);
	}

	/*
	// loop through all of image
	for (unsigned int x = 0; x < ddsd.dwWidth; ++x)
	{
		for (unsigned int y = 0; y < ddsd.dwHeight; ++y)
		{
			unsigned int pixel = buffer[x + y*(ddsd.lPitch >> 2)];
			if (pixel == TEAM_ORIGINAL_HIGHLIGHT_COLOR)
			{
				buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_HIGHLIGHT_COLOR[team];
			} else {
				if (pixel == TEAM_ORIGINAL_BASE_COLOR)
				{
					buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_BASE_COLOR[team];
				} else {
					if (pixel == TEAM_ORIGINAL_SHADOW_COLOR)
					{
						buffer[x + y*(ddsd.lPitch >> 2)] = TEAM_SHADOW_COLOR[team];
					}
				}
			}
		}
	}
*/
	//surface->Unlock(NULL);

}

bool EntityRenderer::replaceTeamSurfaceWithImage(const char * filename, int team)
{
	// load anim surface from file using ddutil.h
	//LPDIRECTDRAWSURFACE7 tempSurface = DDLoadBitmap(lpdd, filename, 0, 0);
	SDL_Surface* tempSurface = SDL_LoadBMP(filename);

	if (tempSurface)
	{
		SDL_Surface* optSurface = SDL_DisplayFormat( tempSurface );
		// release current surface
		if (m_animSurface[team])
		{
			//m_animSurface[team]->Release();
			SDL_FreeSurface(m_animSurface[team]);
		}
		// set surface from temp
		m_animSurface[team] = optSurface;

		// set number of frames		
		//DDSURFACEDESC2 ddsd;
		//memset(&ddsd,0,sizeof(ddsd));
		//ddsd.dwSize = sizeof(ddsd);
		//m_animSurface[team]->GetSurfaceDesc(&ddsd);
		
		//printf("Not sure about this - CHECK M2S surface width of BMP: %d", m_animSurface[team]->w);
		m_numFrames[team] = m_animSurface[team]->w / Map::TILE_WIDTH;

		//DDCOLORKEY key;
		//key.dwColorSpaceLowValue = TRANSPARENT_COLOR;
		//key.dwColorSpaceHighValue = TRANSPARENT_COLOR;
		//m_animSurface[team]->SetColorKey(DDCKEY_SRCBLT, &key);

		SDL_SetColorKey(m_animSurface[team], SDL_SRCCOLORKEY, SDL_MapRGB(m_animSurface[team]->format, 0, 255, 255));

		if(tempSurface)
		{
			SDL_FreeSurface(tempSurface);
			tempSurface = NULL;
		}

		return true;
	} else
		return false;
}
