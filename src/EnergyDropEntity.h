// An energy drop spawns from an EGG1
// the idea is to give energy back to the player
// more than the it costs the player to create it
// This leads to an "energy farming" tactic

#ifndef _ENERGYDROPENTITY
#define _ENERGYDROPENTITY

#include "GameEntity.h"

class EnergyDropEntity : public GameEntity
{
  public:
    EnergyDropEntity(int uniqueID, int playerNumber, int atX, int atY);
    virtual ~EnergyDropEntity();

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();

  private:


};

#endif