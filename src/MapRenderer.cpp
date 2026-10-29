// implementation of MapRenderer.h

#include "MapRenderer.h"
#include "MickSDLAssets.h"

MapRenderer * MapRenderer::getInstance()
{
  static MapRenderer instance;

  return &instance;
}

MapRenderer::MapRenderer()
{
  for (int i = 0; i < Map::NUM_STATIC_TILES; ++i)
  {
    m_staticSurface[i] = NULL;
  }
}

MapRenderer::~MapRenderer()
{
  freeSurfaces();
}

void MapRenderer::freeSurfaces()
{
  for (int i = 0; i < Map::NUM_STATIC_TILES; ++i)
  {
    if (m_staticSurface[i])
    {
      SDL_FreeSurface(m_staticSurface[i]);
      m_staticSurface[i] = NULL;
    }
  }
}

void MapRenderer::init(SDL_Surface* sdl_primary)
{
  // Free any existing tile surfaces first, so re-running this on a resize does not
  // leak the previous ones.
  freeSurfaces();

  SDL_Surface* tempSurface[] = { std::MickSDLAssets::Load_BMP("assets/bitmaps/wall.bmp"), std::MickSDLAssets::Load_BMP("assets/bitmaps/empty.bmp"), std::MickSDLAssets::Load_BMP("assets/bitmaps/empty.bmp") };

  m_staticSurface[Map::WALL] = SDL_ConvertSurface(tempSurface[0], sdl_primary->format, 0);
  m_staticSurface[Map::EMPTY] = SDL_ConvertSurface(tempSurface[1], sdl_primary->format, 0);
  m_staticSurface[Map::BLOCK] =  SDL_ConvertSurface(tempSurface[2], sdl_primary->format, 0);

  SDL_FreeSurface(tempSurface[0]);
  SDL_FreeSurface(tempSurface[1]);
  SDL_FreeSurface(tempSurface[2]);
}

void MapRenderer::render(SDL_Surface*  dest, int offsetX, int offsetY)
{
  SDL_Rect sourceRect, destRect;

  Map* map = Map::getInstance();

  sourceRect.x = 0;
  sourceRect.w = Map::TILE_WIDTH-1;
  sourceRect.y = 0;
  sourceRect.h = Map::TILE_HEIGHT-1;
  // first render static map, then dynamic entities on top
  for (int x = 0; x < Map::MAP_WIDTH; ++x)
    for (int y = 0; y < Map::MAP_HEIGHT; ++y)
    {
      destRect.x = offsetX + (x*Map::TILE_WIDTH);
      destRect.w = offsetX + (x+1)*Map::TILE_WIDTH ;
      destRect.y = offsetY + y*Map::TILE_HEIGHT;
      destRect.h = offsetY + (y+1)*Map::TILE_HEIGHT ;

      SDL_FillRect(dest, &destRect, 0); //sourceRect is too small (31 pixels) maybe for a reason? This is a workaround for the yellow artifacts in the background (till we work out where the artifact is)

      // choose source surface from map
      SDL_BlitSurface(m_staticSurface[map->staticTileAt(x,y)], &sourceRect, dest, &destRect);
    }
}

void MapRenderer::renderTileTo(SDL_Surface* dest, int atX, int atY, Map::StaticTile tileType)
{
  SDL_Rect sourceRect, destRect;

  sourceRect.x = 0;
  sourceRect.w = Map::TILE_WIDTH - 1;
  sourceRect.y = 0;
  sourceRect.h = Map::TILE_HEIGHT - 1;

  destRect.x = atX;
  destRect.w = atX + Map::TILE_WIDTH;
  destRect.y = atY;
  destRect.h = atY + Map::TILE_HEIGHT;

  // Clean the full tile cell to black before blitting. The blit below copies only
  // a 31x31 source (TILE_WIDTH-1 x TILE_HEIGHT-1) - a deliberate workaround for a
  // garbage edge in the tile bitmaps (see render) - so it leaves a 1px seam
  // on the right/bottom of every tile. The InfoPanel reuses its m_surface across
  // frames (drawBGTiles redraws tiles each frame but only over the 31px area), so
  // without this fill those seams retain stale content - e.g. a previous frame's
  // health-bar colour - and show as coloured "stray lines" between the panel's
  // tile sprites. render already does the equivalent fill; renderTileTo did
  // not. Fill the exact cell (TILE_WIDTH x TILE_HEIGHT) so the seam is clean black.
  SDL_Rect bgRect = { atX, atY, Map::TILE_WIDTH, Map::TILE_HEIGHT };
  SDL_FillRect(dest, &bgRect, 0);

  SDL_BlitSurface(m_staticSurface[tileType], &sourceRect, dest, &destRect);
}
