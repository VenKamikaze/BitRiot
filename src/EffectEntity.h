// Effect Entity class
// purely a visual effect that has no other interation on the game

#ifndef _EFFECTENTITY
#define _EFFECTENTITY

#include "GameEntity.h"

class EffectEntity : public GameEntity
{
  public:
    EffectEntity(int uniqueID, EntityType effectType, int atX, int atY, fixed_t offsetX = 0, fixed_t offsetY = 0);
    ~EffectEntity() {}

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber) {} // does nothing

    virtual void update();

  private:
    int m_animCounter, m_origAnimCounter;
};

#endif

