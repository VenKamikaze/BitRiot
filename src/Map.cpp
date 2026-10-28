// implementation of Map.h

#include "Map.h"

int Map::MAP_WIDTH = 17; // usually 17
int Map::MAP_HEIGHT = 13; // usually 13



Map * Map::getInstance()
{
  static Map instance;

  return &instance;
}


int Map::GET_MAP_WIDTH()
{
  return MAP_WIDTH;
}

int Map::GET_MAP_HEIGHT()
{
  return MAP_HEIGHT;
}

Map::StaticTile Map::staticTileAt(int x, int y)
{
  if (!m_staticMap || x < 0 || y < 0 || x >= m_width || y >= m_height)
  {
    return WALL;
  }
  return m_staticMap[x*m_height+y];
}
void Map::setStaticTileAt(int x, int y, Map::StaticTile staticTile)
{
  if (!m_staticMap || x < 0 || y < 0 || x >= m_width || y >= m_height)
  {
    return;
  }
  m_staticMap[x*m_height+y]=staticTile;
}

void Map::init()
{
  // init() can be re-run when the map is resized between rounds, so free any
  // existing allocation first to avoid leaking the previous static map.
  if (m_staticMap)
  {
    delete[] m_staticMap;
    m_staticMap = nullptr;
  }

  m_generation++;
  m_width = MAP_WIDTH;
  m_height = MAP_HEIGHT;
  m_staticMap=new StaticTile[m_width*m_height];


  // initialise basic static map as all empty tiles
  for (int x = 0; x < MAP_WIDTH; ++x)
    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
      setStaticTileAt(x,y,EMPTY);
    }

  // add in outer wall
  for (int x = 0; x < MAP_WIDTH; ++x)
  {
    setStaticTileAt(x,0, WALL);
    setStaticTileAt(x,MAP_HEIGHT - 1, WALL);
  }
  for (int y = 0; y < MAP_HEIGHT; ++y)
  {
    setStaticTileAt(0,y, WALL);
    setStaticTileAt(MAP_WIDTH - 1,y, WALL);
  }
  // now outer walls are done

  // add in extra wall blocks
  for (int x = 2; x < MAP_WIDTH - 2; x+=2)
    for (int y = 2; y < MAP_HEIGHT - 2; y+=2)
    {
      setStaticTileAt(x,y, WALL);
    }
}

void Map::resize(int width, int height)
{
  MAP_WIDTH = width;
  MAP_HEIGHT = height;
  if (!m_staticMap || m_width != width || m_height != height)
  {
    init();
  }
}

Map::Map()
{
  m_staticMap=0;
}

Map::~Map()
{
  if (m_staticMap)
  {
    delete[] m_staticMap;
  }
}

void Map::setBlockAt(int x, int y, bool block)
{
  if (block == true)
  {
    setStaticTileAt(x,y, BLOCK);
  }
  else
  {
    setStaticTileAt(x,y, EMPTY);
  }
}

void Map::clearBlocks()
{
  for (int x = 0; x < m_width; ++x)
  {
    for (int y = 0; y < m_height; ++y)
    {
      if (staticTileAt(x,y) == BLOCK)
      {
        setStaticTileAt(x,y, EMPTY);
      }
    }
  }
}
