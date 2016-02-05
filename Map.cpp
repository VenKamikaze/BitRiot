// implementation of Map.h

#include "Map.h"


Map * Map::getInstance()
{
	static Map instance;

	return &instance;
}

void Map::init(LPDIRECTDRAW7 lpdd)
{
	// initialise basic static map as all empty tiles
	for (int x = 0; x < MAP_WIDTH; ++x)
		for (int y = 0; y < MAP_HEIGHT; ++y)
			m_staticMap[x][y] = EMPTY;

	// add in outer wall
	for (int x = 0; x < MAP_WIDTH; ++x)
	{
		m_staticMap[x][0] = WALL;
		m_staticMap[x][MAP_HEIGHT - 1] = WALL;
	}
	for (int y = 0; y < MAP_HEIGHT; ++y)
	{
		m_staticMap[0][y] = WALL;
		m_staticMap[MAP_WIDTH - 1][y] = WALL;
	}
	// now outer walls are done

	// add in extra wall blocks
	for (int x = 2; x < MAP_WIDTH - 2; x+=2)
		for (int y = 2; y < MAP_HEIGHT - 2; y+=2)
			m_staticMap[x][y] = WALL;

	// set up static tile dd surfaces
	for (int i = 0; i < NUM_STATIC_TILES; ++i)
		m_staticSurface[i] = NULL;

	m_staticSurface[WALL] = DDLoadBitmap(lpdd, "bitmaps/wall.bmp", 0, 0);
	m_staticSurface[EMPTY] = DDLoadBitmap(lpdd, "bitmaps/empty.bmp", 0, 0);
	m_staticSurface[BLOCK] = DDLoadBitmap(lpdd, "bitmaps/empty.bmp", 0, 0);


}

Map::Map()
{
	for (int i = 0; i < NUM_STATIC_TILES; ++i)
		m_staticSurface[i] = NULL;
}

Map::~Map()
{
	for (int i = 0; i < NUM_STATIC_TILES; ++i)
		if (m_staticSurface[i])
			m_staticSurface[i]->Release();
}

void Map::setBlockAt(int x, int y, bool block)
{
	if (block == true)
		m_staticMap[x][y] = BLOCK;
	else
		m_staticMap[x][y] = EMPTY;
}

void Map::clearBlocks()
{
	for (int x = 0; x < MAP_WIDTH; ++x)
		for (int y = 0; y < MAP_HEIGHT; ++y)
			if (m_staticMap[x][y] == BLOCK)
			{			
				m_staticMap[x][y] = EMPTY;
			}
}

void Map::render(LPDIRECTDRAWSURFACE7 dest, int offsetX, int offsetY)
{
	RECT sourceRect, destRect;

	sourceRect.left = 0;
	sourceRect.right = TILE_WIDTH - 1;
	sourceRect.top = 0;
	sourceRect.bottom = TILE_HEIGHT - 1;
	// first render static map, then dynamic entities on top
	for (int x = 0; x < MAP_WIDTH; ++x)
		for (int y = 0; y < MAP_HEIGHT; ++y)
		{
			destRect.left = offsetX + (x*TILE_WIDTH);
			destRect.right = offsetX + (x+1)*TILE_WIDTH;
			destRect.top = offsetY + y*TILE_HEIGHT;
			destRect.bottom = offsetY + (y+1)*TILE_HEIGHT;
			
			// choose source surface from map
			dest->Blt(&destRect, m_staticSurface[m_staticMap[x][y]], &sourceRect, DDBLT_WAIT, NULL);
		}
}

void Map::renderTileTo(LPDIRECTDRAWSURFACE7 dest, int atX, int atY, StaticTile tileType)
{
	RECT sourceRect, destRect;

	sourceRect.left = 0;
	sourceRect.right = TILE_WIDTH - 1;
	sourceRect.top = 0;
	sourceRect.bottom = TILE_HEIGHT - 1;

	destRect.left = atX;
	destRect.right = atX + TILE_WIDTH;
	destRect.top = atY;
	destRect.bottom = atY + TILE_HEIGHT;

	dest->Blt(&destRect, m_staticSurface[tileType], &sourceRect, DDBLT_WAIT, NULL);

}

