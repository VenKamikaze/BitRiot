// implementation of singleton EntityMessageQueue

#include "EntityMessageQueue.h"

#ifndef NULL
#define NULL 0
#endif


EntityMessageQueue * EntityMessageQueue::getInstance()
{
	static EntityMessageQueue instance;
	return &instance;
}

EntityMessageQueue::~EntityMessageQueue()
{
	// delete any messages still in queue
	while (!m_msgQueue.empty())
	{
		EntityMessage * em = m_msgQueue.front();
		if (em != 0)
			delete em;
		m_msgQueue.pop();
	}

}

void EntityMessageQueue::postDestroyMessage(int sender)
{
	EntityMessage * msg = new EntityMessage(EntityMessage::DESTROY_ENTITY, sender);

	m_msgQueue.push(msg);
}

void EntityMessageQueue::postCreateMessage(int typeParam, int teamParam, int atX, int atY, 
										   unsigned int flags, float offsetX, float offsetY)
{
	EntityMessage * msg = new EntityMessage(EntityMessage::CREATE_ENTITY, 0, typeParam, 
		teamParam, atX, atY, 1, 1, flags, offsetX, offsetY);

	m_msgQueue.push(msg);
}

void EntityMessageQueue::postMoveMessage(int sender, int fromX, int fromY, int toX, int toY)
{
	EntityMessage * msg = new EntityMessage(EntityMessage::MOVE_ENTITY,
		sender, 0, 0, toX, toY, fromX, fromY);

	m_msgQueue.push(msg);
}

EntityMessage * EntityMessageQueue::getMessageAtFront()
{
	if (m_msgQueue.empty())
		return NULL;
	else
		return m_msgQueue.front();

}

bool EntityMessageQueue::deleteMessageAtFront()
{
	if (m_msgQueue.empty())
		return false;
	else
	{
		EntityMessage * msg = m_msgQueue.front();
		m_msgQueue.pop();
		delete msg;
		return true;
	}
}
