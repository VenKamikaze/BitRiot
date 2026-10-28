// MapRenderer.h
//
// Draws the static map. The map itself (Map) is simulation state and knows nothing
// about bitmaps; the tile pictures, and the blits, live here on the render side.
#ifndef MAPRENDERER_H_
#define MAPRENDERER_H_

#include <SDL2/SDL.h>
#include "Map.h"

class MapRenderer
{
  public:
    static MapRenderer * getInstance();

    // Loads the tile bitmaps and converts them to the format of the primary surface.
    // Safe to call again (a window resize re-creates the primary surface).
    void init(SDL_Surface* sdl_primary);

    // Draws the whole map, then the caller draws entities on top.
    void render(SDL_Surface* destination, int offsetX = 0, int offsetY = 0);

    void renderTileTo(SDL_Surface* dest, int atX, int atY, Map::StaticTile tileType);

    ~MapRenderer();

  private:
    MapRenderer();
    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;

    void freeSurfaces();

    SDL_Surface* m_staticSurface[Map::NUM_STATIC_TILES]; // pointers to surfaces for static map
};

#endif /* MAPRENDERER_H_ */
