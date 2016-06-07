// A list of AI Instructions is used to attain an AI Goal
// by the player character bot AI
#pragma once

#include "EntityEnum.h"

class AIInstruction
{
public:
	AIInstruction(int type, int targetAmount, EntityType targetType, int tileX, int tileY)
		: m_type(type),
		  m_targetAmount(targetAmount),
		  m_targetType(targetType),
		  m_targetTileX(tileX),
		  m_targetTileY(tileY)
	{}
	~AIInstruction() {}

	enum INSTRUCTION_TYPES
	{
		GO_TO,
		MOVE_FROM_LOCATION,
		CREATE_ENTITY,
		WAIT
	};

	const int m_type; // type of instruction 
	const int m_targetAmount; // "distance" for MOVE_FROM, "number of" for CREATE and "duration" for WAIT, unused by GO_TO
	const EntityType m_targetType; // only used by CREATE_ENTITY
	const int m_targetTileX, m_targetTileY; // "target" for GOTO, "origin" for MOVE_FROM, unused otherwise (use GO_TO before CREATE_ENTITY for positioning)

};