// implementation of EntityRenderer class

#include "EntityRenderer.h"

// following used to change the colour of entities to their team colours
static const unsigned int TRANSPARENT_COLOR = (255 << 8) + (255 << 0);

static const unsigned int TEAM_ORIGINAL_HIGHLIGHT_COLOR = (238 << 16) + (166 << 8) + 247;
static const unsigned int TEAM_ORIGINAL_BASE_COLOR = (200 << 16) + (23 << 8) + 221;
static const unsigned int TEAM_ORIGINAL_SHADOW_COLOR = (142 << 16) + (16 << 8) + 158;

static const unsigned int TEAM_HIGHLIGHT_COLOR[NUM_TEAMS] = 
{
	((238 << 16) + (166 << 8) + 247), // team 0 - computer team highlight
	((247 << 16) + (166 << 8) + 166), // team 1 - red team
	((166 << 16) + (185 << 8) + 247), // team 2 - blue team
	((186 << 16) + (247 << 8) + 192), // team 3 - green team
	((241 << 16) + (237 << 8) + 166) // team 4 - yellow team
};
static const unsigned int TEAM_BASE_COLOR[NUM_TEAMS] = 
{
	((200 << 16) + (23 << 8) + 221), // team 0 - computer team base colour
	((221 << 16) + (23 << 8) + 23), // team 1 - red team
	((23 << 16) + (69 << 8) + 221), // team 2 - blue team
	((72 << 16) + (221 << 8) + 86), // team 3 - green team
	((208 << 16) + (196 << 8) + 23) // team 4 - yellow team
};
static const unsigned int TEAM_SHADOW_COLOR[NUM_TEAMS] = 
{
	((142 << 16) + (16 << 8) + 158), // team 0 - computer team shadow colour
	((158 << 16) + (16 << 8) + 16), // team 1 - red team
	((16 << 16) + (49 << 8) + 158), // team 2 - blue team
	((51 << 16) + (158 << 8) + 61), // team 3 - green team
	((148 << 16) + (140 << 8) + 16) // team 4 - yellow team
};

EntityRenderer::EntityRenderer(LPDIRECTDRAW7 lpdd, const char * filename)
{
	// load anim surface from file using ddutil.h
	m_animSurface[0] = DDLoadBitmap(lpdd, filename, 0, 0);

	DDSURFACEDESC2 ddsd;
	memset(&ddsd,0,sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);

	m_animSurface[0]->GetSurfaceDesc(&ddsd);

	// set number of frames based on width
	for (int i = 0; i < NUM_TEAMS; ++i)
	{
		m_numFrames[i] = ddsd.dwWidth / Map::TILE_WIDTH;
	}

	// copy loaded bitmap to other surfaces
	// set surface to offscreen type
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;

	RECT sourceRect, destRect;
	sourceRect.top = destRect.top = 0;
	sourceRect.left = destRect.left = 0;
	sourceRect.bottom = destRect.bottom = ddsd.dwHeight;
	sourceRect.right = destRect.right = ddsd.dwWidth;

	for (int i = 1; i < NUM_TEAMS;++i)
	{
		// create surface
		lpdd->CreateSurface(&ddsd, &(m_animSurface[i]), NULL);
		// copy to this surface
		m_animSurface[i]->Blt(&destRect, m_animSurface[0], &sourceRect, DDBLT_WAIT, NULL);
	}

	// set transparent color key
	DDCOLORKEY key;
	key.dwColorSpaceLowValue = TRANSPARENT_COLOR;
	key.dwColorSpaceHighValue = TRANSPARENT_COLOR;

	// change to team colours and apply color key
	for (int i = 0; i < NUM_TEAMS; ++i)
	{
		changeToTeamColour(m_animSurface[i], i);
		m_animSurface[i]->SetColorKey(DDCKEY_SRCBLT, &key);
	}	

}

EntityRenderer::~EntityRenderer()
{
	// if pointer is valid, release the surfaces
	for (int i = 0; i < NUM_TEAMS; ++i)
		if (m_animSurface[i])
			m_animSurface[i]->Release();
}

bool EntityRenderer::render(LPDIRECTDRAWSURFACE7 dest, int destX, int destY, int state, int animFrame, int team)
{
	if (state < 0 || state >= MAX_ANIM_STATES)
		return false;
	if (animFrame < 0 || animFrame >= m_numFrames[team])
		return false;
	if (team < 0 || team >= NUM_TEAMS)
		return false;

	RECT sourceRect, destRect;

	sourceRect.left = animFrame * Map::TILE_WIDTH;
	sourceRect.right = (animFrame + 1) * Map::TILE_WIDTH;
	sourceRect.top = state * Map::TILE_HEIGHT;
	sourceRect.bottom = (state + 1) * Map::TILE_HEIGHT;

	destRect.left = destX;
	destRect.right = destX + Map::TILE_WIDTH;
	destRect.top = destY;
	destRect.bottom = destY + Map::TILE_HEIGHT;

	HRESULT result = dest->Blt(&destRect, m_animSurface[team], &sourceRect, DDBLT_WAIT | DDBLT_KEYSRC, NULL);
	if (FAILED(result))
	{
		return false;
	}

	return true;

}

void EntityRenderer::changeToTeamColour(LPDIRECTDRAWSURFACE7 surface, int team)
{
	DDSURFACEDESC2 ddsd;
	memset(&ddsd,0,sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);

	// lock surface for manipulation
	surface->Lock(NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);

	unsigned int * buffer = (unsigned int *)ddsd.lpSurface;

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

	surface->Unlock(NULL);

}

bool EntityRenderer::replaceTeamSurfaceWithImage(LPDIRECTDRAW7 lpdd, const char * filename, int team)
{
	// load anim surface from file using ddutil.h
	LPDIRECTDRAWSURFACE7 tempSurface = DDLoadBitmap(lpdd, filename, 0, 0);

	if (tempSurface)
	{
		// release current surface
		if (m_animSurface[team])
			m_animSurface[team]->Release();
		// set surface from temp
		m_animSurface[team] = tempSurface;

		// set number of frames		
		DDSURFACEDESC2 ddsd;
		memset(&ddsd,0,sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);
		m_animSurface[team]->GetSurfaceDesc(&ddsd);
		
		m_numFrames[team] = ddsd.dwWidth / Map::TILE_WIDTH;

		DDCOLORKEY key;
		key.dwColorSpaceLowValue = TRANSPARENT_COLOR;
		key.dwColorSpaceHighValue = TRANSPARENT_COLOR;

		m_animSurface[team]->SetColorKey(DDCKEY_SRCBLT, &key);

		return true;
	} else
		return false;
}