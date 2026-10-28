// Map.h
#ifndef _MAPCLASS
#define _MAPCLASS
// This class represents the static game map of wall blocks and empty tiles. It is
// simulation state (and hashed), so it holds no bitmaps and needs no SDL: the tile
// pictures live in MapRenderer.

// Yet another singleton class (I'm trying not to abuse it, really!)

enum DIRECTION { UP, DOWN, LEFT, RIGHT };

class Map
{
  public:
    static Map * getInstance();

    // Rebuilds the layout (outer wall, pillars, nothing else) at MAP_WIDTH x
    // MAP_HEIGHT, dropping every block. Needs no surface.
    void init();

    // Sets MAP_WIDTH / MAP_HEIGHT and rebuilds the layout if it is not already that
    // size. This is what Simulation::start() calls, so a round brings its own map.
    // (Blocks are NOT cleared when the size is unchanged; clearBlocks() does that.)
    void resize(int width, int height);

    ~Map();

    static int GET_MAP_WIDTH(); //not really needed right now
    static int GET_MAP_HEIGHT(); //not really needed right now

    static int MAP_WIDTH; // usually 17
    static int MAP_HEIGHT; // usually 13

    const static int TILE_WIDTH = 32;
    const static int TILE_HEIGHT = 32;

    const static int NUM_STATIC_TILES = 3;
    enum StaticTile { WALL, EMPTY, BLOCK };

    // Bumped by every init(). A thing that writes to the map later (BlockEntity's
    // destructor) remembers the generation it saw, and skips the write if the map
    // has since been rebuilt - the new map has different dimensions (so a different
    // stride) and starts with no blocks anyway.
    unsigned int generation() const { return m_generation; }

    // The size of the layout that is actually built (what init() last used), which
    // is what bounds checks and the row stride go by - MAP_WIDTH / MAP_HEIGHT may
    // have been changed since, until the next init().
    int builtWidth() const { return m_width; }
    int builtHeight() const { return m_height; }

    void setBlockAt(int x, int y, bool block);
    void clearBlocks();

    // Anything outside the built map reads as WALL (nothing can stand in or walk
    // through it), and a write outside it is ignored. Neither touches memory
    // outside the map, so a stray coordinate can no longer alias another tile or
    // run off the end of the array.
    StaticTile staticTileAt(int x, int y);
    void setStaticTileAt(int x, int y, StaticTile staticTile);

  private:
    Map();
    // static map data
    StaticTile *m_staticMap; // 2D array of wall tiles, m_width x m_height, x-major
    int m_width = 0;
    int m_height = 0;
    unsigned int m_generation = 0; // see generation()

};


#endif
