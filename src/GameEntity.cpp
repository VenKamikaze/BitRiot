// default behaviour for all game entities

#include "GameEntity.h"
#include "GameSettings.h"

int GameEntity::getTeamNumber() const
{
  return GameSettings::getInstance()->teamNumberForPlayer(m_playerNumber);
}

bool GameEntity::isTeammatePlayer(int otherPlayerNumber) const
{
  return getTeamNumber() == GameSettings::getInstance()->teamNumberForPlayer(otherPlayerNumber);
}

GameEntity::GameEntity()
{
  // standard behaviour for most entities
  m_staticFlag = false;
  m_solidFlag = false;
  m_walkThroughBlocks = false;

  m_type = BLOCK;
  m_groupType = BLOCK_GROUP;

  m_ID = 0;
  m_playerNumber = 0;
  m_health = 0;
  m_velocity = 0;

  m_tileX = m_tileY = 0;
  m_offsetX = m_offsetY = 0;

  m_currentAnimState = EntityRenderer::IDLE;
  m_currentAnimFrame = 0;

}

void GameEntity::hashState(StateHasher& h) const
{
  h.i32(m_type);
  h.i32(m_playerNumber);
  h.i32(m_tileX);
  h.i32(m_tileY);
  h.i32(m_offsetX);   // raw Q16.16 bits
  h.i32(m_offsetY);
  h.i32(m_velocity);
  h.i32(m_health);
  h.i32(m_currentAnimState); // sim state: it drives movement (SimpleBrain, bots)
  h.flag(m_walkThroughBlocks);
  hashExtraState(h);
}

void GameEntity::hashExtraState(StateHasher&) const
{
  // Blocks, explosions, energy drops: nothing beyond the common fields.
}

void GameEntity::moveInDirection(DIRECTION d)
{
  Map * staticMap = Map::getInstance();

  //unsused
  //EntityRenderer * er = getEntityRenderer();


  fixed_t thisMoveVelocity = m_velocity;

  switch (d)
  {
    case UP:
      // check for collision with block moving to
      if (staticMap->staticTileAt(m_tileX, m_tileY - 1) == Map::WALL
          || (staticMap->staticTileAt(m_tileX, m_tileY - 1) == Map::BLOCK && !m_walkThroughBlocks))
      {
        // limit max velocity for this move
        if (m_offsetY - m_velocity < 0)
        {
          thisMoveVelocity = m_offsetY;
        }
      }

      // now try and move up in Y
      // if offsetX is not 0.0, move towards center
      if (m_offsetX < 0)
      {
        // move +ve in x direction
        if (m_offsetX + thisMoveVelocity <= 0)
        {
          // need to move entire move in x direction
          m_offsetX += thisMoveVelocity;
        }
        else
        {
          // only move part of move in x direction
          fixed_t remainingMove = m_offsetX + thisMoveVelocity;
          m_offsetX = 0;
          m_offsetY -= remainingMove;
        }
      }
      else
      {
        if (m_offsetX > 0)
        {
          // move +ve in x direction
          if (m_offsetX - thisMoveVelocity >= 0)
          {
            // need to move entire move in x direction
            m_offsetX -= thisMoveVelocity;
          }
          else
          {
            // only move part of move in x direction
            fixed_t remainingMove = thisMoveVelocity - m_offsetX;
            m_offsetX = 0;
            m_offsetY -= remainingMove;
          }

        }
        else
        {
          // centered in offset, move only in Y
          m_offsetY -= thisMoveVelocity;
        }
      }

      // update animation
      if (m_currentAnimState != EntityRenderer::MOVE_UP)
      {
        // if moving up animation exists, change to it
        m_currentAnimState = EntityRenderer::MOVE_UP;
        m_currentAnimFrame = 0;
      }

      break;
    case DOWN:
      if (staticMap->staticTileAt(m_tileX, m_tileY + 1) == Map::WALL
          || (staticMap->staticTileAt(m_tileX, m_tileY + 1) == Map::BLOCK && !m_walkThroughBlocks))
      {
        // limit max velocity for this move
        if (m_offsetY + m_velocity > 0)
        {
          thisMoveVelocity = -m_offsetY;
        }
      }

      // now try and move down in Y
      // if offsetX is not 0.0, move towards center
      if (m_offsetX < 0)
      {
        // move +ve in x direction
        if (m_offsetX + thisMoveVelocity <= 0)
        {
          // need to move entire move in x direction
          m_offsetX += thisMoveVelocity;
        }
        else
        {
          // only move part of move in x direction
          fixed_t remainingMove = m_offsetX + thisMoveVelocity;
          m_offsetX = 0;
          m_offsetY += remainingMove;
        }
      }
      else
      {
        if (m_offsetX > 0)
        {
          // move +ve in x direction
          if (m_offsetX - thisMoveVelocity >= 0)
          {
            // need to move entire move in x direction
            m_offsetX -= thisMoveVelocity;
          }
          else
          {
            // only move part of move in x direction
            fixed_t remainingMove = thisMoveVelocity - m_offsetX;
            m_offsetX = 0;
            m_offsetY += remainingMove;
          }

        }
        else
        {
          // centered in offset, move only in Y
          m_offsetY += thisMoveVelocity;
        }
      }

      // update animation
      if (m_currentAnimState != EntityRenderer::MOVE_DOWN)
      {
        // if moving up animation exists, change to it
        m_currentAnimState = EntityRenderer::MOVE_DOWN;
        m_currentAnimFrame = 0;
      }

      break;
    case LEFT:
      if (staticMap->staticTileAt(m_tileX - 1, m_tileY) == Map::WALL
          || (staticMap->staticTileAt(m_tileX - 1, m_tileY) == Map::BLOCK && !m_walkThroughBlocks))
      {
        // limit max velocity for this move
        if (m_offsetX - m_velocity < 0)
        {
          thisMoveVelocity = m_offsetX;
        }
      }


      // now try and move left in X
      // if offsetY is not 0.0, move towards center
      if (m_offsetY < 0)
      {
        // move +ve in y direction
        if (m_offsetY + thisMoveVelocity <= 0)
        {
          // need to move entire move in y direction
          m_offsetY += thisMoveVelocity;
        }
        else
        {
          // only move part of move in y direction
          fixed_t remainingMove = m_offsetY + thisMoveVelocity;
          m_offsetY = 0;
          m_offsetX -= remainingMove;
        }
      }
      else
      {
        if (m_offsetY > 0)
        {
          // move +ve in y direction
          if (m_offsetY - thisMoveVelocity >= 0)
          {
            // need to move entire move in y direction
            m_offsetY -= thisMoveVelocity;
          }
          else
          {
            // only move part of move in y direction
            fixed_t remainingMove = thisMoveVelocity - m_offsetY;
            m_offsetY = 0;
            m_offsetX -= remainingMove;
          }

        }
        else
        {
          // centered in offset, move only in X
          m_offsetX -= thisMoveVelocity;
        }
      }

      // update animation
      if (m_currentAnimState != EntityRenderer::MOVE_LEFT)
      {
        // if moving up animation exists, change to it
        m_currentAnimState = EntityRenderer::MOVE_LEFT;
        m_currentAnimFrame = 0;
      }

      break;
    case RIGHT:
      if (staticMap->staticTileAt(m_tileX + 1, m_tileY) == Map::WALL
          || (staticMap->staticTileAt(m_tileX +  1, m_tileY) == Map::BLOCK && !m_walkThroughBlocks))
      {
        // limit max velocity for this move
        if (m_offsetX + m_velocity > 0)
        {
          thisMoveVelocity = -m_offsetX;
        }
      }

      if (m_offsetY < 0)
      {
        // move +ve in y direction
        if (m_offsetY + thisMoveVelocity <= 0)
        {
          // need to move entire move in y direction
          m_offsetY += thisMoveVelocity;
        }
        else
        {
          // only move part of move in y direction
          fixed_t remainingMove = m_offsetY + thisMoveVelocity;
          m_offsetY = 0;
          m_offsetX += remainingMove;
        }
      }
      else
      {
        if (m_offsetY > 0)
        {
          // move +ve in y direction
          if (m_offsetY - thisMoveVelocity >= 0)
          {
            // need to move entire move in y direction
            m_offsetY -= thisMoveVelocity;
          }
          else
          {
            // only move part of move in x direction
            fixed_t remainingMove = thisMoveVelocity - m_offsetY;
            m_offsetY = 0;
            m_offsetX += remainingMove;
          }

        }
        else
        {
          // centered in offset, move only in x
          m_offsetX += thisMoveVelocity;
        }
      }

      // update animation
      if (m_currentAnimState != EntityRenderer::MOVE_RIGHT)
      {
        m_currentAnimState = EntityRenderer::MOVE_RIGHT;
        m_currentAnimFrame = 0;
      }

      break;
  }

  // see if tile positions need updating
  EntityMessageQueue * emq = EntityMessageQueue::getInstance();

  if (m_offsetX < -FIXED_ONE)
  {
    m_offsetX += 2 * FIXED_ONE;
    m_tileX--;
    emq->postMoveMessage(m_ID, m_tileX + 1, m_tileY, m_tileX, m_tileY);
  }
  else
  {
    if (m_offsetX > FIXED_ONE)
    {
      m_offsetX -= 2 * FIXED_ONE;
      m_tileX++;
      emq->postMoveMessage(m_ID, m_tileX - 1, m_tileY, m_tileX, m_tileY);
    }
  }

  if (m_offsetY < -FIXED_ONE)
  {
    m_offsetY += 2 * FIXED_ONE;
    m_tileY--;
    emq->postMoveMessage(m_ID, m_tileX, m_tileY + 1, m_tileX, m_tileY);
  }
  else
  {
    if (m_offsetY > FIXED_ONE)
    {
      m_offsetY -= 2 * FIXED_ONE;
      m_tileY++;
      emq->postMoveMessage(m_ID, m_tileX, m_tileY - 1, m_tileX, m_tileY);
    }
  }

}




EntityRenderer * GameEntity::getEntityRenderer()
{
  EntityRendererFactory * erf = EntityRendererFactory::getInstance();
  return erf->getEntityRenderer(m_type);
}

bool GameEntity::render(SDL_Surface* dest)
{
  EntityRenderer * er = getEntityRenderer();

  int destX = pixelX();
  int destY = pixelY();

  // Render in the entity's alliance-team colour (teammates share a colour), not
  // its per-player slot colour. getTeamNumber() -> the same colour-table index.
  return er->render(dest, destX, destY, m_currentAnimState, m_currentAnimFrame, getTeamNumber());
}

void GameEntity::incAnimFrame()
{
  EntityRenderer * er = getEntityRenderer();

  m_currentAnimFrame++;

  if (m_currentAnimFrame >= er->getNumFrames(m_playerNumber))
  {
    m_currentAnimFrame = 0;
  }
}

void GameEntity::explode()
{
  EntityMessageQueue * emq = EntityMessageQueue::getInstance();
  emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY, 0);

  // create explosions at x +- 1 and y +- 1
  Map * staticMap = Map::getInstance();
  if (staticMap->staticTileAt(m_tileX - 1, m_tileY) != Map::WALL)
  {
    emq->postCreateMessage(EXPLOSION, 0, m_tileX - 1, m_tileY, 0);
  }
  if (staticMap->staticTileAt(m_tileX + 1, m_tileY) != Map::WALL)
  {
    emq->postCreateMessage(EXPLOSION, 0, m_tileX + 1, m_tileY, 0);
  }
  if (staticMap->staticTileAt(m_tileX, m_tileY - 1) != Map::WALL)
  {
    emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY - 1, 0);
  }
  if (staticMap->staticTileAt(m_tileX, m_tileY + 1) != Map::WALL)
  {
    emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY + 1, 0);
  }

  emq->postDestroyMessage(m_ID);
  //MickBaseSound::getInstance()->playSound("audio/boom.wav");
  GameSound::playSound(GameSound::BOOM);
}
