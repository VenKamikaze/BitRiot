
#include "EntityMessage.h"

EntityMessage::EntityMessage(int type, int sender, int typeParam, 
							 int teamParam, int toX, int toY, 
							 int fromX, int fromY, unsigned int EMflags,
							 float offX, float offY)
	: m_type(type), 
	m_senderID(sender), 
	m_typeToCreate(typeParam),
	m_teamToCreate(teamParam), 
	m_toX(toX), 
	m_toY(toY),
	m_fromX(fromX),
	m_fromY(fromY),
	m_entityMessageFlags(EMflags),
	m_offsetX(offX),
	m_offsetY(offY)
{}
