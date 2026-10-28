// EntityMessageQueue.h
// singleton class that entities post messages to,
// processed by EntityManager class

// messages returned from getNextMessage() must be deleted by the calling function

#ifndef _ENTITYMESSAGEQUEUE
#define _ENTITYMESSAGEQUEUE

#include <deque>
#include "EntityMessage.h"

class EntityMessageQueue
{
  public:
    static EntityMessageQueue * getInstance();
    ~EntityMessageQueue();

    void postDestroyMessage(int sender);
    void postCreateMessage(int typeParam, int teamParam, int atX, int atY,
                           unsigned int flags, fixed_t offsetX = 0, fixed_t offsetY = 0,
                           int targetX = 0, int targetY = 0);
    void postMoveMessage(int sender, int fromX, int fromY, int toX, int toY);

    EntityMessage * getMessageAtFront(); // messages returned from
    bool deleteMessageAtFront(); // pops and deletes message at front of queue

    // Deletes every pending message. A message only means something to the world
    // it was posted in, so a new world must not inherit the previous one's.
    void clear();

    inline bool empty()
    {
      return m_msgQueue.empty();  // checks if the message queue is empty
    }

    inline size_t size() const
    {
      return m_msgQueue.size();
    }

    // For the state hash: the pending messages, front (next to be processed)
    // first. A runFrame() posts its collision results after it drains the queue,
    // so every tick ends with messages still waiting; they are part of the state.
    void hashState(StateHasher& h) const;


  private:
    EntityMessageQueue() {}

    // FIFO of EntityMessage object pointers (a deque rather than a std::queue so
    // the state hash can walk it; it is only ever pushed at the back and popped
    // at the front).
    std::deque<EntityMessage *> m_msgQueue;

};

#endif
