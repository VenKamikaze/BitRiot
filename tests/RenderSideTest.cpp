// The render side of the split (brief 03b): the animation frame counts the
// simulation uses agree with the bitmaps the renderer loads, and drawing the world
// reads the simulation without changing it.
//
// Core executable only: it needs SDL surfaces and the game's real bitmaps.

#include "support/HeadlessGame.h"

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "EntityAnim.h"
#include "EntityRendererFactory.h"
#include "GameSettings.h"
#include "Map.h"
#include "MapRenderer.h"
#include "SimRandom.h"
#include "WorldRenderer.h"

using bitriot_test::HeadlessGameTest;

namespace
{

SDL_Surface* g_primary = nullptr;

// FNV-1a over a surface's pixels, row by row (the pitch padding is skipped).
uint64_t pixelDigest(SDL_Surface* surface)
{
  uint64_t h = 1469598103934665603ULL;
  const uint8_t* rows = static_cast<const uint8_t*>(surface->pixels);
  for (int y = 0; y < surface->h; y++)
  {
    for (int i = 0; i < surface->w * surface->format->BytesPerPixel; i++)
    {
      h ^= rows[y * surface->pitch + i];
      h *= 1099511628211ULL;
    }
  }
  return h;
}

class RenderSideTest : public HeadlessGameTest
{
  protected:
    static void SetUpTestSuite()
    {
      ASSERT_EQ(SDL_Init(0), 0) << SDL_GetError();
      // Only the pixel format matters: the entity bitmaps are converted to it.
      g_primary = SDL_CreateRGBSurface(0, 640, 480, 32, 0, 0, 0, 0);
      ASSERT_NE(g_primary, nullptr) << SDL_GetError();
      EntityRendererFactory::getInstance()->initSurfaces(g_primary);
      MapRenderer::getInstance()->init(g_primary);
    }
};

TEST_F(RenderSideTest, TheFrameCountTableAgreesWithEveryLoadedBitmap)
{
  EntityRendererFactory* factory = EntityRendererFactory::getInstance();
  for (int t = 0; t < NUM_ENTITY_TYPES; t++)
  {
    const EntityType type = static_cast<EntityType>(t);
    EntityRenderer* renderer = factory->getEntityRenderer(type);
    ASSERT_NE(renderer, nullptr) << "type " << t;
    for (int team = 0; team < NUM_TEAMS; team++)
    {
      EXPECT_EQ(renderer->getNumFrames(team), EntityAnim::frameCount(type))
          << "entity type " << t << ", team " << team
          << ": the sprite sheet's width / 32 is not the simulation's table entry (src/EntityAnim.cpp)";
    }
  }
}

TEST_F(RenderSideTest, EveryPlayerSheetHasTheCharacterFrameCount)
{
  EntityRenderer* renderer = EntityRendererFactory::getInstance()->getEntityRenderer(PLAYER_CHARACTER);
  ASSERT_NE(renderer, nullptr);
  for (int player = 1; player <= 6; player++)
  {
    for (const char* gender : { "male", "female" })
    {
      const std::string file = "assets/bitmaps/player" + std::to_string(player) + gender + ".bmp";
      ASSERT_TRUE(renderer->replaceTeamSurfaceWithImage(g_primary, file.c_str(), player)) << file;
      EXPECT_EQ(renderer->getNumFrames(player), EntityAnim::frameCount(PLAYER_CHARACTER)) << file;
    }
  }
}

TEST_F(RenderSideTest, EveryFrameTheSimulationCanShowIsDrawnAndTheNextOneIsNot)
{
  SDL_Surface* canvas = SDL_CreateRGBSurface(0, 128, 128, 32, 0, 0, 0, 0);
  ASSERT_NE(canvas, nullptr);
  EntityRendererFactory* factory = EntityRendererFactory::getInstance();
  for (int t = 0; t < NUM_ENTITY_TYPES; t++)
  {
    const EntityType type = static_cast<EntityType>(t);
    EntityRenderer* renderer = factory->getEntityRenderer(type);
    const int frames = EntityAnim::frameCount(type);
    for (int frame = 0; frame < frames; frame++)
    {
      EXPECT_TRUE(renderer->render(canvas, 0, 0, EntityAnim::IDLE, frame, 1)) << "type " << t << " frame " << frame;
    }
    EXPECT_FALSE(renderer->render(canvas, 0, 0, EntityAnim::IDLE, frames, 1))
        << "type " << t << " has a frame beyond the table's count";
  }
  SDL_FreeSurface(canvas);
}

TEST_F(RenderSideTest, DrawingTheWorldTwiceGivesTheSamePictureAndLeavesTheSimulationAlone)
{
  configurePlayers(4);
  setBlockPercentage(55);
  startRound();
  for (int i = 0; i < 150; i++)
  {
    sim().step();
  }

  SDL_Surface* canvas = SDL_CreateRGBSurface(0, Map::MAP_WIDTH * Map::TILE_WIDTH, Map::MAP_HEIGHT * Map::TILE_HEIGHT, 32, 0, 0, 0, 0);
  ASSERT_NE(canvas, nullptr);

  const uint64_t before = sim().stateHash();
  WorldRenderer::renderWorld(em(), canvas);
  const uint64_t first = pixelDigest(canvas);
  WorldRenderer::renderWorld(em(), canvas);
  const uint64_t second = pixelDigest(canvas);

  EXPECT_EQ(first, second) << "rendering with no step in between must give the same picture";
  EXPECT_EQ(sim().stateHash(), before) << "drawing changed the simulation";

  // And the picture follows the simulation.
  for (int i = 0; i < 40; i++)
  {
    sim().step();
  }
  WorldRenderer::renderWorld(em(), canvas);
  EXPECT_NE(pixelDigest(canvas), first);
  SDL_FreeSurface(canvas);
}

// The world, pixel for pixel, as the renderer drew it before the split (brief 03b).
//
// These digests were taken from the code before 03b (EntityManager::renderEntities,
// Map::render and GameEntity::render on a surface the Map had loaded its tiles
// against) and the same rounds drawn through WorldRenderer must give them again:
// the rendering is meant to be unchanged. Each round is pinned (config and seed),
// the player sheets are the shipped male/female bitmaps as the game picks them,
// and the cosmetic random stream is seeded, so the damage-effect frame is fixed.
// Only blits are involved (no text; the HUD is not part of this).
//
// If you change a bitmap, a sprite's position maths or the order things are drawn
// in, these move: check the picture, then regenerate with
//   BITRIOT_PIXEL_DUMP=1 ./bitriot_tests --gtest_filter='RenderSideTest.TheWorldIs*'
struct PixelRound
{
  const char* name;
  int players, width, height, blockPercent;
  uint64_t seed;
  struct Frame { int tick; uint64_t digest; } frames[5];
};

const PixelRound PIXEL_ROUNDS[] =
{
  {"four bots, default map", 4, 17, 13, 55, 4242,
   {{0, 0x36790783ccb58789ULL}, {100, 0x15c6efe03515c5cfULL}, {400, 0xe7aaa0b224760f60ULL},
    {1000, 0xd0a15311f41b4053ULL}, {2500, 0x56634c14e72f047fULL}}},
  {"two bots, 13x11", 2, 13, 11, 55, 77,
   {{0, 0xe6e9029e8f5a10fbULL}, {100, 0x946f2bbcbe1817c7ULL}, {400, 0x3856fd83eec57c9aULL},
    {1000, 0x79e16bffcd939eecULL}, {2500, 0xc18562a390a50595ULL}}},
  {"six bots, big map", 6, 25, 19, 55, 9001,
   {{0, 0x490ccd0f01f0d1c6ULL}, {100, 0x9c4f5a1398ca61f1ULL}, {400, 0x8e9711c2fba0ee70ULL},
    {1000, 0x20841f3a072c95f2ULL}, {2500, 0x6f377b9e6eedab3dULL}}},
};

TEST_F(RenderSideTest, TheWorldIsDrawnPixelForPixelAsItWasBeforeTheSplit)
{
  const bool dump = std::getenv("BITRIOT_PIXEL_DUMP") != nullptr;
  for (const PixelRound& round : PIXEL_ROUNDS)
  {
    SimRandom::cosmetic().seed(1);
    configurePlayers(round.players);
    setMapSize(round.width, round.height);
    setBlockPercentage(round.blockPercent);
    setRoundSeed(round.seed);
    startRound();

    std::vector<bool>* genders = std::GameSettings::getInstance()->getPlayerGenders();
    for (int i = 1; i <= round.players; i++)
    {
      const std::string file = "assets/bitmaps/player" + std::to_string(i) + (genders->at(i - 1) ? "male" : "female") + ".bmp";
      ASSERT_TRUE(EntityRendererFactory::getInstance()->getEntityRenderer(PLAYER_CHARACTER)
                      ->replaceTeamSurfaceWithImage(g_primary, file.c_str(), i));
    }

    SDL_Surface* canvas = SDL_CreateRGBSurface(0, round.width * Map::TILE_WIDTH, round.height * Map::TILE_HEIGHT, 32, 0, 0, 0, 0);
    ASSERT_NE(canvas, nullptr);
    int at = 0;
    for (const PixelRound::Frame& frame : round.frames)
    {
      while (at < frame.tick)
      {
        sim().step();
        at++;
      }
      WorldRenderer::renderWorld(em(), canvas);
      const uint64_t got = pixelDigest(canvas);
      if (dump)
      {
        std::printf("PIXELS %s tick=%d digest=0x%016llxULL\n", round.name, frame.tick, (unsigned long long) got);
      }
      EXPECT_EQ(got, frame.digest) << round.name << " at tick " << frame.tick;
    }
    SDL_FreeSurface(canvas);
  }
}

} // namespace
