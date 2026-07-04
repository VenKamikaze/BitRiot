#include "SimpleMovingEntity.h"

SimpleMovingEntity::SimpleMovingEntity(int uniqueID, int playerNumber, int atX, int atY, float offX, float offY)
{

  m_initialAnimCount = DataReader::getInstance()->getIntFromFile("BASE_ANIM_COUNT", "data/entity.txt");
  m_initialDecayCount = DataReader::getInstance()->getIntFromFile("BASE_DECAY_COUNT", "data/entity.txt");

  m_ID = uniqueID;
  m_playerNumber = playerNumber;
  m_tileX = atX;
  m_tileY = atY;
  m_offsetX = offX;
  m_offsetY = offY;
  m_walkThroughBlocks = false;

  m_pBrain = new SimpleBrain(m_tileX, m_tileY, m_offsetX, m_offsetY,
                             m_currentAnimState, m_walkThroughBlocks, m_velocity);

  m_animCounter = m_initialAnimCount;
  m_decayCounter = m_initialDecayCount;

  m_currentAnimState = EntityRenderer::IDLE;
  m_currentAnimFrame = 0;

  m_velocityModifier = DataReader::getInstance()->getIntFromFile("BASE_VELOCITY_MODIFIER", "data/entity.txt"); // can be changed
  m_velocity = ((float)m_health + 2)/m_velocityModifier;
}

SimpleMovingEntity::~SimpleMovingEntity()
{
  delete m_pBrain;
  m_pBrain = 0;

}

void SimpleMovingEntity::onCollision(EntityType typeCollidedWith,
                                     EntityGroupType groupTypeCollidedWith,
                                     int collidedWithPlayerNumber)
{
  if ((collidedWithPlayerNumber != m_playerNumber && (
         (groupTypeCollidedWith == CHARACTER_GROUP) ||
         (groupTypeCollidedWith == MONSTER_GROUP) ||
         (groupTypeCollidedWith == PLAYER_SPAWN_GROUP) ||
         (groupTypeCollidedWith == ROBOT_GROUP)))
      || (typeCollidedWith == EXPLOSION))
  {
    // collided with hostile type
    m_health--;
    EntityMessageQueue::getInstance()->postCreateMessage(DAMAGE_EFFECT,
        m_playerNumber, m_tileX, m_tileY, 0, m_offsetX, m_offsetY);
    return;
  }
}

void SimpleMovingEntity::update()
{
  m_animCounter--;
  if (m_animCounter <= 0)
  {
    incAnimFrame();
    m_animCounter = m_initialAnimCount;
  }

  if (m_playerNumber != 0) // CPU team doesn't decay
  {
    m_decayCounter--;
    if (m_decayCounter <= 0)
    {
      // used to slowly reduce health
      m_health--;
      m_decayCounter = m_initialDecayCount;
    }
  }

  // check health for death
  if (m_health <= 0)
  {
    EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
    EntityMessageQueue::getInstance()->postCreateMessage(DAMAGE_EFFECT,
        m_playerNumber, m_tileX, m_tileY, 0, m_offsetX, m_offsetY);
    return;
  }

  m_velocity = ((float)m_health + 2)/m_velocityModifier;

  // do movement AI
  m_pBrain->setDirectionToMove();

  if (m_currentAnimState != SimpleBrain::IDLE)
  {
    moveInDirection(static_cast<DIRECTION>(m_currentAnimState - 1));
  }


}

