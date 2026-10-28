// first stage of player homing rocket weapon

#ifndef _ROCKETSPINENTITY
#define _ROCKETSPINENTITY

#include "GameEntity.h"
#include "AIGameView.h"

class RocketSpinEntity : public GameEntity
{
  public:
    RocketSpinEntity(int uniqueID, int playerNumber, int atX, int atY);
    virtual ~RocketSpinEntity();

    // overridden virtual methods
    virtual void onCollision(EntityType typeCollidedWith,
                             EntityGroupType groupTypeCollidedWith,
                             int collidedWithPlayerNumber);

    virtual void update();

  protected:
    virtual void hashExtraState(StateHasher& h) const;

  private:
    int m_lifespan;
    void fireAt(const DIRECTION d, const int destX, const int destY);
    void die();

    bool enemyInTile(const int tileX, const int tileY) const;

};

#endif