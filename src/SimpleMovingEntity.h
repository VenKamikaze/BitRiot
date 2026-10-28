// shared base class for concrete class entities that share
// a lot of functionality

#ifndef _SIMPLEMOVINGENTITY
#define _SIMPLEMOVINGENTITY

#include "GameEntity.h"
#include "SimpleBrain.h"

class SimpleMovingEntity : public GameEntity
{
  public:
    SimpleMovingEntity(int uniqueID, int playerNumber, int atX, int atY,
                       fixed_t offsetX = 0, fixed_t offsetY = 0);
    virtual ~SimpleMovingEntity();

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();

  protected:
    virtual void hashExtraState(StateHasher& h) const;

    SimpleBrain * m_pBrain;

    int m_velocityModifier;

    int m_animCounter;
    int m_initialAnimCount;

    int m_decayCounter;
    int m_initialDecayCount;

};

#endif