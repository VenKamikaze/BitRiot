// The static map (brief 03b): the layout holds no bitmaps and no surface; reads and
// writes outside it are safe (brief 01's follow-up); a resize rebuilds only when it
// has to.

#include "support/HeadlessGame.h"

#include "Map.h"

using bitriot_test::HeadlessGameTest;

namespace
{

class MapTest : public HeadlessGameTest
{
  protected:
    int countOf(Map::StaticTile tile)
    {
      Map* map = Map::getInstance();
      int n = 0;
      for (int x = 0; x < map->builtWidth(); x++)
      {
        for (int y = 0; y < map->builtHeight(); y++)
        {
          n += map->staticTileAt(x, y) == tile ? 1 : 0;
        }
      }
      return n;
    }
};

TEST_F(MapTest, InitBuildsWallsOnTheBorderAndPillarsOnEvenTilesWithNoSurface)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  ASSERT_EQ(map->builtWidth(), 9);
  ASSERT_EQ(map->builtHeight(), 7);
  for (int x = 0; x < 9; x++)
  {
    for (int y = 0; y < 7; y++)
    {
      const bool border = x == 0 || y == 0 || x == 8 || y == 6;
      const bool pillar = x % 2 == 0 && y % 2 == 0;
      EXPECT_EQ(map->staticTileAt(x, y), (border || pillar) ? Map::WALL : Map::EMPTY) << x << "," << y;
    }
  }
}

TEST_F(MapTest, AnythingOutsideTheMapReadsAsWall)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  EXPECT_EQ(map->staticTileAt(-1, 3), Map::WALL);
  EXPECT_EQ(map->staticTileAt(3, -1), Map::WALL);
  EXPECT_EQ(map->staticTileAt(9, 3), Map::WALL);
  EXPECT_EQ(map->staticTileAt(3, 7), Map::WALL);
  EXPECT_EQ(map->staticTileAt(100, 100), Map::WALL);
  EXPECT_EQ(map->staticTileAt(-100, -100), Map::WALL);
  // In range, nothing changed.
  EXPECT_EQ(map->staticTileAt(1, 1), Map::EMPTY);
}

TEST_F(MapTest, AWriteOutsideTheMapChangesNothingInsideIt)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  const int walls = countOf(Map::WALL);
  const int empties = countOf(Map::EMPTY);

  // Without a bounds check these land on other tiles: (2, -1) is the last tile of
  // the column before, (1, 7) is the first tile of the column after, and both are
  // border walls.
  map->setStaticTileAt(2, -1, Map::BLOCK);
  map->setStaticTileAt(1, 7, Map::BLOCK);
  map->setStaticTileAt(3, 100, Map::EMPTY);
  map->setBlockAt(2, -1, true);
  map->setBlockAt(1, 7, true);

  EXPECT_EQ(map->staticTileAt(1, 6), Map::WALL);
  EXPECT_EQ(map->staticTileAt(2, 0), Map::WALL);
  EXPECT_EQ(countOf(Map::WALL), walls);
  EXPECT_EQ(countOf(Map::EMPTY), empties);
  EXPECT_EQ(countOf(Map::BLOCK), 0);
}

TEST_F(MapTest, TheRowStrideAndBoundsFollowTheBuiltMapNotAStaticChangedSince)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  map->setBlockAt(3, 3, true);

  // Someone changes the statics without rebuilding (the old resize bug's shape):
  // the built map keeps being read and written as 9x7.
  Map::MAP_WIDTH = 21;
  Map::MAP_HEIGHT = 15;
  EXPECT_EQ(map->staticTileAt(3, 3), Map::BLOCK);
  EXPECT_EQ(map->staticTileAt(10, 3), Map::WALL) << "outside the 9x7 that is actually built";
  map->setStaticTileAt(10, 3, Map::BLOCK);
  EXPECT_EQ(map->staticTileAt(3, 3), Map::BLOCK);
  EXPECT_EQ(countOf(Map::BLOCK), 1);
  Map::MAP_WIDTH = 9;
  Map::MAP_HEIGHT = 7;
}

TEST_F(MapTest, ResizeRebuildsTheLayoutOnlyWhenTheSizeChanges)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  map->setBlockAt(3, 3, true);
  const unsigned int generation = map->generation();

  map->resize(9, 7);
  EXPECT_EQ(map->generation(), generation) << "same size: no rebuild";
  EXPECT_EQ(map->staticTileAt(3, 3), Map::BLOCK) << "same size: blocks stay (clearBlocks() is the reset)";

  map->resize(11, 7);
  EXPECT_GT(map->generation(), generation);
  EXPECT_EQ(Map::MAP_WIDTH, 11);
  EXPECT_EQ(Map::MAP_HEIGHT, 7);
  EXPECT_EQ(map->builtWidth(), 11);
  EXPECT_EQ(countOf(Map::BLOCK), 0) << "a rebuilt layout has no blocks";

  const unsigned int rebuilt = map->generation();
  map->resize(11, 9);
  EXPECT_GT(map->generation(), rebuilt) << "a change of height alone rebuilds too";
  EXPECT_EQ(map->builtHeight(), 9);
}

TEST_F(MapTest, ClearBlocksRemovesBlocksAndNothingElse)
{
  setMapSize(9, 7);
  Map* map = Map::getInstance();
  const int walls = countOf(Map::WALL);
  map->setBlockAt(1, 1, true);
  map->setBlockAt(3, 5, true);
  ASSERT_EQ(countOf(Map::BLOCK), 2);
  map->clearBlocks();
  EXPECT_EQ(countOf(Map::BLOCK), 0);
  EXPECT_EQ(countOf(Map::WALL), walls);
}

} // namespace
