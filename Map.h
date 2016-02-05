// Map.h
#ifndef _MAPCLASS
#define _MAPCLASS
// this class represents the static game map of wall blocks and empty tiles

// Yet another singleton class (I'm trying not to abuse it, really!)
#include <ddraw.h>
#include "ddutil.h"

enum DIRECTION { UP, DOWN, LEFT, RIGHT };

class Map
{
public:
	static Map * getInstance();
	void init(LPDIRECTDRAW7 lpdd); // need to init surfaces before map can be rendered
	~Map();

	const static int MAP_WIDTH = 17; // usually 17
	const static int MAP_HEIGHT = 13; // usually 13

	const static int TILE_WIDTH = 32;
	const static int TILE_HEIGHT = 32;

	const static int NUM_STATIC_TILES = 3;
	enum StaticTile { WALL, EMPTY, BLOCK };

	void setBlockAt(int x, int y, bool block);
	void clearBlocks();

	void render(LPDIRECTDRAWSURFACE7 destination, int offsetX = 0, int offsetY = 0);

	StaticTile staticTileAt(int x, int y) { return m_staticMap[x][y]; }
	void renderTileTo(LPDIRECTDRAWSURFACE7 dest, int atX, int atY, StaticTile tileType);

private:
	Map();
	// static map data
	StaticTile m_staticMap[MAP_WIDTH][MAP_HEIGHT]; // 2D array of wall tiles
	LPDIRECTDRAWSURFACE7 m_staticSurface[NUM_STATIC_TILES]; // pointers to surfaces for static map

};


#endif

