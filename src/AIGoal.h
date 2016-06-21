// AIGoal.h

// a goal for Player Character Bot AI to attain
#pragma once

#include "EntityEnum.h"

class AIGoal
{
public:
	AIGoal(int goalType, int targetAmount, int tileX, int tileY,
		EntityType targetType, int targetID)
		: m_goalType(goalType),
		m_targetAmount(targetAmount),
		m_tileX(tileX),
		m_tileY(tileY),
		m_targetType(targetType),
		m_targetID(targetID) {}

	~AIGoal() {}

	enum GOAL_TYPES { 
		GOAL_INCREASE_ENERGY,
		GOAL_SECURE_AREA,
		GOAL_CREATE_ENTITIES,
		GOAL_ATTACK_TEAM,
		GOAL_ATTACK_ENTITY,
		GOAL_GET_POWERUP
	};

	const int m_goalType;	
	const int m_targetAmount;
	const int m_tileX; // generic x and y amounts for different purposes
	const int m_tileY;
	const EntityType m_targetType;
	const int m_targetID;
};

