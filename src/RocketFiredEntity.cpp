#include "RocketFiredEntity.h"

RocketFiredEntity::RocketFiredEntity(int uniqueID, int playerNumber, int atX, int atY,
                                     unsigned int flags, int toX, int toY)
{
  // constructor
  m_ID = uniqueID;
  m_playerNumber = playerNumber;

  m_type = ROCKET_FIRED;
  m_groupType = WEAPON_GROUP;

  m_staticFlag = false;

  m_tileX = atX;
  m_tileY = atY;
  m_offsetX = 0;
  m_offsetY = 0;
  m_health = 10;

  // Rounded UP, not to nearest: 10/25 is 0.4 = 26214.4 in Q16.16, and 26214 is a
  // hair short of it. With 26214 the rocket's offset reaches -0.4 one unit outside
  // "within one step of the tile middle" and the rocket explodes a tick later than
  // it did with floats (where noise happened to put it inside). 26215 keeps the
  // rocket's timing identical to the float code, wall and block stops included.
  m_velocity = fixedFromRatioCeil(m_health, DataReader::getInstance()->getIntFromFile("ROCKET_VELOCITY", "data/entity.txt"));

  m_dir = UP;
  m_currentAnimState = EntityRenderer::IDLE;
  m_currentAnimFrame = 0;

  // set direction from flags
  if (flags & GameEntity::UP_FLAG)
  {
    m_dir = UP;
    m_currentAnimState = EntityRenderer::MOVE_UP;
  }
  else if (flags & GameEntity::DOWN_FLAG)
  {
    m_dir = DOWN;
    m_currentAnimState = EntityRenderer::MOVE_DOWN;
  }
  else if (flags & GameEntity::LEFT_FLAG)
  {
    m_dir = LEFT;
    m_currentAnimState = EntityRenderer::MOVE_LEFT;
  }
  else if (flags & GameEntity::RIGHT_FLAG)
  {
    m_dir = RIGHT;
    m_currentAnimState = EntityRenderer::MOVE_RIGHT;
  }
  // set target tile
  m_toX = toX;
  m_toY = toY;

  GameSound::playSound(GameSound::ROCKET_FIRED);
}

RocketFiredEntity::~RocketFiredEntity()
{}

void RocketFiredEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int collidedWithPlayerNumber)
{
  if ((!isTeammatePlayer(collidedWithPlayerNumber) && (
         (groupTypeCollidedWith == CHARACTER_GROUP) ||
         (groupTypeCollidedWith == MONSTER_GROUP) ||
         (groupTypeCollidedWith == PLAYER_SPAWN_GROUP) ||
         (groupTypeCollidedWith == ROBOT_GROUP)))
      || (typeCollidedWith == EXPLOSION))
  {
    explode();
  }
}

void RocketFiredEntity::update()
{
  // move in direction
  moveInDirection(m_dir);

  // if in destination, explode
  if ((m_tileX == m_toX) && (m_tileY == m_toY) && (fixedAbs(m_offsetX) <= m_velocity) && (fixedAbs(m_offsetY) <= m_velocity))
  {
    explode();
  }

  // if in center of tile, and next tile is a block, explode
  if ((fixedAbs(m_offsetX) <= m_velocity) && (fixedAbs(m_offsetY) <= m_velocity))
  {
    // get next tile
    Map * map = Map::getInstance();
    Map::StaticTile nextTile = Map::EMPTY;
    switch (m_dir)
    {
      case UP:
        nextTile = map->staticTileAt(m_tileX, m_tileY - 1);
        break;
      case DOWN:
        nextTile = map->staticTileAt(m_tileX, m_tileY + 1);
        break;
      case LEFT:
        nextTile = map->staticTileAt(m_tileX - 1, m_tileY);
        break;
      case RIGHT:
        nextTile = map->staticTileAt(m_tileX + 1, m_tileY);
        break;
    }
    if ((nextTile == Map::WALL) || (nextTile == Map::BLOCK))
    {
      explode();
    }
  }
}
