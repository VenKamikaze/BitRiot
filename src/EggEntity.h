// EggEntity.h
#ifndef _EGGENTITYCLASS
#define _EGGENTITYCLASS

// an egg entity can be one of several numbered types
// all of which work the same way, but are rendered slightly different
// therefore, they all use this class, but get different renderers depending
// on their TYPE. So type is not a static variable for this class
#include "GameEntity.h"

class EggEntity : public GameEntity
{
  public:
    EggEntity(int uniqueID, int playerNumber, EntityType eggType, EntityType hatchType, int atX, int atY, unsigned int flags);
    ~EggEntity();

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();



  protected:
    void hashExtraState(StateHasher& h) const override;

  private:
    EntityType m_hatchling;
    int m_animCounter, m_hatchCounter;

    unsigned int m_hatchFlags;

    void hatch();
};

#endif
