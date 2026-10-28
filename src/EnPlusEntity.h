// energy plus powerup type
#ifndef _ENPLUSENTITY
#define _ENPLUSENTITY

#include "GameEntity.h"

class EnPlusEntity : public GameEntity
{
  public:
    EnPlusEntity(int uniqueID, int playerNumber, int atX, int atY);
    virtual ~EnPlusEntity() {}

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();

  protected:
    virtual void hashExtraState(StateHasher& h) const;

  private:
    int m_decayCounter;
    int m_initialDecayCount;

};

#endif
