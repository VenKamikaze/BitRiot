// EntityMessage.h
// messages created by GameEntities and sent on to EntityMessageQueue
#ifndef _ENTITYMESSAGECLASS
#define _ENTITYMESSAGECLASS


class EntityMessage
{
public:
	enum MessageType { DESTROY_ENTITY, CREATE_ENTITY, MOVE_ENTITY };

	EntityMessage(int mtype, int sender, 
		int typeParam = 0, int teamParam = 0, 
		int toX = 1, int toY = 1,
		int fromX = 1, int fromY = 1, unsigned int EMflags = 0,
		float offsetX = 0.0f, float offsetY = 0.0f);
	~EntityMessage() {}


	int getType() { return m_type; }
	int getSender() { return m_senderID; }
	int getTypeToCreate() { return m_typeToCreate; }
	int getTeamToCreate() { return m_teamToCreate; }
	int getFromX() { return m_fromX; }
	int getFromY() { return m_fromY; }
	int getToX() { return m_toX; }
	int getToY() { return m_toY; }
	float getOffsetX() { return m_offsetX; }
	float getOffsetY() { return m_offsetY; }
	unsigned int getFlags() { return m_entityMessageFlags; }

private:
	// generic message data
	int m_type; // type of message
	int m_senderID; // unique ID of sender entity

	// CREATE_ENTITY message data
	int m_typeToCreate; // entity type to create
	int m_teamToCreate; // team to assign created entity to
	// CREATE_ENTITY & MOVE_ENTITY data
	int m_toX, m_toY; // create_at or move_to coordinates
	int m_fromX, m_fromY; // move_from coordinates
	

	unsigned int m_entityMessageFlags; // bit flipped flags for extended behaviour
	float m_offsetX, m_offsetY;

};


#endif
