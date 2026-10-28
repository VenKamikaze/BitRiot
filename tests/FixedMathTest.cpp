// Fixed-point maths (brief 02b): the helpers in src/Fixed.h, the velocities the
// entity classes derive from health, and the movement code, with expected values
// written out in raw Q16.16 integers (65536 = 1.0).
//
// The movement table was produced by an independent integer implementation of
// the original float algorithm, not by running this code, and
// FixedPointSimTest.cpp measures the same code against a float reference.

#include "support/HeadlessGame.h"

#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "DroneEntity.h"
#include "EggThiefEntity.h"
#include "EntityMessageQueue.h"
#include "Fixed.h"
#include "Map.h"
#include "PlayerCharacterEntity.h"
#include "RobotEntity.h"
#include "RocketFiredEntity.h"
#include "SimpleBrain.h"
#include "WallEaterEntity.h"

namespace bitriot_test
{
namespace
{

// ---------------------------------------------------------------------------
// The helpers
// ---------------------------------------------------------------------------

TEST(FixedHelpersTest, FromIntIsExactAndWorksForNegatives)
{
  EXPECT_EQ(FIXED_ONE, 65536);
  EXPECT_EQ(fixedFromInt(0), 0);
  EXPECT_EQ(fixedFromInt(1), 65536);
  EXPECT_EQ(fixedFromInt(-1), -65536);
  EXPECT_EQ(fixedFromInt(2), 131072);
  EXPECT_EQ(fixedFromInt(-7), -458752);
}

TEST(FixedHelpersTest, FromRatioRoundsToNearest)
{
  EXPECT_EQ(fixedFromRatio(1, 2), 32768);
  EXPECT_EQ(fixedFromRatio(1, 1), 65536);
  EXPECT_EQ(fixedFromRatio(0, 5), 0);
  EXPECT_EQ(fixedFromRatio(110, 400), 18022);  // 0.275  = 18022.4   rounds down
  EXPECT_EQ(fixedFromRatio(10, 25), 26214);    // 0.4    = 26214.4   rounds down
  EXPECT_EQ(fixedFromRatio(2, 3), 43691);      // 0.6667 = 43690.67  rounds up
  EXPECT_EQ(fixedFromRatio(1, 3), 21845);      // 0.3333 = 21845.33  rounds down
  EXPECT_EQ(fixedFromRatio(27, 160), 11059);   // 0.16875 = 11059.2
  EXPECT_EQ(fixedFromRatio(210, 400), 34406);  // 0.525  = 34406.4
}

TEST(FixedHelpersTest, FromRatioIsSymmetricForNegatives)
{
  EXPECT_EQ(fixedFromRatio(-110, 400), -18022);
  EXPECT_EQ(fixedFromRatio(-10, 25), -26214);
  EXPECT_EQ(fixedFromRatio(-2, 3), -43691);  // not -43690, which flooring would give
  EXPECT_EQ(fixedFromRatio(-1, 3), -21845);
  for (int n = -300; n <= 300; ++n)
  {
    EXPECT_EQ(fixedFromRatio(-n, 190), -fixedFromRatio(n, 190)) << n;
  }
}

TEST(FixedHelpersTest, ExactTiesRoundAwayFromZero)
{
  // n / 131072 scaled by 65536 is n / 2, so odd n lands exactly on a half.
  EXPECT_EQ(fixedFromRatio(1, 131072), 1);    // 0.5 -> 1
  EXPECT_EQ(fixedFromRatio(3, 131072), 2);    // 1.5 -> 2
  EXPECT_EQ(fixedFromRatio(5, 131072), 3);    // 2.5 -> 3 (round-half-even would say 2)
  EXPECT_EQ(fixedFromRatio(-1, 131072), -1);  // -0.5 -> -1
  EXPECT_EQ(fixedFromRatio(-3, 131072), -2);
  EXPECT_EQ(fixedFromRatio(-5, 131072), -3);
  // just either side of a tie
  EXPECT_EQ(fixedFromRatio(16000, 131072 * 16000 + 1), 0);  // 0.49999999...
  EXPECT_EQ(fixedFromRatio(16000, 131072 * 16000 - 1), 1);  // 0.50000000...
  EXPECT_EQ(fixedFromRatio(-16000, 131072 * 16000 + 1), 0);
  EXPECT_EQ(fixedFromRatio(-16000, 131072 * 16000 - 1), -1);
}

TEST(FixedHelpersTest, FromRatioWithANonPositiveDenominatorIsZero)
{
  EXPECT_EQ(fixedFromRatio(10, 0), 0);
  EXPECT_EQ(fixedFromRatio(-10, 0), 0);
  EXPECT_EQ(fixedFromRatio(10, -5), 0);
  EXPECT_EQ(fixedFromRatio(0, 0), 0);
}

TEST(FixedHelpersTest, FromRatioSaturatesRatherThanOverflowing)
{
  EXPECT_EQ(fixedFromRatio(INT32_MAX, 1), INT32_MAX);
  EXPECT_EQ(fixedFromRatio(-INT32_MAX, 1), -INT32_MAX);
  EXPECT_EQ(fixedFromRatio(INT32_MIN, 1), -INT32_MAX);
  EXPECT_EQ(fixedFromRatio(32767, 1), 32767 * 65536);   // the largest whole number that fits
  EXPECT_EQ(fixedFromRatio(32768, 1), INT32_MAX);       // one more does not
  EXPECT_EQ(fixedFromRatio(-32768, 1), -INT32_MAX);
}

TEST(FixedHelpersTest, FromRatioKeepsRepresentableResultsForLargeArguments)
{
  // the numerator is large but the quotient is not: it must not saturate
  EXPECT_EQ(fixedFromRatio(INT32_MAX, INT32_MAX), 65536);
  EXPECT_EQ(fixedFromRatio(INT32_MIN, INT32_MAX), -65536);   // -1.0000000005 rounds to -1.0
  EXPECT_EQ(fixedFromRatio(2000000000, 100000), 20000 * 65536);
  EXPECT_EQ(fixedFromRatio(-2000000000, 100000), -20000 * 65536);
  EXPECT_EQ(fixedFromRatio(INT32_MAX, 65536), INT32_MAX);        // exactly INT32_MAX, not a saturation
  EXPECT_EQ(fixedFromRatio(1, INT32_MAX), 0);                // 0.00003 of the smallest step
}

TEST(FixedHelpersTest, FromRatioCeilRoundsUp)
{
  EXPECT_EQ(fixedFromRatioCeil(10, 25), 26215);     // 26214.4 -> 26215 (nearest gives 26214)
  EXPECT_EQ(fixedFromRatio(10, 25), 26214);
  EXPECT_EQ(fixedFromRatioCeil(110, 400), 18023);   // 18022.4
  EXPECT_EQ(fixedFromRatioCeil(1, 3), 21846);       // 21845.33
  EXPECT_EQ(fixedFromRatioCeil(2, 3), 43691);       // 43690.67
  EXPECT_EQ(fixedFromRatioCeil(1, 2), 32768);       // exact stays exact
  EXPECT_EQ(fixedFromRatioCeil(4, 1), 262144);
  EXPECT_EQ(fixedFromRatioCeil(0, 7), 0);
  // toward +infinity, so a negative quotient moves toward zero
  EXPECT_EQ(fixedFromRatioCeil(-1, 3), -21845);     // -21845.33
  EXPECT_EQ(fixedFromRatioCeil(-2, 3), -43690);     // -43690.67
  EXPECT_EQ(fixedFromRatioCeil(-1, 2), -32768);
  // not below the true quotient, and within one unit of the nearest rounding
  for (int n = -300; n <= 300; ++n)
  {
    const fixed_t up = fixedFromRatioCeil(n, 190);
    const fixed_t nearest = fixedFromRatio(n, 190);
    EXPECT_GE((int64_t)up * 190, (int64_t)n * 65536) << n;
    EXPECT_GE(up, nearest) << n;
    EXPECT_LE(up - nearest, 1) << n;
  }
  // defined for bad denominators and saturates like fixedFromRatio
  EXPECT_EQ(fixedFromRatioCeil(10, 0), 0);
  EXPECT_EQ(fixedFromRatioCeil(10, -5), 0);
  EXPECT_EQ(fixedFromRatioCeil(INT32_MAX, 1), INT32_MAX);
  EXPECT_EQ(fixedFromRatioCeil(INT32_MIN, 1), -INT32_MAX);
  EXPECT_EQ(fixedFromRatioCeil(INT32_MAX, INT32_MAX), 65536);
}

TEST(FixedHelpersTest, AbsIsSymmetricAndSaturatesTheOneBadValue)
{
  EXPECT_EQ(fixedAbs(0), 0);
  EXPECT_EQ(fixedAbs(26214), 26214);
  EXPECT_EQ(fixedAbs(-26214), 26214);
  EXPECT_EQ(fixedAbs(-1), 1);
  EXPECT_EQ(fixedAbs(INT32_MAX), INT32_MAX);
  EXPECT_EQ(fixedAbs(-INT32_MAX), INT32_MAX);
  EXPECT_EQ(fixedAbs(INT32_MIN), INT32_MAX);
}

TEST(FixedHelpersTest, ToPixelsTruncatesTowardZeroLikeTheOldIntCast)
{
  EXPECT_EQ(fixedToPixels(0, 16), 0);
  EXPECT_EQ(fixedToPixels(FIXED_ONE, 16), 16);
  EXPECT_EQ(fixedToPixels(-FIXED_ONE, 16), -16);
  EXPECT_EQ(fixedToPixels(FIXED_ONE / 2, 16), 8);
  EXPECT_EQ(fixedToPixels(-FIXED_ONE / 2, 16), -8);
  EXPECT_EQ(fixedToPixels(65535, 16), 15);     // 15.9998
  EXPECT_EQ(fixedToPixels(-65535, 16), -15);   // not -16, which flooring would give
  EXPECT_EQ(fixedToPixels(1, 16), 0);
  EXPECT_EQ(fixedToPixels(-1, 16), 0);         // a floor, or >> 16, would give -1
  EXPECT_EQ(fixedToPixels(-4095, 16), 0);      // -0.98 px
  EXPECT_EQ(fixedToPixels(-4096, 16), -1);     // exactly -1 px
}

TEST(FixedHelpersTest, ToPixelsMatchesTheOldRenderingExpressionForEveryOffset)
{
  // GameEntity::render() and the HUD used (int)(offset * (TILE_WIDTH / 2)) on a
  // float. Every fixed offset from -1 to +1 is exactly representable as a float
  // and so is the product, so the two must agree on every one.
  const int half = Map::TILE_WIDTH / 2;
  for (int raw = -65536 - 4; raw <= 65536 + 4; ++raw)
  {
    const float offset = (float)raw / 65536.0f;
    ASSERT_EQ(fixedToPixels(raw, half), (int)(offset * half)) << "raw " << raw;
  }
}

// ---------------------------------------------------------------------------
// Entity velocities
// ---------------------------------------------------------------------------

class FixedMathTest : public HeadlessGameTest
{
  protected:
    void TearDown() override
    {
      EntityMessageQueue::getInstance()->clear();
      HeadlessGameTest::TearDown();
    }
};

template <class T>
class Mover : public T
{
  public:
    template <class... Args>
    Mover(int health, Args... args) : T(args...)
    {
      this->m_health = health;
      this->update(); // owner 0 does not decay: this recomputes velocity from health
      this->setTileX(3);
      this->setTileY(3);
      this->setOffsetX(0);
      this->setOffsetY(0);
    }
    void put(int tileX, int tileY, fixed_t offX, fixed_t offY)
    {
      this->setTileX(tileX);
      this->setTileY(tileY);
      this->setOffsetX(offX);
      this->setOffsetY(offY);
    }
};

struct VelocityRow
{
  const char * name;
  int health;
  fixed_t expected;       // raw Q16.16
  double floatVelocity;   // what the float code computed, to 9 places
  double floatTicks;      // 2 / floatVelocity: ticks to cross one tile
  int floatTicksTenTiles; // ticks the float code took to cross ten tiles from rest
  int fixedTicksTenTiles; // and the fixed-point code (same unless noted)
};

// (health + extra) / modifier, rounded to nearest:
//   player (h+10)/400, robot (h+2)/190, drone (h+2)/180, wall eater (h+2)/160,
//   egg thief (h+2)/140. (The rocket's 10/25 is rounded up instead; its own test
//   is below.)
// Each row also records what the float code gave, so the table shows what the
// conversion changed. A velocity is off by at most half of 1/65536 (so a tile takes
// at most 0.03% longer or shorter, at the lowest health where it takes 100+
// ticks), and ten tiles take the same number of ticks as before, except the
// slowest player (1 more tick in 691). The whole number of ticks per tile does not
// change anywhere.
const VelocityRow PLAYER_ROWS[] =
{
  { "player initial (100)", 100, 18022, 0.275000006, 7.27273, 70, 70 },
  { "player max (200)",     200, 34406, 0.524999976, 3.80952, 37, 37 },
  { "player low (1)",         1,  1802, 0.027500000, 72.72727, 691, 692 },
};
const VelocityRow ROBOT_ROWS[] =
{
  { "robot initial (38)",    38, 13797, 0.210526317, 9.50000, 91, 91 },
  { "robot low (1)",          1,  1035, 0.015789473, 126.66667, 1204, 1204 },
};
const VelocityRow DRONE_ROWS[] =
{
  { "drone initial (35)",    35, 13471, 0.205555558, 9.72973, 93, 93 },
  { "drone low (1)",          1,  1092, 0.016666668, 119.99999, 1141, 1141 },
};
const VelocityRow WALL_EATER_ROWS[] =
{
  { "wall eater initial (25)", 25, 11059, 0.168750003, 11.85185, 113, 113 },
  { "wall eater low (1)",       1,  1229, 0.018750001, 106.66666, 1014, 1014 },
};
const VelocityRow EGG_THIEF_ROWS[] =
{
  { "egg thief initial (20)", 20, 10299, 0.157142863, 12.72727, 121, 121 },
  { "egg thief low (1)",       1,  1404, 0.021428572, 93.33333, 887, 887 },
};

// Ticks for an entity at rest on tile 1 to reach tile 11 along an open row.
template <class E>
int ticksToCrossTenTiles(E & e)
{
  e.put(1, 1, 0, 0);
  int ticks = 0;
  while (e.getTileX() < 11 && ticks < 5000)
  {
    e.moveInDirection(RIGHT);
    EntityMessageQueue::getInstance()->clear();
    ++ticks;
  }
  return ticks;
}

template <class Row, class E>
void checkRow(const Row & row, E & e)
{
  const fixed_t actual = e.getVelocity();
  EXPECT_EQ(ticksToCrossTenTiles(e), row.fixedTicksTenTiles) << row.name;
  EXPECT_EQ(actual, row.expected) << row.name;
  // within half a unit in the last place of the float computation
  EXPECT_NEAR(actual / 65536.0, row.floatVelocity, 0.5 / 65536.0 + 1e-8) << row.name;
  // and so the time a tile takes agrees with the float version to 0.03%
  EXPECT_NEAR(131072.0 / actual, row.floatTicks, row.floatTicks * 3e-4) << row.name;
}

TEST_F(FixedMathTest, PlayerVelocityComesFromHealthAsRoundedToNearest)
{
  setMapSize(17, 13);
  Map::getInstance()->clearBlocks();
  for (const VelocityRow & row : PLAYER_ROWS)
  {
    Mover<PlayerCharacterEntity> e(row.health, 9001, 1, 3, 3, 0u);
    checkRow(row, e);
  }
}

TEST_F(FixedMathTest, MonsterAndRobotVelocitiesComeFromHealthAsRoundedToNearest)
{
  setMapSize(17, 13);
  Map::getInstance()->clearBlocks();
  for (const VelocityRow & row : ROBOT_ROWS)
  {
    Mover<RobotEntity> e(row.health, 9001, 0, 3, 3, 0u);
    checkRow(row, e);
  }
  for (const VelocityRow & row : DRONE_ROWS)
  {
    Mover<DroneEntity> e(row.health, 9001, 0, 3, 3);
    checkRow(row, e);
  }
  for (const VelocityRow & row : WALL_EATER_ROWS)
  {
    Mover<WallEaterEntity> e(row.health, 9001, 0, 3, 3);
    checkRow(row, e);
  }
  for (const VelocityRow & row : EGG_THIEF_ROWS)
  {
    Mover<EggThiefEntity> e(row.health, 9001, 0, 3, 3);
    checkRow(row, e);
  }
}

TEST_F(FixedMathTest, RocketVelocityIsATenthOverTheDataFileValueRoundedUp)
{
  setMapSize(9, 9);
  RocketFiredEntity rocket(9001, 1, 3, 3, GameEntity::RIGHT_FLAG, 5, 3);
  EXPECT_EQ(rocket.getVelocity(), 26215); // 10 / ROCKET_VELOCITY(25) = 0.4, rounded UP (nearest is 26214)
}

TEST_F(FixedMathTest, VelocityOfAnEntityWithNegativeHealthIsDefined)
{
  // health + 10 below zero gives a negative velocity, symmetric with a positive
  // one, as the float code did; never undefined behaviour
  setMapSize(9, 9);
  Mover<PlayerCharacterEntity> e(-50, 9001, 1, 3, 3, 0u); // (-50 + 10) / 400 = -0.1
  EXPECT_EQ(e.getVelocity(), -6554);
  Mover<PlayerCharacterEntity> f(30, 9001, 1, 3, 3, 0u);  // (30 + 10) / 400 = +0.1
  EXPECT_EQ(f.getVelocity(), 6554);
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

struct MoveRow
{
  DIRECTION dir;
  int blockedAhead;           // a block on the tile in that direction
  fixed_t startX, startY;     // offsets before
  int moves;
  int tileX, tileY;           // tile after (the entity starts on tile 3,3)
  fixed_t offX, offY;         // offsets after
};

// Player at 100 health (velocity 18022) on an open 9x9 board, starting on tile
// (3,3). Rows from an independent integer model of the original algorithm.
// Wrapping into the next tile happens when an offset passes +-65536 (not when it
// reaches it: the rows starting at +-47514 land exactly on +-1.0 and stay put).
const MoveRow MOVE_ROWS[] =
{
  { UP   , 0,      0,      0, 1, 3, 3,      0, -18022 },
  { UP   , 0,  20000,      0, 1, 3, 3,   1978,      0 },
  { UP   , 0, -20000,      0, 1, 3, 3,  -1978,      0 },
  { UP   , 0,      0,  20000, 1, 3, 3,      0,   1978 },
  { UP   , 0,      0, -20000, 1, 3, 3,      0, -38022 },
  { UP   , 0,  20000, -20000, 1, 3, 3,   1978, -20000 },
  { UP   , 0, -20000,  20000, 1, 3, 3,  -1978,  20000 },
  { UP   , 0,  64000,      0, 1, 3, 3,  45978,      0 },
  { UP   , 0, -64000,      0, 1, 3, 3, -45978,      0 },
  { UP   , 0,      0,  64000, 1, 3, 3,      0,  45978 },
  { UP   , 0,      0, -64000, 1, 3, 2,      0,  49050 },
  { UP   , 0,  47514,      0, 1, 3, 3,  29492,      0 },
  { UP   , 0, -47514,      0, 1, 3, 3, -29492,      0 },
  { UP   , 0,      0,  47514, 1, 3, 3,      0,  29492 },
  { UP   , 0,      0, -47514, 1, 3, 3,      0, -65536 },
  { UP   , 1,      0,      0, 1, 3, 3,      0,      0 },
  { UP   , 1,  20000,      0, 1, 3, 3,  20000,      0 },
  { UP   , 1, -20000,      0, 1, 3, 3, -20000,      0 },
  { UP   , 1,      0,  20000, 1, 3, 3,      0,   1978 },
  { UP   , 1,      0, -20000, 1, 3, 3,      0,      0 },
  { UP   , 1,  20000, -20000, 1, 3, 3,  40000, -20000 },
  { UP   , 1, -20000,  20000, 1, 3, 3,  -1978,  20000 },
  { UP   , 1,  64000,      0, 1, 3, 3,  64000,      0 },
  { UP   , 1, -64000,      0, 1, 3, 3, -64000,      0 },
  { UP   , 1,      0,  64000, 1, 3, 3,      0,  45978 },
  { UP   , 1,      0, -64000, 1, 3, 3,      0,      0 },
  { UP   , 1,  47514,      0, 1, 3, 3,  47514,      0 },
  { UP   , 1, -47514,      0, 1, 3, 3, -47514,      0 },
  { UP   , 1,      0,  47514, 1, 3, 3,      0,  29492 },
  { UP   , 1,      0, -47514, 1, 3, 3,      0,      0 },
  { DOWN , 0,      0,      0, 1, 3, 3,      0,  18022 },
  { DOWN , 0,  20000,      0, 1, 3, 3,   1978,      0 },
  { DOWN , 0, -20000,      0, 1, 3, 3,  -1978,      0 },
  { DOWN , 0,      0,  20000, 1, 3, 3,      0,  38022 },
  { DOWN , 0,      0, -20000, 1, 3, 3,      0,  -1978 },
  { DOWN , 0,  20000, -20000, 1, 3, 3,   1978, -20000 },
  { DOWN , 0, -20000,  20000, 1, 3, 3,  -1978,  20000 },
  { DOWN , 0,  64000,      0, 1, 3, 3,  45978,      0 },
  { DOWN , 0, -64000,      0, 1, 3, 3, -45978,      0 },
  { DOWN , 0,      0,  64000, 1, 3, 4,      0, -49050 },
  { DOWN , 0,      0, -64000, 1, 3, 3,      0, -45978 },
  { DOWN , 0,  47514,      0, 1, 3, 3,  29492,      0 },
  { DOWN , 0, -47514,      0, 1, 3, 3, -29492,      0 },
  { DOWN , 0,      0,  47514, 1, 3, 3,      0,  65536 },
  { DOWN , 0,      0, -47514, 1, 3, 3,      0, -29492 },
  { DOWN , 1,      0,      0, 1, 3, 3,      0,      0 },
  { DOWN , 1,  20000,      0, 1, 3, 3,  20000,      0 },
  { DOWN , 1, -20000,      0, 1, 3, 3, -20000,      0 },
  { DOWN , 1,      0,  20000, 1, 3, 3,      0,      0 },
  { DOWN , 1,      0, -20000, 1, 3, 3,      0,  -1978 },
  { DOWN , 1,  20000, -20000, 1, 3, 3,   1978, -20000 },
  { DOWN , 1, -20000,  20000, 1, 3, 3, -40000,  20000 },
  { DOWN , 1,  64000,      0, 1, 3, 3,  64000,      0 },
  { DOWN , 1, -64000,      0, 1, 3, 3, -64000,      0 },
  { DOWN , 1,      0,  64000, 1, 3, 3,      0,      0 },
  { DOWN , 1,      0, -64000, 1, 3, 3,      0, -45978 },
  { DOWN , 1,  47514,      0, 1, 3, 3,  47514,      0 },
  { DOWN , 1, -47514,      0, 1, 3, 3, -47514,      0 },
  { DOWN , 1,      0,  47514, 1, 3, 3,      0,      0 },
  { DOWN , 1,      0, -47514, 1, 3, 3,      0, -29492 },
  { LEFT , 0,      0,      0, 1, 3, 3, -18022,      0 },
  { LEFT , 0,  20000,      0, 1, 3, 3,   1978,      0 },
  { LEFT , 0, -20000,      0, 1, 3, 3, -38022,      0 },
  { LEFT , 0,      0,  20000, 1, 3, 3,      0,   1978 },
  { LEFT , 0,      0, -20000, 1, 3, 3,      0,  -1978 },
  { LEFT , 0,  20000, -20000, 1, 3, 3,  20000,  -1978 },
  { LEFT , 0, -20000,  20000, 1, 3, 3, -20000,   1978 },
  { LEFT , 0,  64000,      0, 1, 3, 3,  45978,      0 },
  { LEFT , 0, -64000,      0, 1, 2, 3,  49050,      0 },
  { LEFT , 0,      0,  64000, 1, 3, 3,      0,  45978 },
  { LEFT , 0,      0, -64000, 1, 3, 3,      0, -45978 },
  { LEFT , 0,  47514,      0, 1, 3, 3,  29492,      0 },
  { LEFT , 0, -47514,      0, 1, 3, 3, -65536,      0 },
  { LEFT , 0,      0,  47514, 1, 3, 3,      0,  29492 },
  { LEFT , 0,      0, -47514, 1, 3, 3,      0, -29492 },
  { LEFT , 1,      0,      0, 1, 3, 3,      0,      0 },
  { LEFT , 1,  20000,      0, 1, 3, 3,   1978,      0 },
  { LEFT , 1, -20000,      0, 1, 3, 3,      0,      0 },
  { LEFT , 1,      0,  20000, 1, 3, 3,      0,  20000 },
  { LEFT , 1,      0, -20000, 1, 3, 3,      0, -20000 },
  { LEFT , 1,  20000, -20000, 1, 3, 3,  20000,  -1978 },
  { LEFT , 1, -20000,  20000, 1, 3, 3, -20000,  40000 },
  { LEFT , 1,  64000,      0, 1, 3, 3,  45978,      0 },
  { LEFT , 1, -64000,      0, 1, 3, 3,      0,      0 },
  { LEFT , 1,      0,  64000, 1, 3, 3,      0,  64000 },
  { LEFT , 1,      0, -64000, 1, 3, 3,      0, -64000 },
  { LEFT , 1,  47514,      0, 1, 3, 3,  29492,      0 },
  { LEFT , 1, -47514,      0, 1, 3, 3,      0,      0 },
  { LEFT , 1,      0,  47514, 1, 3, 3,      0,  47514 },
  { LEFT , 1,      0, -47514, 1, 3, 3,      0, -47514 },
  { RIGHT, 0,      0,      0, 1, 3, 3,  18022,      0 },
  { RIGHT, 0,  20000,      0, 1, 3, 3,  38022,      0 },
  { RIGHT, 0, -20000,      0, 1, 3, 3,  -1978,      0 },
  { RIGHT, 0,      0,  20000, 1, 3, 3,      0,   1978 },
  { RIGHT, 0,      0, -20000, 1, 3, 3,      0,  -1978 },
  { RIGHT, 0,  20000, -20000, 1, 3, 3,  20000,  -1978 },
  { RIGHT, 0, -20000,  20000, 1, 3, 3, -20000,   1978 },
  { RIGHT, 0,  64000,      0, 1, 4, 3, -49050,      0 },
  { RIGHT, 0, -64000,      0, 1, 3, 3, -45978,      0 },
  { RIGHT, 0,      0,  64000, 1, 3, 3,      0,  45978 },
  { RIGHT, 0,      0, -64000, 1, 3, 3,      0, -45978 },
  { RIGHT, 0,  47514,      0, 1, 3, 3,  65536,      0 },
  { RIGHT, 0, -47514,      0, 1, 3, 3, -29492,      0 },
  { RIGHT, 0,      0,  47514, 1, 3, 3,      0,  29492 },
  { RIGHT, 0,      0, -47514, 1, 3, 3,      0, -29492 },
  { RIGHT, 1,      0,      0, 1, 3, 3,      0,      0 },
  { RIGHT, 1,  20000,      0, 1, 3, 3,      0,      0 },
  { RIGHT, 1, -20000,      0, 1, 3, 3,  -1978,      0 },
  { RIGHT, 1,      0,  20000, 1, 3, 3,      0,  20000 },
  { RIGHT, 1,      0, -20000, 1, 3, 3,      0, -20000 },
  { RIGHT, 1,  20000, -20000, 1, 3, 3,  20000, -40000 },
  { RIGHT, 1, -20000,  20000, 1, 3, 3, -20000,   1978 },
  { RIGHT, 1,  64000,      0, 1, 3, 3,      0,      0 },
  { RIGHT, 1, -64000,      0, 1, 3, 3, -45978,      0 },
  { RIGHT, 1,      0,  64000, 1, 3, 3,      0,  64000 },
  { RIGHT, 1,      0, -64000, 1, 3, 3,      0, -64000 },
  { RIGHT, 1,  47514,      0, 1, 3, 3,      0,      0 },
  { RIGHT, 1, -47514,      0, 1, 3, 3, -29492,      0 },
  { RIGHT, 1,      0,  47514, 1, 3, 3,      0,  47514 },
  { RIGHT, 1,      0, -47514, 1, 3, 3,      0, -47514 },
  { UP   , 0,      0,      0, 6, 3, 2,      0,  22940 },
  { UP   , 0,  20000,      0, 6, 3, 2,      0,  42940 },
  { UP   , 0, -20000,      0, 6, 3, 2,      0,  42940 },
  { UP   , 0,      0,  20000, 6, 3, 2,      0,  42940 },
  { UP   , 0,      0, -20000, 6, 3, 2,      0,   2940 },
  { UP   , 0,  20000, -20000, 6, 3, 2,      0,  22940 },
  { UP   , 0, -20000,  20000, 6, 3, 2,      0,  62940 },
  { UP   , 0,  64000,      0, 6, 3, 3,      0, -44132 },
  { UP   , 0, -64000,      0, 6, 3, 3,      0, -44132 },
  { UP   , 0,      0,  64000, 6, 3, 3,      0, -44132 },
  { UP   , 0,      0, -64000, 6, 3, 2,      0, -41060 },
  { UP   , 0,  47514,      0, 6, 3, 3,      0, -60618 },
  { UP   , 0, -47514,      0, 6, 3, 3,      0, -60618 },
  { UP   , 0,      0,  47514, 6, 3, 3,      0, -60618 },
  { UP   , 0,      0, -47514, 6, 3, 2,      0, -24574 },
  { UP   , 1,      0,      0, 6, 3, 3,      0,      0 },
  { UP   , 1,  20000,      0, 6, 3, 3,  20000,      0 },
  { UP   , 1, -20000,      0, 6, 3, 3, -20000,      0 },
  { UP   , 1,      0,  20000, 6, 3, 3,      0,      0 },
  { UP   , 1,      0, -20000, 6, 3, 3,      0,      0 },
  { UP   , 1,  20000, -20000, 6, 3, 3,  60000, -20000 },
  { UP   , 1, -20000,  20000, 6, 3, 3,      0,      0 },
  { UP   , 1,  64000,      0, 6, 3, 3,  64000,      0 },
  { UP   , 1, -64000,      0, 6, 3, 3, -64000,      0 },
  { UP   , 1,      0,  64000, 6, 3, 3,      0,      0 },
  { UP   , 1,      0, -64000, 6, 3, 3,      0,      0 },
  { UP   , 1,  47514,      0, 6, 3, 3,  47514,      0 },
  { UP   , 1, -47514,      0, 6, 3, 3, -47514,      0 },
  { UP   , 1,      0,  47514, 6, 3, 3,      0,      0 },
  { UP   , 1,      0, -47514, 6, 3, 3,      0,      0 },
  { DOWN , 0,      0,      0, 6, 3, 4,      0, -22940 },
  { DOWN , 0,  20000,      0, 6, 3, 4,      0, -42940 },
  { DOWN , 0, -20000,      0, 6, 3, 4,      0, -42940 },
  { DOWN , 0,      0,  20000, 6, 3, 4,      0,  -2940 },
  { DOWN , 0,      0, -20000, 6, 3, 4,      0, -42940 },
  { DOWN , 0,  20000, -20000, 6, 3, 4,      0, -62940 },
  { DOWN , 0, -20000,  20000, 6, 3, 4,      0, -22940 },
  { DOWN , 0,  64000,      0, 6, 3, 3,      0,  44132 },
  { DOWN , 0, -64000,      0, 6, 3, 3,      0,  44132 },
  { DOWN , 0,      0,  64000, 6, 3, 4,      0,  41060 },
  { DOWN , 0,      0, -64000, 6, 3, 3,      0,  44132 },
  { DOWN , 0,  47514,      0, 6, 3, 3,      0,  60618 },
  { DOWN , 0, -47514,      0, 6, 3, 3,      0,  60618 },
  { DOWN , 0,      0,  47514, 6, 3, 4,      0,  24574 },
  { DOWN , 0,      0, -47514, 6, 3, 3,      0,  60618 },
  { DOWN , 1,      0,      0, 6, 3, 3,      0,      0 },
  { DOWN , 1,  20000,      0, 6, 3, 3,  20000,      0 },
  { DOWN , 1, -20000,      0, 6, 3, 3, -20000,      0 },
  { DOWN , 1,      0,  20000, 6, 3, 3,      0,      0 },
  { DOWN , 1,      0, -20000, 6, 3, 3,      0,      0 },
  { DOWN , 1,  20000, -20000, 6, 3, 3,      0,      0 },
  { DOWN , 1, -20000,  20000, 6, 3, 3, -60000,  20000 },
  { DOWN , 1,  64000,      0, 6, 3, 3,  64000,      0 },
  { DOWN , 1, -64000,      0, 6, 3, 3, -64000,      0 },
  { DOWN , 1,      0,  64000, 6, 3, 3,      0,      0 },
  { DOWN , 1,      0, -64000, 6, 3, 3,      0,      0 },
  { DOWN , 1,  47514,      0, 6, 3, 3,  47514,      0 },
  { DOWN , 1, -47514,      0, 6, 3, 3, -47514,      0 },
  { DOWN , 1,      0,  47514, 6, 3, 3,      0,      0 },
  { DOWN , 1,      0, -47514, 6, 3, 3,      0,      0 },
  { LEFT , 0,      0,      0, 6, 2, 3,  22940,      0 },
  { LEFT , 0,  20000,      0, 6, 2, 3,  42940,      0 },
  { LEFT , 0, -20000,      0, 6, 2, 3,   2940,      0 },
  { LEFT , 0,      0,  20000, 6, 2, 3,  42940,      0 },
  { LEFT , 0,      0, -20000, 6, 2, 3,  42940,      0 },
  { LEFT , 0,  20000, -20000, 6, 2, 3,  62940,      0 },
  { LEFT , 0, -20000,  20000, 6, 2, 3,  22940,      0 },
  { LEFT , 0,  64000,      0, 6, 3, 3, -44132,      0 },
  { LEFT , 0, -64000,      0, 6, 2, 3, -41060,      0 },
  { LEFT , 0,      0,  64000, 6, 3, 3, -44132,      0 },
  { LEFT , 0,      0, -64000, 6, 3, 3, -44132,      0 },
  { LEFT , 0,  47514,      0, 6, 3, 3, -60618,      0 },
  { LEFT , 0, -47514,      0, 6, 2, 3, -24574,      0 },
  { LEFT , 0,      0,  47514, 6, 3, 3, -60618,      0 },
  { LEFT , 0,      0, -47514, 6, 3, 3, -60618,      0 },
  { LEFT , 1,      0,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1,  20000,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1, -20000,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1,      0,  20000, 6, 3, 3,      0,  20000 },
  { LEFT , 1,      0, -20000, 6, 3, 3,      0, -20000 },
  { LEFT , 1,  20000, -20000, 6, 3, 3,      0,      0 },
  { LEFT , 1, -20000,  20000, 6, 3, 3, -20000,  60000 },
  { LEFT , 1,  64000,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1, -64000,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1,      0,  64000, 6, 3, 3,      0,  64000 },
  { LEFT , 1,      0, -64000, 6, 3, 3,      0, -64000 },
  { LEFT , 1,  47514,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1, -47514,      0, 6, 3, 3,      0,      0 },
  { LEFT , 1,      0,  47514, 6, 3, 3,      0,  47514 },
  { LEFT , 1,      0, -47514, 6, 3, 3,      0, -47514 },
  { RIGHT, 0,      0,      0, 6, 4, 3, -22940,      0 },
  { RIGHT, 0,  20000,      0, 6, 4, 3,  -2940,      0 },
  { RIGHT, 0, -20000,      0, 6, 4, 3, -42940,      0 },
  { RIGHT, 0,      0,  20000, 6, 4, 3, -42940,      0 },
  { RIGHT, 0,      0, -20000, 6, 4, 3, -42940,      0 },
  { RIGHT, 0,  20000, -20000, 6, 4, 3, -22940,      0 },
  { RIGHT, 0, -20000,  20000, 6, 4, 3, -62940,      0 },
  { RIGHT, 0,  64000,      0, 6, 4, 3,  41060,      0 },
  { RIGHT, 0, -64000,      0, 6, 3, 3,  44132,      0 },
  { RIGHT, 0,      0,  64000, 6, 3, 3,  44132,      0 },
  { RIGHT, 0,      0, -64000, 6, 3, 3,  44132,      0 },
  { RIGHT, 0,  47514,      0, 6, 4, 3,  24574,      0 },
  { RIGHT, 0, -47514,      0, 6, 3, 3,  60618,      0 },
  { RIGHT, 0,      0,  47514, 6, 3, 3,  60618,      0 },
  { RIGHT, 0,      0, -47514, 6, 3, 3,  60618,      0 },
  { RIGHT, 1,      0,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1,  20000,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1, -20000,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1,      0,  20000, 6, 3, 3,      0,  20000 },
  { RIGHT, 1,      0, -20000, 6, 3, 3,      0, -20000 },
  { RIGHT, 1,  20000, -20000, 6, 3, 3,  20000, -60000 },
  { RIGHT, 1, -20000,  20000, 6, 3, 3,      0,      0 },
  { RIGHT, 1,  64000,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1, -64000,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1,      0,  64000, 6, 3, 3,      0,  64000 },
  { RIGHT, 1,      0, -64000, 6, 3, 3,      0, -64000 },
  { RIGHT, 1,  47514,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1, -47514,      0, 6, 3, 3,      0,      0 },
  { RIGHT, 1,      0,  47514, 6, 3, 3,      0,  47514 },
  { RIGHT, 1,      0, -47514, 6, 3, 3,      0, -47514 },
};

TEST_F(FixedMathTest, MovementProducesTheExpectedOffsetsAndTileChanges)
{
  setMapSize(9, 9);
  for (const MoveRow & row : MOVE_ROWS)
  {
    Map::getInstance()->clearBlocks();
    int bx = 3, by = 3;
    switch (row.dir)
    {
      case UP:    by = 2; break;
      case DOWN:  by = 4; break;
      case LEFT:  bx = 2; break;
      default:    bx = 4; break;
    }
    if (row.blockedAhead)
    {
      Map::getInstance()->setBlockAt(bx, by, true);
    }
    Mover<PlayerCharacterEntity> e(100, 9001, 1, 3, 3, 0u);
    ASSERT_EQ(e.getVelocity(), 18022);
    e.put(3, 3, row.startX, row.startY);
    for (int m = 0; m < row.moves; ++m)
    {
      e.moveInDirection(row.dir);
      EntityMessageQueue::getInstance()->clear();
    }
    EXPECT_EQ(e.getTileX(), row.tileX) << "dir " << row.dir << " blocked " << row.blockedAhead
                                       << " start " << row.startX << "," << row.startY << " moves " << row.moves;
    EXPECT_EQ(e.getTileY(), row.tileY) << "dir " << row.dir << " blocked " << row.blockedAhead
                                       << " start " << row.startX << "," << row.startY << " moves " << row.moves;
    EXPECT_EQ(e.getOffsetX(), row.offX) << "dir " << row.dir << " blocked " << row.blockedAhead
                                        << " start " << row.startX << "," << row.startY << " moves " << row.moves;
    EXPECT_EQ(e.getOffsetY(), row.offY) << "dir " << row.dir << " blocked " << row.blockedAhead
                                        << " start " << row.startX << "," << row.startY << " moves " << row.moves;
  }
}

TEST_F(FixedMathTest, AWallStopsAnEntityExactlyOnTheMiddleOfItsTile)
{
  // tile (1,1) has the outer wall above and to its left
  setMapSize(9, 9);
  Map::getInstance()->clearBlocks();
  Mover<PlayerCharacterEntity> e(100, 9001, 1, 1, 1, 0u);
  e.put(1, 1, 0, 0);
  for (int i = 0; i < 20; ++i)
  {
    e.moveInDirection(UP);
    e.moveInDirection(LEFT);
    EntityMessageQueue::getInstance()->clear();
  }
  EXPECT_EQ(e.getTileX(), 1);
  EXPECT_EQ(e.getTileY(), 1);
  EXPECT_EQ(e.getOffsetX(), 0);
  EXPECT_EQ(e.getOffsetY(), 0);

  // and from a little off centre it comes to rest on the middle, not past it
  e.put(1, 1, 5000, 7000);
  for (int i = 0; i < 20; ++i)
  {
    e.moveInDirection(UP);
    EntityMessageQueue::getInstance()->clear();
  }
  EXPECT_EQ(e.getTileY(), 1);
  EXPECT_EQ(e.getOffsetY(), 0);
}

TEST_F(FixedMathTest, WalkingAcrossTilesNeverDriftsOutOfRange)
{
  setMapSize(17, 13);
  Map::getInstance()->clearBlocks();
  Mover<PlayerCharacterEntity> e(200, 9001, 1, 1, 1, 0u);
  e.put(1, 1, 0, 0);
  for (int i = 0; i < 400; ++i)
  {
    e.moveInDirection(RIGHT);
    EntityMessageQueue::getInstance()->clear();
    ASSERT_LE(e.getOffsetX(), FIXED_ONE);
    ASSERT_GE(e.getOffsetX(), -FIXED_ONE);
  }
  EXPECT_EQ(e.getTileX(), 15); // stopped by the east wall
  EXPECT_EQ(e.getOffsetX(), 0);
}

// ---------------------------------------------------------------------------
// "Within one step of the middle of the tile"
// ---------------------------------------------------------------------------

class NearProbe : public PlayerCharacterEntity
{
  public:
    NearProbe() : PlayerCharacterEntity(9001, 1, 3, 3, 0u) {}
    bool near(fixed_t offX, fixed_t offY, fixed_t velocity)
    {
      m_offsetX = offX;
      m_offsetY = offY;
      m_velocity = velocity;
      return nearTileCenter();
    }
};

class BrainProbe : public SimpleBrain
{
  public:
    BrainProbe()
      : SimpleBrain(m_tx, m_ty, m_ox, m_oy, m_anim, m_walk, m_vel), m_tx(3), m_ty(3), m_ox(0), m_oy(0),
        m_anim(0), m_walk(false), m_vel(0) {}
    bool near(fixed_t offX, fixed_t offY, fixed_t velocity)
    {
      m_ox = offX;
      m_oy = offY;
      m_vel = velocity;
      return nearTileCenter();
    }
  private:
    int m_tx, m_ty;
    fixed_t m_ox, m_oy;
    int m_anim;
    bool m_walk;
    fixed_t m_vel;
};

template <class Probe>
void checkNearTileCenter(Probe & p)
{
  const fixed_t v = 18022;
  EXPECT_TRUE(p.near(0, 0, v));
  EXPECT_TRUE(p.near(v, 0, v));        // exactly one step away counts
  EXPECT_TRUE(p.near(-v, 0, v));
  EXPECT_TRUE(p.near(0, v, v));
  EXPECT_TRUE(p.near(0, -v, v));
  EXPECT_TRUE(p.near(-v, v, v));
  EXPECT_FALSE(p.near(v + 1, 0, v));   // a hair further does not
  EXPECT_FALSE(p.near(-v - 1, 0, v));
  EXPECT_FALSE(p.near(0, v + 1, v));
  EXPECT_FALSE(p.near(0, -v - 1, v));
  EXPECT_FALSE(p.near(v, v + 1, v));   // both axes must be near
  EXPECT_FALSE(p.near(40000, -40000, v));
}

TEST_F(FixedMathTest, PlayerNearTileCenterComparesAbsoluteOffsetsWithVelocity)
{
  NearProbe p;
  checkNearTileCenter(p);
}

TEST_F(FixedMathTest, SimpleBrainNearTileCenterComparesAbsoluteOffsetsWithVelocity)
{
  BrainProbe p;
  checkNearTileCenter(p);
}

} // namespace
} // namespace bitriot_test
