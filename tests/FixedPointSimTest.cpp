// Characterisation tests for the sim's movement, rocket and path-search maths.
//
// Written against the FLOAT simulation first (brief 02b), and kept passing
// across the conversion to fixed-point, so that "play feel is unchanged" is a
// measurement and not a hope. They record behaviour that can be compared across
// number representations: the pixel a sprite is drawn at on every tick, the tick
// a rocket explodes on, and the route the A* search picks. Where the fixed-point
// version legitimately differs, the test says so next to the expected value.
//
// The few places that touch the offset type itself live in the "representation"
// block below, so the rest of the file does not care what an offset is.

#include "support/HeadlessGame.h"

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

#include "AIInstruction.h"
#include "DataReader.h"
#include "DroneEntity.h"
#include "EggThiefEntity.h"
#include "EntityMessageQueue.h"
#include "GameSettings.h"
#include "Map.h"
#include "PlayerCharacterEntity.h"
#include "RobotEntity.h"
#include "RocketFiredEntity.h"
#include "SimRandom.h"
#include "WallEaterEntity.h"

namespace bitriot_test
{
namespace
{

// ---------------------------------------------------------------------------
// Representation: the only code that knows what an offset is
// ---------------------------------------------------------------------------

std::unique_ptr<RocketFiredEntity> makeRocket(int id, int owner, int atX, int atY,
    unsigned int flags, int toX, int toY)
{
  return std::unique_ptr<RocketFiredEntity>(
           new RocketFiredEntity(id, owner, atX, atY, flags, toX, toY));
}

void postRocket(int owner, int atX, int atY, unsigned int flags, int toX, int toY)
{
  EntityMessageQueue::getInstance()->postCreateMessage(ROCKET_FIRED, owner, atX, atY, flags,
      0, 0, toX, toY);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

// FNV-1a over 32-bit values: a compact fingerprint of a long trace.
class Digest
{
  public:
    void add(int32_t v)
    {
      uint32_t u = (uint32_t)v;
      for (int i = 0; i < 4; ++i)
      {
        m_h ^= (u >> (8 * i)) & 0xFFu;
        m_h *= 1099511628211ull;
      }
    }
    uint64_t value() const
    {
      return m_h;
    }
  private:
    uint64_t m_h = 14695981039346656037ull;
};

// Prints a fingerprint when BITRIOT_FIXED_DUMP is set, for regenerating the
// expected values below after a deliberate change.
void dump(const char * what, uint64_t value)
{
  if (std::getenv("BITRIOT_FIXED_DUMP"))
  {
    std::printf("DUMP %s 0x%016llxull\n", what, (unsigned long long)value);
  }
}

// A tiny deterministic generator for building layouts and scripts. Not the sim
// RNG: these must not move when the sim's draws do.
class Lcg
{
  public:
    explicit Lcg(uint32_t seed) : m_s(seed) {}
    uint32_t next()
    {
      m_s = m_s * 1664525u + 1013904223u;
      return m_s >> 8;
    }
    int below(int n)
    {
      return (int)(next() % (uint32_t)n);
    }
  private:
    uint32_t m_s;
};

class FixedPointSimTest : public HeadlessGameTest
{
  protected:
    int countOfType(EntityType type)
    {
      int count = 0;
      DynamicMap * map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        if (it->second->getType() == type)
        {
          count++;
        }
      }
      return count;
    }

    void TearDown() override
    {
      EntityMessageQueue::getInstance()->clear();
      HeadlessGameTest::TearDown();
    }

    // Posts every pending message away, returning them to the caller as plain
    // data.
    struct Msg
    {
      int type;
      int sender;
      int typeToCreate;
      int toX, toY;
    };
    std::vector<Msg> drainMessages()
    {
      std::vector<Msg> out;
      EntityMessageQueue * q = EntityMessageQueue::getInstance();
      while (EntityMessage * m = q->getMessageAtFront())
      {
        Msg x = { m->getType(), m->getSender(), m->getTypeToCreate(), m->getToX(), m->getToY() };
        out.push_back(x);
        q->deleteMessageAtFront();
      }
      return out;
    }
};

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

// An entity with its velocity set from its own update(), put back on the middle
// of tile (1,1). Only the real constructors and update() compute velocity, so
// this exercises them; owner 0 does not decay, so health stays what it was set to.
template <class T>
class Mover : public T
{
  public:
    template <class... Args>
    Mover(int health, Args... args) : T(args...)
    {
      this->m_health = health;
      this->update();
      this->setTileX(1);
      this->setTileY(1);
      this->setOffsetX(0);
      this->setOffsetY(0);
    }
};

struct TraceResult
{
  uint64_t digest;
  int crossingsX; // tile changes in x over the script
  int finalPixelX;
  int finalPixelY;
};

// Walks an entity through a scripted run of directions, recording where it is
// drawn on every tick. The script is long and varied enough to bring in
// cornering (move toward the centre line first), stopping against walls and
// blocks, and every tile wrap.
TraceResult walkScript(GameEntity & e, uint32_t scriptSeed, int ticks,
                       std::vector<int> * pixels = nullptr)
{
  Lcg lcg(scriptSeed);
  Digest d;
  int crossings = 0;
  int lastTileX = e.getTileX();
  DIRECTION dir = RIGHT;
  int runLeft = 0;
  for (int t = 0; t < ticks; ++t)
  {
    if (runLeft == 0)
    {
      dir = (DIRECTION)lcg.below(4);
      runLeft = 1 + lcg.below(12);
    }
    --runLeft;
    e.moveInDirection(dir);
    EntityMessageQueue::getInstance()->clear();
    if (e.getTileX() != lastTileX)
    {
      ++crossings;
      lastTileX = e.getTileX();
    }
    d.add(e.pixelX());
    d.add(e.pixelY());
    if (pixels)
    {
      pixels->push_back(e.pixelX());
      pixels->push_back(e.pixelY());
    }
  }
  TraceResult r;
  r.digest = d.value();
  r.crossingsX = crossings;
  r.finalPixelX = e.pixelX();
  r.finalPixelY = e.pixelY();
  return r;
}

// A board with pillars, and a few blocks to walk into.
void prepareWalkBoard()
{
  Map::getInstance()->clearBlocks();
  Map * m = Map::getInstance();
  m->setBlockAt(3, 1, true);
  m->setBlockAt(5, 3, true);
  m->setBlockAt(1, 5, true);
  m->setBlockAt(7, 5, true);
  m->setBlockAt(4, 7, true);
}

enum Kind { PLAYER, ROBOT, DRONE, WALL_EATER_K, EGG_THIEF_K };

TraceResult traceKind(Kind k, int health, uint32_t seed, std::vector<int> * pixels = nullptr)
{
  switch (k)
  {
    case PLAYER:
    {
      Mover<PlayerCharacterEntity> e(health, 9001, 1, 1, 1, 0u);
      return walkScript(e, seed, 600, pixels);
    }
    case ROBOT:
    {
      Mover<RobotEntity> e(health, 9001, 0, 1, 1, 0u);
      return walkScript(e, seed, 600, pixels);
    }
    case DRONE:
    {
      Mover<DroneEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600, pixels);
    }
    case WALL_EATER_K:
    {
      Mover<WallEaterEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600, pixels);
    }
    case EGG_THIEF_K:
    default:
    {
      Mover<EggThiefEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600, pixels);
    }
  }
}

struct MoverCase
{
  const char * name;
  Kind kind;
  int health;
  uint32_t seed;
  // the fixed-point result: a digest of every tick's drawn position, the tile
  // changes in x, and the final drawn position. Eight of the twelve are identical
  // to what the float maths gave; the four that are not (player h100 and h10,
  // drone h9, wall eater h7) were e.g. 0x43735d8ba130bc33 for player h100 in float
  // and differ by one pixel on the ticks counted in the last column.
  uint64_t digest;
  int crossingsX;
  int pixelX;
  int pixelY;
  // ticks (of 600) on which the fixed-point result is drawn a pixel away from
  // the original float maths (see ScriptedWalksStayWithinOnePixel...)
  int pixelsDifferingFromFloat;
};

const MoverCase MOVER_CASES[] =
{
  { "player h200",       PLAYER,       200, 11, 0xe22fee89094c5921ull, 12, 32, 107, 0 },
  { "player h100",       PLAYER,       100, 12, 0xe9fefcd6b269cd64ull, 23, 187, 224, 23 },
  { "player h55",        PLAYER,        55, 13, 0x17b2cf5700f02250ull, 14, 89, 96, 0 },
  { "player h10",        PLAYER,        10, 14, 0x669a10efd59834deull, 6, 37, 32, 2 },
  { "robot h38",         ROBOT,         38, 15, 0x93f01f6fc3a42ae3ull, 19, 49, 96, 0 },
  { "robot h12",         ROBOT,         12, 16, 0x26c2733c38370891ull, 4, 34, 32, 0 },
  { "drone h35",         DRONE,         35, 17, 0x72359ff1f04e1c04ull, 18, 47, 96, 0 },
  { "drone h9",          DRONE,          9, 18, 0xe179c002fcc746a4ull, 0, 32, 96, 1 },
  { "wall eater h25",    WALL_EATER_K,  25, 19, 0x4df2343d864e0070ull, 10, 160, 143, 0 },
  { "wall eater h7",     WALL_EATER_K,   7, 20, 0x3fb416e279d0f63eull, 0, 32, 71, 11 },
  { "egg thief h20",     EGG_THIEF_K,   20, 21, 0xd9a2233cbe2358f9ull, 12, 32, 128, 0 },
  { "egg thief h6",      EGG_THIEF_K,    6, 22, 0xedccf1386c3343bcull, 0, 33, 32, 0 },
};

TEST_F(FixedPointSimTest, ScriptedWalksAreDrawnOnTheExpectedPixels)
{
  setMapSize(9, 9);
  prepareWalkBoard();
  SimRandom::sim().seed(4242);

  for (size_t i = 0; i < sizeof(MOVER_CASES) / sizeof(MOVER_CASES[0]); ++i)
  {
    const MoverCase & c = MOVER_CASES[i];
    const TraceResult r = traceKind(c.kind, c.health, c.seed);
    dump(c.name, r.digest);
    if (std::getenv("BITRIOT_FIXED_DUMP"))
    {
      std::printf("DUMPX %s crossings=%d px=%d py=%d\n", c.name, r.crossingsX,
                  r.finalPixelX, r.finalPixelY);
    }
    else
    {
      EXPECT_EQ(r.digest, c.digest) << c.name;
      EXPECT_EQ(r.crossingsX, c.crossingsX) << c.name;
      EXPECT_EQ(r.finalPixelX, c.pixelX) << c.name;
      EXPECT_EQ(r.finalPixelY, c.pixelY) << c.name;
    }
  }
}

// The original float implementation of GameEntity::moveInDirection, kept here
// as a reference to measure the fixed-point code against. It follows the
// original branch for branch (every direction was the same code with the axes
// swapped), using 32-bit floats. This is test code: floats are fine here.
struct FloatMover
{
  int tileX, tileY;
  float offX, offY;
  float velocity;
  bool walkThroughBlocks;

  void move(DIRECTION d)
  {
    Map * map = Map::getInstance();
    const bool horizontal = (d == LEFT || d == RIGHT);
    const bool towardNegative = (d == UP || d == LEFT);
    const int nextX = tileX + (d == RIGHT) - (d == LEFT);
    const int nextY = tileY + (d == DOWN) - (d == UP);
    float & main = horizontal ? offX : offY;     // offset along the direction of travel
    float & across = horizontal ? offY : offX;   // offset across it, centred first

    float thisMove = velocity;
    const Map::StaticTile ahead = map->staticTileAt(nextX, nextY);
    if (ahead == Map::WALL || (ahead == Map::BLOCK && !walkThroughBlocks))
    {
      if (towardNegative)
      {
        if (main - velocity < 0)
        {
          thisMove = main;
        }
      }
      else
      {
        if (main + velocity > 0)
        {
          thisMove = -main;
        }
      }
    }

    // move toward the centre line before travelling
    if (across < 0.0f)
    {
      if (across + thisMove <= 0.0f)
      {
        across += thisMove;
      }
      else
      {
        const float remaining = across + thisMove;
        across = 0.0f;
        main = towardNegative ? main - remaining : main + remaining;
      }
    }
    else if (across > 0.0f)
    {
      if (across - thisMove >= 0.0f)
      {
        across -= thisMove;
      }
      else
      {
        const float remaining = thisMove - across;
        across = 0.0f;
        main = towardNegative ? main - remaining : main + remaining;
      }
    }
    else
    {
      main = towardNegative ? main - thisMove : main + thisMove;
    }

    if (offX < -1.0f) { offX += 2.0f; tileX--; }
    else if (offX > 1.0f) { offX -= 2.0f; tileX++; }
    if (offY < -1.0f) { offY += 2.0f; tileY--; }
    else if (offY > 1.0f) { offY -= 2.0f; tileY++; }
  }
};

// (health + extra) / modifier in 32-bit float, as the original computed velocity.
float floatVelocity(int health, int extra, const char * key, const char * file)
{
  return ((float)health + extra) / DataReader::getInstance()->getIntFromFile(key, file);
}

TEST_F(FixedPointSimTest, ScriptedWalksStayWithinOnePixelOfTheOriginalFloatMaths)
{
  setMapSize(9, 9);
  prepareWalkBoard();
  SimRandom::sim().seed(4242);

  int totalDiffering = 0;
  for (size_t i = 0; i < sizeof(MOVER_CASES) / sizeof(MOVER_CASES[0]); ++i)
  {
    const MoverCase & c = MOVER_CASES[i];

    FloatMover ref = { 1, 1, 0.0f, 0.0f, 0.0f, false };
    switch (c.kind)
    {
      case PLAYER:
        ref.velocity = floatVelocity(c.health, 10, "PLAYER_VELOCITY_MODIFIER", "data/player.txt");
        break;
      case ROBOT:
        ref.velocity = floatVelocity(c.health, 2, "ROBOT_VELOCITY_MODIFIER", "data/entity.txt");
        break;
      case DRONE:
        ref.velocity = floatVelocity(c.health, 2, "DRONE_VELOCITY_MODIFIER", "data/entity.txt");
        break;
      case WALL_EATER_K:
        ref.velocity = floatVelocity(c.health, 2, "WALL_EATER_VELOCITY_MODIFIER", "data/entity.txt");
        ref.walkThroughBlocks = true;
        break;
      default:
        ref.velocity = floatVelocity(c.health, 2, "BASE_VELOCITY_MODIFIER", "data/entity.txt");
        break;
    }

    std::vector<int> actual;
    traceKind(c.kind, c.health, c.seed, &actual);

    // the same script, on the float reference
    Lcg lcg(c.seed);
    DIRECTION dir = RIGHT;
    int runLeft = 0;
    int differing = 0, worst = 0;
    for (int t = 0; t < 600; ++t)
    {
      if (runLeft == 0)
      {
        dir = (DIRECTION)lcg.below(4);
        runLeft = 1 + lcg.below(12);
      }
      --runLeft;
      ref.move(dir);
      const int px = ref.tileX * Map::TILE_WIDTH + (int)(ref.offX * (Map::TILE_WIDTH / 2));
      const int py = ref.tileY * Map::TILE_WIDTH + (int)(ref.offY * (Map::TILE_WIDTH / 2));
      const int dx = std::abs(px - actual[2 * t]);
      const int dy = std::abs(py - actual[2 * t + 1]);
      if (dx || dy)
      {
        ++differing;
      }
      worst = std::max(worst, std::max(dx, dy));
    }
    totalDiffering += differing;
    EXPECT_LE(worst, 1) << c.name << ": drawn more than a pixel from the original float maths";
    EXPECT_EQ(differing, c.pixelsDifferingFromFloat) << c.name;
  }
  // 37 of 7200 ticks (0.5%) are drawn one pixel off. Nothing is drawn further.
  EXPECT_EQ(totalDiffering, 37);
}

// ---------------------------------------------------------------------------
// Entities that steer themselves (SimpleBrain: nearTileCenter, turning)
// ---------------------------------------------------------------------------

template <class T, class... Extra>
uint64_t brainWalk(int owner, uint32_t rngSeed, int ticks, int * tilesVisited, Extra... extra)
{
  SimRandom::sim().seed(rngSeed);
  T e(9002, owner, 1, 1, extra...);
  Digest d;
  int crossings = 0;
  int lastX = e.getTileX(), lastY = e.getTileY();
  for (int t = 0; t < ticks; ++t)
  {
    e.update();
    EntityMessageQueue::getInstance()->clear();
    if (e.getTileX() != lastX || e.getTileY() != lastY)
    {
      ++crossings;
      lastX = e.getTileX();
      lastY = e.getTileY();
    }
    d.add(e.pixelX());
    d.add(e.pixelY());
  }
  *tilesVisited = crossings;
  return d.value();
}

// A self-steering entity turns when it is within one step of the middle of a
// tile (|offset| <= velocity). For a velocity like a full-health robot's 4/19 the
// offset lands on exactly +-velocity every 19 ticks, and whether that counts as
// "within one step" was decided by float rounding noise; the fixed rounding
// decides some of those ties the other way. The entity then turns one step
// earlier or later and, being random, goes on to a different walk from there. Which
// velocities that happens to is arbitrary: here the robot, drone and egg thief walks
// differ from the float ones (different digests) and the wall eater walks match
// exactly. The pace is the same: tile changes over 1500 ticks were
// float 161/130/160/125/127/90/120/77 and fixed are as below.
struct BrainCase
{
  const char * name;
  Kind kind;
  int owner;
  uint32_t seed;
  uint64_t digest;
  int tileChanges;
};

const BrainCase BRAIN_CASES[] =
{
  { "robot owner0",      ROBOT,        0, 101, 0xff95e4d9640ef62aull, 161 },
  { "robot owner1",      ROBOT,        1, 102, 0x76908f828d86cbedull, 131 },
  { "drone owner0",      DRONE,        0, 103, 0xf24c65ba5dab2ff1ull, 160 },
  { "drone owner2",      DRONE,        2, 104, 0xb823152a7668dbb4ull, 125 },
  { "wall eater owner0", WALL_EATER_K, 0, 105, 0xcdc85dafb27268d3ull, 127 },
  { "wall eater owner1", WALL_EATER_K, 1, 106, 0x5b6641f74261d630ull, 90 },
  { "egg thief owner0",  EGG_THIEF_K,  0, 107, 0x0ac8740d2c26e1b6ull, 120 },
  { "egg thief owner3",  EGG_THIEF_K,  3, 108, 0x25f587d0040615d4ull, 77 },
};

TEST_F(FixedPointSimTest, SelfSteeringEntitiesWalkTheSameRoutesAsTheOriginalMaths)
{
  setMapSize(9, 9);
  prepareWalkBoard();

  for (size_t i = 0; i < sizeof(BRAIN_CASES) / sizeof(BRAIN_CASES[0]); ++i)
  {
    const BrainCase & c = BRAIN_CASES[i];
    int changes = 0;
    uint64_t digest = 0;
    switch (c.kind)
    {
      case ROBOT:
        digest = brainWalk<RobotEntity>(c.owner, c.seed, 1500, &changes, 0u);
        break;
      case DRONE:
        digest = brainWalk<DroneEntity>(c.owner, c.seed, 1500, &changes);
        break;
      case WALL_EATER_K:
        digest = brainWalk<WallEaterEntity>(c.owner, c.seed, 1500, &changes);
        break;
      default:
        digest = brainWalk<EggThiefEntity>(c.owner, c.seed, 1500, &changes);
        break;
    }
    dump(c.name, digest);
    if (std::getenv("BITRIOT_FIXED_DUMP"))
    {
      std::printf("DUMPX %s tileChanges=%d\n", c.name, changes);
    }
    else
    {
      EXPECT_EQ(digest, c.digest) << c.name;
      EXPECT_EQ(changes, c.tileChanges) << c.name;
    }
  }
}

// ---------------------------------------------------------------------------
// Rockets
// ---------------------------------------------------------------------------

struct RocketResult
{
  int tick;      // update() on which the rocket blew up (0: never within the budget)
  int tileX, tileY; // the rocket's tile at that moment
};

RocketResult flyRocket(FixedPointSimTest * t, int atX, int atY, unsigned int flags, int toX, int toY)
{
  (void)t;
  const int id = 7000;
  std::unique_ptr<RocketFiredEntity> r = makeRocket(id, 1, atX, atY, flags, toX, toY);
  RocketResult out = { 0, 0, 0 };
  for (int tick = 1; tick <= 300 && out.tick == 0; ++tick)
  {
    r->update();
    EntityMessageQueue * q = EntityMessageQueue::getInstance();
    while (EntityMessage * m = q->getMessageAtFront())
    {
      if (m->getType() == EntityMessage::DESTROY_ENTITY && m->getSender() == id && out.tick == 0)
      {
        out.tick = tick;
        out.tileX = r->getTileX();
        out.tileY = r->getTileY();
      }
      q->deleteMessageAtFront();
    }
  }
  return out;
}

TEST_F(FixedPointSimTest, RocketsExplodeOnTheSameTickAsTheOriginalMaths)
{
  setMapSize(17, 13);
  Map::getInstance()->clearBlocks();

  Digest d;
  int flights = 0;
  // along the open top row and left column, to every target tile, plus one
  // beyond the wall that must be stopped by it
  for (int to = 1; to <= 15; ++to)
  {
    RocketResult r = flyRocket(this, 1, 1, GameEntity::RIGHT_FLAG, to, 1);
    d.add(r.tick); d.add(r.tileX); d.add(r.tileY);
    RocketResult l = flyRocket(this, 15, 1, GameEntity::LEFT_FLAG, to, 1);
    d.add(l.tick); d.add(l.tileX); d.add(l.tileY);
    flights += 2;
  }
  for (int to = 1; to <= 11; ++to)
  {
    RocketResult dn = flyRocket(this, 1, 1, GameEntity::DOWN_FLAG, 1, to);
    d.add(dn.tick); d.add(dn.tileX); d.add(dn.tileY);
    RocketResult up = flyRocket(this, 1, 11, GameEntity::UP_FLAG, 1, to);
    d.add(up.tick); d.add(up.tileX); d.add(up.tileY);
    flights += 2;
  }
  RocketResult beyond = flyRocket(this, 1, 1, GameEntity::RIGHT_FLAG, 40, 1);
  d.add(beyond.tick); d.add(beyond.tileX); d.add(beyond.tileY);
  RocketResult beyondUp = flyRocket(this, 1, 11, GameEntity::UP_FLAG, 1, -3);
  d.add(beyondUp.tick); d.add(beyondUp.tileX); d.add(beyondUp.tileY);

  dump("rockets", d.value());
  EXPECT_EQ(flights, 52);
  EXPECT_EQ(d.value(), 0x3aa149109fb5251full);

  // Fired from (1,1) along the open row at a target k tiles along, the rocket
  // blows up in the target tile. With the old float velocity it did so on ticks
  // 1, 4, 9, 14, 19, ...: in every tile after the first it was already "within
  // one step of the middle" 0.4 short of it, because float rounding put the
  // offset at -0.39999995 against a velocity of 0.4. The fixed velocity is
  // 26214/65536, a hair under 0.4, and the offset there is -26216/65536, a hair
  // outside it, so the rocket waits one more tick and explodes at the middle of
  // the tile instead: 1, 5, 10, 15, 20, ... Tile spacing is unchanged, 5 ticks.
  const int expectedTick[] = { 0, 1, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70 };
  for (int to = 1; to <= 15; ++to)
  {
    RocketResult r = flyRocket(this, 1, 1, GameEntity::RIGHT_FLAG, to, 1);
    EXPECT_EQ(r.tick, expectedTick[to]) << "target " << to;
    EXPECT_EQ(r.tileX, to);
    EXPECT_EQ(r.tileY, 1);
  }
  // one fired past the end of the row is stopped by the wall, in the last open tile
  EXPECT_EQ(beyond.tick, 70);
  EXPECT_EQ(beyond.tileX, 15);
}

// A rocket created the way RocketSpinEntity creates it (a create message that
// carries the destination), in a real round: it must blow up where it was aimed,
// not at the wall and not at the origin.
TEST_F(FixedPointSimTest, ARocketCreatedByMessageExplodesInTheTileItWasAimedAt)
{
  configurePlayers(2);
  setMapSize(17, 13);
  startRound();
  destroyPlayer(1); // nothing but the rocket on the board: no bot to walk into its path
  destroyPlayer(2);
  clearBlocks();

  const int targets[] = { 5, 9, 13 };
  for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i)
  {
    const int target = targets[i];
    postRocket(0, 3, 1, GameEntity::RIGHT_FLAG, target, 1);
    em().processMessageQueue();
    ASSERT_EQ(countOfType(ROCKET_FIRED), 1);

    std::vector<std::pair<int, int> > blast;
    int framesTaken = 0;
    for (int f = 0; f < 200 && blast.empty(); ++f)
    {
      runFrames(1);
      ++framesTaken;
      DynamicMap * map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        if (it->second->getType() == EXPLOSION)
        {
          blast.push_back(std::make_pair(it->second->getTileX(), it->second->getTileY()));
        }
      }
    }
    // the plus-shaped blast around the target (odd columns, so the tile below is open)
    std::vector<std::pair<int, int> > expected;
    expected.push_back(std::make_pair(target, 1));
    expected.push_back(std::make_pair(target - 1, 1));
    expected.push_back(std::make_pair(target + 1, 1));
    expected.push_back(std::make_pair(target, 2));
    std::sort(blast.begin(), blast.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(blast, expected) << "target " << target << " after " << framesTaken << " frames";

    runFrames(30); // let the blast die out before the next rocket
  }
}

// The real path: a bot-style rocket spinner sees an enemy in line, fires, and the
// rocket it creates carries the enemy's tile as its target. The enemy is taken
// away after the launch, so the rocket can only explode in the target tile
// because it was told to (not by running into someone, or into the wall).
TEST_F(FixedPointSimTest, ARocketFiredByASpinnerExplodesInTheEnemyTileEvenIfTheEnemyLeaves)
{
  configurePlayers(2);
  setMapSize(9, 9);
  startRound();
  destroyPlayer(1);
  clearBlocks();
  player(2)->m_controlledByAI = false; // a human nobody is steering: it stays put
  // stand it in the middle of the bottom row, with open floor beyond it, so a
  // rocket that missed its target would visibly fly on past
  const int targetX = 5, targetY = 7;
  {
    std::shared_ptr<PlayerCharacterEntity> p = player(2);
    const int fromX = p->getTileX(), fromY = p->getTileY();
    p->setTileX(targetX);
    p->setTileY(targetY);
    EntityMessageQueue::getInstance()->postMoveMessage(p->getID(), fromX, fromY, targetX, targetY);
    em().processMessageQueue();
  }

  spawnEntity(ROCKET_SPIN, 1, targetX - 4, targetY); // 4 tiles to its left
  for (int f = 0; f < 50 && countOfType(ROCKET_FIRED) == 0; ++f)
  {
    runFrames(1);
  }
  ASSERT_EQ(countOfType(ROCKET_FIRED), 1) << "the spinner never fired";
  destroyPlayer(2); // gone before the rocket gets there

  std::vector<std::pair<int, int> > blast;
  for (int f = 0; f < 100 && blast.empty(); ++f)
  {
    runFrames(1);
    DynamicMap * map = em().getDynamicMap();
    for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
    {
      if (it->second->getType() == EXPLOSION)
      {
        blast.push_back(std::make_pair(it->second->getTileX(), it->second->getTileY()));
      }
    }
  }
  ASSERT_FALSE(blast.empty()) << "the rocket never exploded";
  // the plus-shaped blast around the target, wherever there is no wall (a rocket
  // next to a wall explodes twice in one tick, so compare as sets)
  std::set<std::pair<int, int> > expected;
  expected.insert(std::make_pair(targetX, targetY));
  const int dx[4] = { -1, 1, 0, 0 };
  const int dy[4] = { 0, 0, -1, 1 };
  for (int i = 0; i < 4; ++i)
  {
    if (Map::getInstance()->staticTileAt(targetX + dx[i], targetY + dy[i]) != Map::WALL)
    {
      expected.insert(std::make_pair(targetX + dx[i], targetY + dy[i]));
    }
  }
  const std::set<std::pair<int, int> > actual(blast.begin(), blast.end());
  EXPECT_EQ(actual, expected);
}

// ---------------------------------------------------------------------------
// A* open-list order
// ---------------------------------------------------------------------------

// A bot that is not in the round, so a test can run a path search and read
// everything it decided: the next tile, and the bomb-a-block instructions it
// queues as a side effect.
class PathProbe : public PlayerCharacterEntity
{
  public:
    PathProbe(int x, int y) : PlayerCharacterEntity(99998, 1, x, y, 0u) {}

    coord search(int targetX, int targetY, bool throughBlocks, Digest & into)
    {
      clearGoalsAndInstructions();
      const coord step = getNextTileFromPathfinding(targetX, targetY, throughBlocks);
      into.add(step.first);
      into.add(step.second);
      into.add((int)m_insList.size());
      for (std::list<AIInstruction *>::iterator it = m_insList.begin(); it != m_insList.end(); ++it)
      {
        into.add((*it)->m_type);
        into.add((*it)->m_targetAmount);
        into.add((int)(*it)->m_targetType);
        into.add((*it)->m_targetTileX);
        into.add((*it)->m_targetTileY);
      }
      clearGoalsAndInstructions();
      return step;
    }
};

// Scatters blocks over the free (non-pillar) tiles.
void scatterBlocks(int width, int height, int percent, uint32_t seed)
{
  Map * map = Map::getInstance();
  map->clearBlocks();
  Lcg lcg(seed);
  for (int x = 1; x < width - 1; ++x)
  {
    for (int y = 1; y < height - 1; ++y)
    {
      if ((x % 2 == 0) && (y % 2 == 0))
      {
        continue; // pillar
      }
      if (lcg.below(100) < percent)
      {
        map->setBlockAt(x, y, true);
      }
    }
  }
}

TEST_F(FixedPointSimTest, PathSearchPicksTheSameRoutesAsTheOriginalOpenList)
{
  // Many layouts, each searched between assorted start and target tiles, with
  // and without bombing through blocks. Open boards and light block cover give
  // a great many frontier nodes with equal cost + heuristic, which is exactly
  // when the order the open list pops them in decides the route.
  setMapSize(17, 13);
  Digest d;
  int searches = 0;
  for (int layout = 0; layout < 48; ++layout)
  {
    scatterBlocks(17, 13, (layout % 6) * 8, 5000 + layout);
    Lcg pick(900 + layout);
    for (int n = 0; n < 6; ++n)
    {
      int sx = 1 + pick.below(15), sy = 1 + pick.below(11);
      int tx = 1 + pick.below(15), ty = 1 + pick.below(11);
      if ((sx % 2 == 0 && sy % 2 == 0) || (tx % 2 == 0 && ty % 2 == 0))
      {
        continue; // pillars
      }
      // follow the route a bot would walk: search, step, search again
      for (int through = 0; through < 2; ++through)
      {
        int cx = sx, cy = sy;
        for (int hop = 0; hop < 40 && !(cx == tx && cy == ty); ++hop)
        {
          PathProbe bot(cx, cy);
          const coord step = bot.search(tx, ty, through != 0, d);
          ++searches;
          if (step.first == cx && step.second == cy)
          {
            break;
          }
          cx = step.first;
          cy = step.second;
        }
      }
    }
  }
  dump("pathsearch", d.value());
  if (std::getenv("BITRIOT_FIXED_DUMP"))
  {
    std::printf("DUMPX searches=%d\n", searches);
  }
  else
  {
    EXPECT_EQ(d.value(), 0x6c26a0d210c6953aull);
    EXPECT_EQ(searches, 2592);
  }
  EXPECT_EQ(PlayerCharacterEntity::livePathNodes(), 0);
}

TEST_F(FixedPointSimTest, PathSearchOnAnOpenBoardTakesTheSameFirstSteps)
{
  // No blocks at all: the most ties there can be. Each answer is the first
  // tile of the route the search chose among many equally short ones.
  setMapSize(17, 13);
  Map::getInstance()->clearBlocks();
  Digest ignored;
  struct Q { int sx, sy, tx, ty, ex, ey; };
  const Q qs[] = { {1,1,15,11, 2,1}, {15,11,1,1, 14,11}, {1,11,15,1, 2,11}, {15,1,1,11, 14,1}, {1,1,3,3, 2,1}, {3,3,1,1, 2,3}, {7,5,9,7, 8,5}, {1,1,1,11, 1,2}, {1,1,15,1, 2,1} };
  for (size_t i = 0; i < sizeof(qs) / sizeof(qs[0]); ++i)
  {
    PathProbe bot(qs[i].sx, qs[i].sy);
    coord step = bot.search(qs[i].tx, qs[i].ty, false, ignored);
    EXPECT_EQ(step.first, qs[i].ex) << "from (" << qs[i].sx << "," << qs[i].sy << ")";
    EXPECT_EQ(step.second, qs[i].ey) << "from (" << qs[i].sx << "," << qs[i].sy << ")";
  }
}

} // namespace
} // namespace bitriot_test
