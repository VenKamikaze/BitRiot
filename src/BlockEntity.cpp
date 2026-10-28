// implementation of BlockEntity class

#include "BlockEntity.h"

BlockEntity::BlockEntity(int uniqueID, int playerNumber, int atX, int atY)
{
  // constructor
  m_ID = uniqueID;
  m_playerNumber = playerNumber;

  m_type = BLOCK;
  m_groupType = BLOCK_GROUP;
  m_solidFlag = true;
  m_staticFlag = true;

  m_tileX = atX;
  m_tileY = atY;
  m_mapGeneration = Map::getInstance()->generation();
  Map::getInstance()->setBlockAt(atX, atY, true);

  m_offsetX = 0;
  m_offsetY = 0;
  m_health = 1;
  m_velocity = 0;
  m_currentAnimState = EntityRenderer::IDLE;
  m_currentAnimFrame = 0;

  // play creation sound
  GameSound::playSound(GameSound::BLOCK_DROP);
}

BlockEntity::~BlockEntity()
{
  // destructor
  // When the map is resized between rounds, Map is rebuilt (new stride, no blocks)
  // before the old round's entities are destroyed. Writing then would corrupt the
  // new map, or fall outside it. The rebuilt map has no blocks, so skip.
  if (m_mapGeneration == Map::getInstance()->generation())
  {
    Map::getInstance()->setBlockAt(m_tileX, m_tileY, false);
  }
}

void BlockEntity::onCollision(EntityType typeCollidedWith, EntityGroupType groupTypeCollidedWith, int collidedWithPlayerNumber)
{

  if (typeCollidedWith == EXPLOSION)
  {
    EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
    return;
  }
  if (typeCollidedWith == WALL_EATER)
  {
    // wall eater destroys any blocks in contact
    // munch effect!
    EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
    GameSound::playSound(GameSound::MUNCHY);
    EntityMessageQueue::getInstance()->postCreateMessage(MUNCH_EFFECT, m_playerNumber, m_tileX, m_tileY, 0);
    return;
  }
}

void BlockEntity::update()
{
  return;
}
