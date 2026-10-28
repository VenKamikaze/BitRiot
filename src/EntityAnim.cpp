#include "EntityAnim.h"

namespace
{

// Indexed by EntityType. Widths in pixels / 32, from assets/bitmaps/:
//   block 32, egg1-5 64, en 32, iq 32, bomb 128, mine 64, rocketspin 256,
//   rocketfired 32, energydrop 128, explosion 256, player{N}{male,female} 128,
//   eggthief 128, walleater 256, drone 512, robot1 128, munch 32, damage 160.
const int FRAME_COUNTS[NUM_ENTITY_TYPES] =
{
  1,                // BLOCK
  2, 2, 2, 2, 2,    // EGG1 .. EGG5
  1, 1,             // ENPLUS_POWERUP, IQPLUS_POWERUP
  4,                // BOMB
  2,                // MINE
  8,                // ROCKET_SPIN
  1,                // ROCKET_FIRED
  4,                // ENERGY_DROP
  8,                // EXPLOSION
  4,                // PLAYER_CHARACTER
  4,                // EGG_THIEF
  8,                // WALL_EATER
  16,               // DRONE
  4,                // ROBOT1
  1,                // MUNCH_EFFECT
  5                 // DAMAGE_EFFECT
};

static_assert(sizeof(FRAME_COUNTS) / sizeof(FRAME_COUNTS[0]) == NUM_ENTITY_TYPES,
              "one frame count per EntityType");
static_assert(DAMAGE_EFFECT == NUM_ENTITY_TYPES - 1, "the table follows the EntityType enum order");

} // namespace

int EntityAnim::frameCount(EntityType type)
{
  if (type < 0 || type >= NUM_ENTITY_TYPES)
  {
    return 1;
  }
  return FRAME_COUNTS[type];
}
