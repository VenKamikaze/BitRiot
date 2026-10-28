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
#include <vector>

#include "AIInstruction.h"
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

typedef float Offset;

double offsetToDouble(Offset o)
{
  return o;
}

Offset offsetFromDouble(double d)
{
  return (float)d;
}

// The sprite's pixel offset from its tile's top-left, exactly as
// GameEntity::render() computes it.
int pixelOffset(Offset o)
{
  return (int)(o * (Map::TILE_WIDTH / 2));
}

Offset getVelocityOf(GameEntity & e)
{
  return e.getVelocity();
}

std::unique_ptr<RocketFiredEntity> makeRocket(int id, int owner, int atX, int atY,
    unsigned int flags, int toX, int toY)
{
  return std::unique_ptr<RocketFiredEntity>(
           new RocketFiredEntity(id, owner, atX, atY, flags, (float)toX, (float)toY));
}

void postRocket(int owner, int atX, int atY, unsigned int flags, int toX, int toY)
{
  EntityMessageQueue::getInstance()->postCreateMessage(ROCKET_FIRED, owner, atX, atY, flags,
      (float)toX, (float)toY);
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
TraceResult walkScript(GameEntity & e, uint32_t scriptSeed, int ticks)
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
    d.add(e.getTileX() * Map::TILE_WIDTH + pixelOffset(e.getOffsetX()));
    d.add(e.getTileY() * Map::TILE_WIDTH + pixelOffset(e.getOffsetY()));
  }
  TraceResult r;
  r.digest = d.value();
  r.crossingsX = crossings;
  r.finalPixelX = e.getTileX() * Map::TILE_WIDTH + pixelOffset(e.getOffsetX());
  r.finalPixelY = e.getTileY() * Map::TILE_WIDTH + pixelOffset(e.getOffsetY());
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

TraceResult traceKind(Kind k, int health, uint32_t seed)
{
  switch (k)
  {
    case PLAYER:
    {
      Mover<PlayerCharacterEntity> e(health, 9001, 1, 1, 1, 0u);
      return walkScript(e, seed, 600);
    }
    case ROBOT:
    {
      Mover<RobotEntity> e(health, 9001, 0, 1, 1, 0u);
      return walkScript(e, seed, 600);
    }
    case DRONE:
    {
      Mover<DroneEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600);
    }
    case WALL_EATER_K:
    {
      Mover<WallEaterEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600);
    }
    case EGG_THIEF_K:
    default:
    {
      Mover<EggThiefEntity> e(health, 9001, 0, 1, 1);
      return walkScript(e, seed, 600);
    }
  }
}

struct MoverCase
{
  const char * name;
  Kind kind;
  int health;
  uint32_t seed;
  // what the original float maths produced
  uint64_t digest;
  int crossingsX;
  int pixelX;
  int pixelY;
};

const MoverCase MOVER_CASES[] =
{
  { "player h200",       PLAYER,       200, 11, 0xe22fee89094c5921ull, 12, 32, 107 },
  { "player h100",       PLAYER,       100, 12, 0x43735d8ba130bc33ull, 23, 187, 224 },
  { "player h55",        PLAYER,        55, 13, 0x17b2cf5700f02250ull, 14, 89, 96 },
  { "player h10",        PLAYER,        10, 14, 0xe720da5f525f135eull, 6, 37, 32 },
  { "robot h38",         ROBOT,         38, 15, 0x93f01f6fc3a42ae3ull, 19, 49, 96 },
  { "robot h12",         ROBOT,         12, 16, 0x26c2733c38370891ull, 4, 34, 32 },
  { "drone h35",         DRONE,         35, 17, 0x72359ff1f04e1c04ull, 18, 47, 96 },
  { "drone h9",          DRONE,          9, 18, 0x6935a6b5208f6433ull, 0, 32, 96 },
  { "wall eater h25",    WALL_EATER_K,  25, 19, 0x4df2343d864e0070ull, 10, 160, 143 },
  { "wall eater h7",     WALL_EATER_K,   7, 20, 0xeb9cdb1cd4d00cefull, 0, 32, 71 },
  { "egg thief h20",     EGG_THIEF_K,   20, 21, 0xd9a2233cbe2358f9ull, 12, 32, 128 },
  { "egg thief h6",      EGG_THIEF_K,    6, 22, 0xedccf1386c3343bcull, 0, 33, 32 },
};

TEST_F(FixedPointSimTest, ScriptedWalksDrawEverySpriteOnTheSamePixelsAsTheOriginalMaths)
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
    d.add(e.getTileX() * Map::TILE_WIDTH + pixelOffset(e.getOffsetX()));
    d.add(e.getTileY() * Map::TILE_WIDTH + pixelOffset(e.getOffsetY()));
  }
  *tilesVisited = crossings;
  return d.value();
}

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
  { "robot owner0",      ROBOT,        0, 101, 0xf494f283f9f310d7ull, 161 },
  { "robot owner1",      ROBOT,        1, 102, 0xfdd19c715b2ccda9ull, 130 },
  { "drone owner0",      DRONE,        0, 103, 0x1ee497211d7badc0ull, 160 },
  { "drone owner2",      DRONE,        2, 104, 0x434f4aba2e5d5db0ull, 125 },
  { "wall eater owner0", WALL_EATER_K, 0, 105, 0xcdc85dafb27268d3ull, 127 },
  { "wall eater owner1", WALL_EATER_K, 1, 106, 0x5b6641f74261d630ull, 90 },
  { "egg thief owner0",  EGG_THIEF_K,  0, 107, 0xaa3925b742d38f8aull, 120 },
  { "egg thief owner3",  EGG_THIEF_K,  3, 108, 0x0c1ab876855679e4ull, 77 },
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
  EXPECT_EQ(d.value(), 0x0bdb3f6c29ac1b6full);

  // the same flights, read off for the first one: fired from (1,1) at (5,1) it
  // blows up in the target tile on tick 19, and one fired past the end of the
  // row is stopped by the wall and blows up in the last open tile
  RocketResult five = flyRocket(this, 1, 1, GameEntity::RIGHT_FLAG, 5, 1);
  EXPECT_EQ(five.tick, 19);
  EXPECT_EQ(five.tileX, 5);
  EXPECT_EQ(five.tileY, 1);
  EXPECT_EQ(beyond.tick, 69);
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
