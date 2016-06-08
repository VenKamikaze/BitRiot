
#include "InfoPanel.h"

static const unsigned int TRANSPARENT_COLOR = (255 << 8) + (255 << 0);

InfoPanel::InfoPanel(SDL_Surface* backbuf, int numPlayers, bool * isMale)
{
	m_players = numPlayers;

	for (int i = 0; i < 4; i++)
		playerPointer[i] = NULL;

	clearSelections();

	// set up surface	
//	DDSURFACEDESC2 ddsd;
//	memset(&ddsd,0,sizeof(ddsd));
//	ddsd.dwSize = sizeof(ddsd);
//	ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
//	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;

//	ddsd.dwWidth = WIDTH;
//	ddsd.dwHeight = HEIGHT;

//	lpdd->CreateSurface(&ddsd, &m_surface, NULL);

	m_surface = SDL_CreateRGBSurface(SDL_HWSURFACE, WIDTH, HEIGHT, backbuf->format->BitsPerPixel, backbuf->format->Rmask, backbuf->format->Gmask, backbuf->format->Bmask, 0 );

	// set up face surfaces
	for (int i = 0; i < 4; i++)
	{
		stringstream ss;
		ss << "bitmaps/player" << (i + 1);
		if (isMale[i])
			ss << "male";
		else
			ss << "female";
		ss << "face.bmp";

		//m_faceSurfaces[i] = DDLoadBitmap(lpdd, ss.str().c_str(), 0, 0);
		m_faceSurfaces[i] = SDL_LoadBMP(ss.str().c_str());


	}

	//DDCOLORKEY key;
	//key.dwColorSpaceLowValue = TRANSPARENT_COLOR;
	//key.dwColorSpaceHighValue = TRANSPARENT_COLOR;

	// change to team colours and apply color key
	for (int i = 0; i < 4; ++i)
	{
		SDL_SetColorKey( m_faceSurfaces[i], SDL_RLEACCEL | SDL_SRCCOLORKEY, SDL_MapRGB(m_faceSurfaces[i]->format, 0, 255, 255) );
	}	

}

InfoPanel::~InfoPanel()
{
	if (m_surface)
	{
		SDL_FreeSurface(m_surface);
		m_surface = NULL;
	}
	for (int i = 0; i < 4; i++)
		if (m_faceSurfaces[i])
		{
			SDL_FreeSurface(m_faceSurfaces[i]);
			m_faceSurfaces[i] = NULL;
		}
		//	m_faceSurfaces[i]->Release();
}

void InfoPanel::setPlayerPointers(int num, 
		PlayerCharacterEntity * player1,
		PlayerCharacterEntity * player2,
		PlayerCharacterEntity * player3,
		PlayerCharacterEntity * player4)
{
	m_players = num;
	playerPointer[0] = player1;
	playerPointer[1] = player2;
	playerPointer[2] = player3;
	playerPointer[3] = player4;
}

void InfoPanel::clearSelections()
{
	for (int t = 0; t < 4; t++)
	{
		playerSelection[t] = PLAYER_CHARACTER; // player character used as null selection
	}
}

void InfoPanel::setSelection(EntityType type, int team)
{
	team--;
	if (0 <= team && team < 4)
	{
		// ok to set array
		playerSelection[team ] = type;
	} 
}

void InfoPanel::renderSurfaceTo(SDL_Surface* dest, int x, int y)
{
	assert(playerPointer[0] != NULL);
	// clear surface
	//DDBLTFX ddbltfx;
	//memset(&ddbltfx,0,sizeof(ddbltfx));
	//ddbltfx.dwSize = sizeof(ddbltfx);
	//ddbltfx.dwFillColor = 0; // color for filling in panel
	//m_surface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);

	// render stuff to surface
	//COLORREF white = 0x00FFFFFF;

	//unused
	//static Uint32 white = SDL_MapRGB(dest->format, 255, 255, 255);

	drawBGTiles();

	
	//RECT sourceRect, destRect;
	SDL_Rect sourceRect, destRect;
	int xOffset, yOffset;
	
	for (int i = 0; i < m_players; i++)
	{
		int health = 0;
		if (m_playerDead[i] != true && playerPointer[i] != NULL)
			health = playerPointer[i]->getHealth();
		xOffset = 0;
		yOffset = ((i * 3) + 1) * Map::TILE_HEIGHT;

		// draw face
		//DDSURFACEDESC2 ddsd;
		//memset(&ddsd,0,sizeof(ddsd));
		//ddsd.dwSize = sizeof(ddsd);
		//m_faceSurfaces[i]->GetSurfaceDesc(&ddsd);

		sourceRect.y = 0;
		sourceRect.x = 0;
		sourceRect.w = m_faceSurfaces[i]->w;
		sourceRect.h = m_faceSurfaces[i]->h;

		destRect.y = yOffset;
		destRect.h = yOffset + (2 * Map::TILE_HEIGHT);
		destRect.x = xOffset;
		destRect.w = xOffset + (2 * Map::TILE_WIDTH);

		//SDL_BlitSurface(m_surface, &sourceRect, m_faceSurfaces[i], &destRect);
		SDL_BlitSurface(m_faceSurfaces[i], &sourceRect, m_surface, &destRect);

		//m_surface->Blt(&destRect, m_faceSurfaces[i], &sourceRect, DDBLT_WAIT | DDBLT_KEYSRC, 0);

			
		xOffset += (2 * Map::TILE_WIDTH);
		if (playerSelection[i] != PLAYER_CHARACTER)
		{
			EntityRenderer * er = EntityRendererFactory::getInstance()->getEntityRenderer(playerSelection[i]);	
			er->render(m_surface, xOffset, yOffset, 0, 0, i + 1);
		}

		// draw health bar
		unsigned char intensity = (unsigned char)(((float)health/100.0f)*255);
		if (health > 100)
		{
			intensity = 255;
		}

		//ddbltfx.dwFillColor = ((255 - intensity) << 16) + (intensity << 8);

		Uint32 fillColour = SDL_MapRGB(m_surface->format, (255-intensity), intensity ,0);

		destRect.y = yOffset + Map::TILE_HEIGHT; // + (int)(1.25f * (float)Map::TILE_HEIGHT);
		/*destRect.h = yOffset + (int)(1.75f * (float)Map::TILE_HEIGHT);*/
		destRect.h = Map::TILE_HEIGHT;
		destRect.x = xOffset + (int)(0.25f * (float)Map::TILE_WIDTH);
		destRect.w = health;

		if (health > 100)
			destRect.w = destRect.x + 100;

		SDL_FillRect(m_surface, &destRect, fillColour);
		//m_surface->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);


		if (health > 100)
		{
			int extraHealth = health - 100;
			intensity = (unsigned char)(((float)extraHealth/100.0f)*255);
			//ddbltfx.dwFillColor = ((255 - intensity) << 8) + (intensity << 0);
			fillColour = SDL_MapRGB(m_surface->format, 0, (255-intensity), intensity);

			destRect.w = destRect.x + extraHealth;

			SDL_FillRect(m_surface, &destRect, fillColour);
			//m_surface->Blt(&destRect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
		}

	}

	/// render surface to destination

	sourceRect.x = 0;
	sourceRect.w = WIDTH;
	sourceRect.y = 0;
	sourceRect.h = HEIGHT;

	destRect.x = x;
	destRect.w = x + WIDTH;
	destRect.y = y;
	destRect.h = y + HEIGHT;

	SDL_BlitSurface(m_surface, &sourceRect, dest, &destRect);
	//dest->Blt(&destRect, m_surface, &sourceRect, DDBLT_WAIT, 0);
	
}

void InfoPanel::setPlayerDead(int player, bool flag)
{
	m_playerDead[player] = flag;
}

void InfoPanel::drawTextGDI(const char * text, int x, int y,
				 Uint32 color, SDL_Surface* dest)
{
	/*
	HDC xdc;

	if (FAILED(dest->GetDC(&xdc)))
		return;

	SetTextColor(xdc, color);

	// set background mode to transparent
	SetBkMode(xdc, TRANSPARENT);

	// draw text out
	TextOut(xdc, x, y, text, (int)strlen(text));

	// release device context (or it'd be locked)
	dest->ReleaseDC(xdc);
*/
	static TTF_Font* font = TTF_OpenFont( "bitmaps/FreeMono.ttf", 10 );
	if(!font)
	{
		SDL_Colour colour;
		colour.b = (color << 0);
		colour.g = (color << 8);
		colour.r = (color << 16);
				//Render the text
		SDL_Surface* message = TTF_RenderText_Solid( font, text, colour);

		//If there was an error in rendering the text
		if( message == NULL )
		{
			cout << "Text render failure! Got null message";
		}

		SDL_Rect destRect;
		destRect.x = x;
		destRect.y = y;
		destRect.w = message->w;
		destRect.h = message->h;

		SDL_BlitSurface(message, NULL, dest, &destRect);
		SDL_FreeSurface(message);

		message = NULL;
	}

}

void InfoPanel::drawBGTiles()
{
	for (int y = 0; y < Map::MAP_HEIGHT; y++)
	{
		for (int x = 0; x < InfoPanel::PANEL_TILE_WIDTH; x++)
		{
			Map::StaticTile tile;
			if ((y%3) == 0)
				tile = Map::WALL;
			else 
			{
				if (x == InfoPanel::PANEL_TILE_WIDTH - 1)
					tile = Map::WALL;
				else
					tile = Map::EMPTY;
			}

			int atX, atY;
			atX = x * Map::TILE_WIDTH;
			atY = y * Map::TILE_HEIGHT;
			Map::getInstance()->renderTileTo(m_surface, atX, atY, tile);


		}


	}


}
