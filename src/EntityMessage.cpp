
#include "EntityMessage.h"

EntityMessage::EntityMessage(int type, int sender, int typeParam,
                             int teamParam, int toX, int toY,
                             int fromX, int fromY, unsigned int EMflags,
                             fixed_t offX, fixed_t offY, int targetX, int targetY)
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
    m_offsetY(offY),
    m_targetX(targetX),
    m_targetY(targetY)
{}

void EntityMessage::hashState(StateHasher& h) const
{
  h.i32(m_type, "message.type");
  h.ref(m_senderID, "message.sender");
  h.i32(m_typeToCreate, "message.typeToCreate");
  h.i32(m_teamToCreate, "message.teamToCreate");
  h.i32(m_toX, "message.toX");
  h.i32(m_toY, "message.toY");
  h.i32(m_fromX, "message.fromX");
  h.i32(m_fromY, "message.fromY");
  h.i32((int32_t) m_entityMessageFlags, "message.flags");
  h.i32(m_offsetX, "message.offsetX");
  h.i32(m_offsetY, "message.offsetY");
  h.i32(m_targetX, "message.targetX");
  h.i32(m_targetY, "message.targetY");
}
