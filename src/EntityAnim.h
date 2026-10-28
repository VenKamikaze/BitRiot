// EntityAnim.h
//
// The animation facts the simulation needs, with no bitmaps behind them.
//
// An entity's animation state (which row of its sprite sheet: idle, moving up,
// ...) is simulation state: it is hashed, and movement code reads it. Its frame
// number (which column) is render bookkeeping that the simulation advances once
// per tick, wrapping at the number of frames the entity's sprite sheet has. That
// count used to be asked of EntityRenderer, i.e. of the loaded bitmap, which tied
// the simulation to SDL surfaces. It is a table here instead; EntityRenderer
// (render side) still derives its own count from the bitmap's width, and
// AnimFrameTableTest keeps the two in agreement for every shipped bitmap.
//
// A sprite sheet is TILE_WIDTH pixels per frame and TILE_HEIGHT per state, so a
// sheet that is 128 pixels wide has 4 frames. If you change a bitmap's width in
// assets/bitmaps/, change its row here.

#ifndef ENTITYANIM_H_
#define ENTITYANIM_H_

#include "EntityEnum.h"

struct EntityAnim
{
  // Rows of a sprite sheet. The numbering is part of the state hash.
  enum ANIM_STATE
  { IDLE = 0, MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT };

  static const int MAX_ANIM_STATES = 5;

  // Frames in the sprite sheet of an entity of this type (columns), at least 1.
  // The same for every team colour and for the male and female player sheets.
  // Returns 1 for a type outside the enum.
  static int frameCount(EntityType type);
};

#endif /* ENTITYANIM_H_ */
