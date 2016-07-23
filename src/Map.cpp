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
  return m_staticMap[x*MAP_HEIGHT+y];
}
void Map::setStaticTileAt(int x, int y, Map::StaticTile staticTile)
{
  m_staticMap[x*MAP_HEIGHT+y]=staticTile;
}

void Map::init(SDL_Surface* sdl_primary)
{



  m_staticMap=new StaticTile[MAP_WIDTH*MAP_HEIGHT];


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

  // set up static tile dd surfaces
  for (int i = 0; i < NUM_STATIC_TILES; ++i)
  {
    m_staticSurface[i] = NULL;
  }

  SDL_Surface* tempSurface[] = { std::MickSDLAssets::Load_BMP("bitmaps/wall.bmp"), std::MickSDLAssets::Load_BMP("bitmaps/empty.bmp"), std::MickSDLAssets::Load_BMP("bitmaps/empty.bmp") };

  m_staticSurface[WALL] = SDL_ConvertSurface(tempSurface[0], sdl_primary->format, 0); //SDL_LoadBMP("bitmaps/wall.bmp");
  m_staticSurface[EMPTY] = SDL_ConvertSurface(tempSurface[1], sdl_primary->format, 0);  //SDL_LoadBMP("bitmaps/empty.bmp");
  m_staticSurface[BLOCK] =  SDL_ConvertSurface(tempSurface[2], sdl_primary->format, 0); //SDL_LoadBMP("bitmaps/empty.bmp");
  //m_staticSurface[BLOCK] = DDLoadBitmap(surface, "bitmaps/empty.bmp", 0, 0);

  SDL_FreeSurface(tempSurface[0]);
  SDL_FreeSurface(tempSurface[1]);
  SDL_FreeSurface(tempSurface[2]);

  tempSurface[0] = NULL;
  tempSurface[1] = NULL;
  tempSurface[2] = NULL;

}

Map::Map()
{
  m_staticMap=0;

  for (int i = 0; i < NUM_STATIC_TILES; ++i)
  {
    m_staticSurface[i] = NULL;
  }
}

Map::~Map()
{
  if (m_staticMap)
  {
    delete m_staticMap;
  }

  for (int i = 0; i < NUM_STATIC_TILES; ++i)
    if (m_staticSurface[i])
    {
      SDL_FreeSurface(m_staticSurface[i]);
      m_staticSurface[i] = NULL;
    }
  //m_staticSurface[i]->Release();
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
  for (int x = 0; x < MAP_WIDTH; ++x)
  {
    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
      if (staticTileAt(x,y) == BLOCK)
      {
        setStaticTileAt(x,y, EMPTY);
      }
    }
  }
}

void Map::render(SDL_Surface*  dest, int offsetX, int offsetY)
{
  SDL_Rect sourceRect, destRect;



  sourceRect.x = 0;
  sourceRect.w = TILE_WIDTH-1;
  sourceRect.y = 0;
  sourceRect.h = TILE_HEIGHT-1;
  // first render static map, then dynamic entities on top
  for (int x = 0; x < MAP_WIDTH; ++x)
    for (int y = 0; y < MAP_HEIGHT; ++y)
    {
      destRect.x = offsetX + (x*TILE_WIDTH);
      destRect.w = offsetX + (x+1)*TILE_WIDTH ;
      destRect.y = offsetY + y*TILE_HEIGHT;
      destRect.h = offsetY + (y+1)*TILE_HEIGHT ;

      SDL_FillRect(dest, &destRect, 0); //sourceRect is too small (31 pixels) maybe for a reason? This is a workaround for the yellow artifacts in the background (till we work out where the artifact is)

      // choose source surface from map
      //dest->Blt(&destRect, m_staticSurface[m_staticMap[x][y]], &sourceRect, DDBLT_WAIT, NULL);
      SDL_BlitSurface(m_staticSurface[staticTileAt(x,y)], &sourceRect, dest, &destRect);


      //dest->Draw(&destRect, m_staticSurface[m_staticMap[x][y]], &sourceRect, DDBLT_WAIT, NULL);
    }
}

void Map::renderTileTo(SDL_Surface* dest, int atX, int atY, StaticTile tileType)
{
  SDL_Rect sourceRect, destRect;

  sourceRect.x = 0;
  sourceRect.w = TILE_WIDTH - 1;
  sourceRect.y = 0;
  sourceRect.h = TILE_HEIGHT - 1;

  destRect.x = atX;
  destRect.w = atX + TILE_WIDTH;
  destRect.y = atY;
  destRect.h = atY + TILE_HEIGHT;

  //dest->Blt(&destRect, m_staticSurface[tileType], &sourceRect, DDBLT_WAIT, NULL);
  //dest->Draw(&destRect, m_staticSurface[tileType], &sourceRect, DDBLT_WAIT, NULL);
  SDL_BlitSurface(m_staticSurface[tileType], &sourceRect, dest, &destRect);


}

