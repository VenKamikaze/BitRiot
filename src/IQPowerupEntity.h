// IQ plus powerup type
#ifndef _IQPLUSENTITY
#define _IQPLUSENTITY

#include "GameEntity.h"

class IQPowerupEntity : public GameEntity
{
  public:
    IQPowerupEntity(int uniqueID, int playerNumber, int atX, int atY);
    virtual ~IQPowerupEntity() {}

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();

  protected:
    void hashExtraState(StateHasher& h) const override;

  private:
    int m_decayCounter;
    int m_initialDecayCount;

};

#endif