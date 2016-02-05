// EntityMessageQueue.h
// singleton class that entities post messages to,
// processed by EntityManager class

// messages returned from getNextMessage() must be deleted by the calling function

#ifndef _ENTITYMESSAGEQUEUE
#define _ENTITYMESSAGEQUEUE

//#include "DSound.h" // put DSound here so everything that calls the EntityMessageQueue can play sounds

#include <queue>
#include "EntityMessage.h"

class EntityMessageQueue
{
public:
	static EntityMessageQueue * getInstance();
	~EntityMessageQueue();

	void postDestroyMessage(int sender);
	void postCreateMessage(int typeParam, int teamParam, int atX, int atY, 
		unsigned int flags, float offsetX = 0.0f, float offsetY = 0.0f);
	void postMoveMessage(int sender, int fromX, int fromY, int toX, int toY);

	EntityMessage * getMessageAtFront(); // messages returned from
	bool deleteMessageAtFront(); // pops and deletes message at front of queue

	inline bool empty() { return m_msgQueue.empty(); } // checks if the message queue is empty


private:
	EntityMessageQueue() {}

	std::queue<EntityMessage *> m_msgQueue; // the actual queue of EntityMessage object pointers

};

#endif
