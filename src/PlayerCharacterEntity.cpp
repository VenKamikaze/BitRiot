// implementation of player character entity class

#include "PlayerCharacterEntity.h"

PlayerCharacterEntity::PlayerCharacterEntity(int uniqueID, int teamNumber, int atX, int atY, unsigned int flags)
{
	// constructor
	m_ID = uniqueID;
	m_team = teamNumber;

	m_type = PLAYER_CHARACTER;
	m_groupType = CHARACTER_GROUP;

	m_tileX = atX;
	m_tileY = atY;
	m_offsetX = 0.0F;
	m_offsetY = 0.0F;
	m_health = DataReader::getInstance()->getIntFromFile("PLAYER_INITIAL_HEALTH", "data/player.txt");

	m_velocityModifier = DataReader::getInstance()->getIntFromFile("PLAYER_VELOCITY_MODIFIER", "data/player.txt");
	m_velocity = ((float)m_health + 10)/m_velocityModifier;

	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;
	m_animDelay = DataReader::getInstance()->getIntFromFile("PLAYER_ANIM_DELAY", "data/player.txt");
	m_animCounter = m_animDelay;

	m_IQ = 0;
	m_checkTimer = (rand()%2 > 0) ? 37 : 67;

	if ((flags & AI_CONTROLLED_BOT_FLAG) > 0)
	{
		m_controlledByAI = true;
		
		// pause to give human players a little time at beginning of game
		AIInstruction * ins = new AIInstruction(AIInstruction::WAIT, 23 + (rand()%42), getType(), 0, 0); 
		m_insList.push_front(ins);

		// set initial goals
		//AIGoal * goal1 = new AIGoal(AIGoal::GOAL_SECURE_AREA, targetAmount, tileX, tileY, targetType, targetID);
		//m_goalList.push_back(goal1);
		AIGoal * goal2 = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, (rand() % 3) + 2, -1, -1, EGG2, -1);
		m_goalList.push_back(goal2);

		//AIGoal * goal3 = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, 8, -1, -1, EGG1, -1);
		//m_goalList.push_back(goal3);

	} 
	else
		m_controlledByAI = false;
}

PlayerCharacterEntity::~PlayerCharacterEntity()
{
	// destructor

	clearGoalsAndInstructions();

}

void PlayerCharacterEntity::update()
{
	m_velocity = ((float)m_health + 10)/m_velocityModifier;

	if (m_controlledByAI == true)
	{
		// this is a bot character, here's the AI! :D

		// set check surroundings timer to one of two primes
		m_checkTimer--;
		if (m_checkTimer <= 0)
		{
			m_checkTimer = (rand()%2 > 0) ? 37 : 67;
			checkEnvironment();
		}

		if (m_goalList.empty() && m_insList.empty())
			determineGoals();

		if (m_insList.empty())
			determineInstructionsFromGoal();

		// add AI for important surroundings sensing whateverness

		processNextInstruction();
	}
}

void PlayerCharacterEntity::incAnimFrame()
{
	m_animCounter--;
	if (m_animCounter <= 0)
	{
		m_animCounter = m_animDelay;
		GameEntity::incAnimFrame();
	}

}

void PlayerCharacterEntity::onCollision(EntityType typeCollidedWith,
										EntityGroupType groupTypeCollidedWith,
										int teamCollidedWith)
{
	if ((teamCollidedWith != m_team && (
		(groupTypeCollidedWith == CHARACTER_GROUP) ||
		(groupTypeCollidedWith == MONSTER_GROUP) ||
		(groupTypeCollidedWith == PLAYER_SPAWN_GROUP) ||
		(groupTypeCollidedWith == ROBOT_GROUP)))
		|| (typeCollidedWith == EXPLOSION))
	{
		m_health--;
		EntityMessageQueue::getInstance()->postCreateMessage(DAMAGE_EFFECT, 
			m_team, m_tileX, m_tileY, 0, m_offsetX, m_offsetY);
	}

	if (typeCollidedWith == ENERGY_DROP)
	{
		// touched an energy drop
		// regain some health
		incHealth(DataReader::getInstance()->getIntFromFile("ENERGY_DROP_BONUS", "data/player.txt"));
		//M2S SOUND DSound::getInstance()->playSound(DSound::ENERGY_PICKUP);
		GameSound::playSound(GameSound::ENERGY_PICKUP);
	}

	if (typeCollidedWith == ENPLUS_POWERUP)
	{
		// touched energy POWERUP
		// regain more health
		incHealth(DataReader::getInstance()->getIntFromFile("ENERGY_POWERUP_BONUS", "data/player.txt"));
		//M2S SOUND DSound::getInstance()->playSound(DSound::ENERGY_PICKUP);
		GameSound::playSound(GameSound::ENERGY_PICKUP);
	}

	if (typeCollidedWith == IQPLUS_POWERUP)
	{
		// touched IQ POWERUP
		// increase IQ counter
		incIQ(DataReader::getInstance()->getIntFromFile("IQ_POWERUP_BONUS", "data/player.txt"));
		//M2S SOUND DSound::getInstance()->playSound(DSound::ENERGY_PICKUP);
		GameSound::playSound(GameSound::ENERGY_PICKUP);
	}

	if (m_health < 0)
	{
		m_health = 0;
		die();
	}

}

bool PlayerCharacterEntity::createEntity(EntityType type)
{
	// calling function should check there are no static entities in this entity's tile
	// called when player character tries to "lay an entity"

	// this could be done a lot more elegantly

	unsigned int flags = 0;
	
	// remove health
	switch (type)
	{
	case BLOCK:		
		m_health -= DataReader::getInstance()->getIntFromFile("BLOCK_COST", "data/player.txt");
		break;
	case BOMB:
		m_health -= DataReader::getInstance()->getIntFromFile("BOMB_COST", "data/player.txt");
		break;
	case MINE:
		m_health -= DataReader::getInstance()->getIntFromFile("MINE_COST", "data/player.txt");
		break;
	case ROCKET_SPIN:
		m_health -= DataReader::getInstance()->getIntFromFile("ROCKET_COST", "data/player.txt");
		break;
	case EGG1:
		m_health -= DataReader::getInstance()->getIntFromFile("EGG1_COST", "data/player.txt");
		break;
	case EGG2:
		m_health -= DataReader::getInstance()->getIntFromFile("EGG2_COST", "data/player.txt");
		break;
	case EGG3:
		m_health -= DataReader::getInstance()->getIntFromFile("EGG3_COST", "data/player.txt");
		break;
	case EGG4:
		m_health -= DataReader::getInstance()->getIntFromFile("EGG4_COST", "data/player.txt");
		break;
	case EGG5:
		m_health -= DataReader::getInstance()->getIntFromFile("EGG5_COST", "data/player.txt");
		flags |= FIRE_ROCKETS;
		break;
	default:
		// illegal type to lay specified
		return false;
	}
	EntityMessageQueue::getInstance()->postCreateMessage(type, m_team, m_tileX, m_tileY, flags);

	// check health for death
	if (m_health <= 0)
	{
		m_health = 0;
		die();		
	}
	return true;

}

void PlayerCharacterEntity::incHealth(int amount)
{
	m_health += amount;
	int MAX_HEALTH = DataReader::getInstance()->getIntFromFile("PLAYER_MAX_HEALTH", "data/player.txt");
	if (m_health > MAX_HEALTH)
		m_health = MAX_HEALTH;
}

void PlayerCharacterEntity::die()
{
	EntityMessageQueue::getInstance()->postDestroyMessage(m_ID);
	//M2S SOUND DSound::getInstance()->playSound(DSound::SPLAT);
	GameSound::playSound(GameSound::SPLAT);
}

void PlayerCharacterEntity::determineGoals()
{
	AIGameView * view = AIGameView::getInstance();

	// check surrounding area
	static const int RANGE = 4;
	bool goalFound = false;
	for (int x = m_tileX - RANGE; x <= m_tileX + RANGE; ++x)
	{
		if (x < 0 || x >= Map::MAP_WIDTH)
			continue;
		for (int y = m_tileY - RANGE; y <= m_tileY + RANGE; ++y)
		{
			if (y < 0 || y >= Map::MAP_HEIGHT)
				continue;

			if (x == m_tileX && y == m_tileY)
				continue;

			// should be optimised with an outward-spiralling search pattern,
			// and first powerup found will be approx. nearest 
			// (as much as the current *solution* is "nearest")
			
			vector<EntityInfo> info = view->getEntityInfoAtLocation(x, y);
			vector<EntityInfo>::iterator it;

			for (it = info.begin(); it != info.end(); ++it)
			{
				// is there a powerup?
				if (it->groupType == POWERUP_GROUP || it->type == ENERGY_DROP)
				{
					// add to goals if this is closest powerup
					list<AIGoal *>::iterator goalIt;
					for (goalIt = m_goalList.begin(); goalIt != m_goalList.end(); ++goalIt)
					{
						AIGoal * oldGoal = *goalIt;
						if (oldGoal->m_goalType == AIGoal::GOAL_GET_POWERUP)
						{
							int oldDistance, newDistance;
							oldDistance = abs( oldGoal->m_tileX - getTileX() ) 
								+ abs( oldGoal->m_tileY - getTileY() );
							newDistance = abs( x - getTileX() ) + abs( y - getTileY() );

							if (newDistance < oldDistance)
							{
								// replace old goal with closer one
								m_goalList.remove(oldGoal);
								delete oldGoal;

								m_goalList.push_front(new AIGoal(AIGoal::GOAL_GET_POWERUP, 0, x, y, it->type, it->id));

								goalFound = true;
								break;
							}
							// else ignore this powerup for now

						} // end if goal type == GET_POWERUP

					} // end for loop on goalList

					if (!goalFound)
                        m_goalList.push_front(new AIGoal(AIGoal::GOAL_GET_POWERUP, 0, x, y, it->type, it->id));
						return;
				}



			} // end iteration of entity info

		} // end for y
	} // end for x

	// other goals
	if (m_health < 45)
	{
		AIGoal * g1 = new AIGoal(AIGoal::GOAL_SECURE_AREA, 0, getTileX(), getTileY(), getType(), 0);
		m_goalList.push_back(g1);
	}
	if (m_health < 100)
	{
		AIGoal * g2 = new AIGoal(AIGoal::GOAL_INCREASE_ENERGY, 130, getTileX(), getTileY(), getType(), 0);
		m_goalList.push_back(g2);
		return;
	}
	if (m_health >= 100)
	{
		// create some entities
	
		// drone or robot
		if (rand()%2 > 0)
		{
			// tanks
			AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
				1 + rand()%3, getTileX(), getTileY(), EGG5, 0);
			m_goalList.push_back(goal);
		}
		else
		{
			// drones
			AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
				1 + rand()%4, getTileX(), getTileY(), EGG4, 0);
			m_goalList.push_back(goal);

		}
		if (rand()%2 > 0)
		{
			// wall eater or two
			AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
				(rand()%2) + 1, getTileX(), getTileY(), EGG3, 0);
			m_goalList.push_back(goal);
		}
		if (rand()%10 > 3)
		{
			// egg thief?
			AIGoal * goal = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES,
				(rand()%2) + 1, getTileX(), getTileY(), EGG2, 0);
			m_goalList.push_back(goal);
		}


	}
}

void PlayerCharacterEntity::determineInstructionsFromGoal()
{
	if (m_goalList.empty())
		return;

	AIGoal * currentGoal = m_goalList.front();
	m_goalList.pop_front();

	switch (currentGoal->m_goalType)
	{
	case AIGoal::GOAL_INCREASE_ENERGY:
		{
			// create 8 energy drops at once

			AIGoal * create = new AIGoal(AIGoal::GOAL_CREATE_ENTITIES, 2 + rand()%5,
				getTileX(), getTileY(), EGG1, 0);
			m_goalList.push_back(create);

			determineInstructionsFromGoal();

			AIInstruction * wait = new AIInstruction(AIInstruction::WAIT,
				33, getType(), 0, 0);
			m_insList.push_back(wait);



		}
		break;
	case AIGoal::GOAL_SECURE_AREA:
		{
			// use path flooding to certain depth and place blocks at end
			/*
			const int SECURE_DISTANCE = 3;

			int x1, x2, y1, y2;
			x1 = m_tileX - SECURE_DISTANCE;
			x2 = m_tileX + SECURE_DISTANCE;
			y1 = m_tileY - SECURE_DISTANCE;
			y2 = m_tileY + SECURE_DISTANCE;

			for (int counter = x1; counter <= x2; ++counter)
			{
				// at (x1, y1 + counter), (x1 + counter, y1)
				// (x2, y1 + counter), (x1 + counter, y2)
				for (int i = 0; i < 4; ++i)
				{
					int xCheck, yCheck;
					switch (i)
					{
					case 0:
						xCheck = x1;
						yCheck = y1 + counter;
						break;
					case 1:
						xCheck = x1 + counter;
						yCheck = y1;						
						break;
					case 2:
						xCheck = x2;
						yCheck = y1 + counter;
						break;
					case 3:
						xCheck = x1 + counter;
						yCheck = y2;
						break;
					}

					if (xCheck <= 0 || xCheck >= Map::MAP_WIDTH - 1)
						continue;
					if (yCheck <= 0 || yCheck >= Map::MAP_HEIGHT - 1)
						continue;

					// is location accessible
					coord next = getNextTileFromPathfinding(xCheck, yCheck);
					
					if (Map::getInstance()->staticTileAt(xCheck, yCheck) != Map::EMPTY)
						continue; // blocked

					if (next.first == m_tileX && next.second == m_tileY)
						continue; // can't get there

					// else place block there
					AIInstruction * goTo, * place;//, * returnJourney;

					goTo = new AIInstruction(AIInstruction::GO_TO,
						0, getType(), xCheck, yCheck);
					m_insList.push_back(goTo);

					place = new AIInstruction(AIInstruction::CREATE_ENTITY,
						1, BLOCK, xCheck, yCheck);
					m_insList.push_back(place);

					//returnJourney = new AIInstruction(AIInstruction::GO_TO,
					//	0, getType(), next.first, next.second);
					//m_insList.push_back(returnJourney);



				}
			

			}*/
		}
		break;
	case AIGoal::GOAL_CREATE_ENTITIES:
		{
            // this can be done by just adding instructions
			// to create the right kind and number of eggs

			// the instruction handing will move character as needed
			for (int i = 0; i < currentGoal->m_targetAmount; ++i)
			{
				// add pause to simulate human player decision/action delay
				// could remove for more difficult AI bots
				AIInstruction * waitIns =
					new AIInstruction(AIInstruction::WAIT, 5 + (rand()%11), getType(), -1, -1);
				m_insList.push_back(waitIns);


				AIInstruction * ins = 
					new AIInstruction(AIInstruction::CREATE_ENTITY, 1, currentGoal->m_targetType, getTileX(), getTileY());
				m_insList.push_back(ins);		


			}

		}
		break;
	case AIGoal::GOAL_ATTACK_TEAM:
		{


		}
		break;
	case AIGoal::GOAL_ATTACK_ENTITY:
		{


		}
		break;
	case AIGoal::GOAL_GET_POWERUP:
		{
			if (currentGoal->m_tileX == m_tileX && currentGoal->m_tileY == m_tileY)
				break; // we're already there

			// check powerup still exists
			vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(
				currentGoal->m_tileX, currentGoal->m_tileY);
			vector<EntityInfo>::iterator it;
			bool targetExists = false;
			for (it = info.begin(); it != info.end(); ++it)
			{
				if (it->id == currentGoal->m_targetID)
				{
					targetExists = true;
					break;
				}
			}
			if (!targetExists)
				break;

			// keep this goal for later REMOVE??
			// as we may need multiple block bombings
			//AIGoal * copy = new AIGoal(*currentGoal);
			//m_goalList.push_front(copy);

			// set "final" goto
			AIInstruction * target = new AIInstruction(AIInstruction::GO_TO, 0, currentGoal->m_targetType,
				currentGoal->m_tileX, currentGoal->m_tileY);
			m_insList.push_front(target);

			// go to powerup with demolition enabled
			coord next = getNextTileFromPathfinding(currentGoal->m_tileX, currentGoal->m_tileY, true);

			// seeing as we've already calculated it...
			AIInstruction * firstGoTo = new AIInstruction(AIInstruction::GO_TO,
				0, currentGoal->m_targetType, next.first, next.second);
			m_insList.push_front(firstGoTo);
			

		}
		break;
	}

	delete currentGoal;
}

void PlayerCharacterEntity::processNextInstruction()
{
	if (m_insList.empty())
		return;
	
	AIInstruction * ins = m_insList.front();
	m_insList.pop_front();

	switch (ins->m_type)
	{
	case AIInstruction::CREATE_ENTITY:
		{
			if (m_health < 15)
				break;
			// check if this tile already has a static entity
			bool staticAtLocation = this->staticEntityAtLocation(getTileX(), getTileY());

			if (staticAtLocation)
			{
				// there is already a static entity

				// if this is an egg type required, move to nearest "empty" tile
				if (   ins->m_targetType == EGG1
					|| ins->m_targetType == EGG2
					|| ins->m_targetType == EGG3
					|| ins->m_targetType == EGG4
					|| ins->m_targetType == EGG5 )
				{
					// find nearest empty tile and move there, replace instruction

					if (goToNearestEmptyTile(ins->m_targetTileX, ins->m_targetTileY))
					{
						// can go to an empty tile
						AIInstruction * copiedIns =
							new AIInstruction(*ins);

						// put CREATE instruction after GOTO at head of list
						AIInstruction * goToIns = m_insList.front();
						m_insList.pop_front();
						m_insList.push_front(copiedIns);
						m_insList.push_front(goToIns);
					} else {
						// failed to find a nearby empty tile

						// forget this instruction for now

					}
				} else {
					// is a weapon/block type
					// probably not very helpful to move location
					// so forget this instruction

				}


			} else {
				// there isn't a static entity

				// if this is an egg, is it a safe enough location?

				// is this a weapon? if so, check desired spot
				if (ins->m_targetType == BOMB ||
					ins->m_targetType == BLOCK ||
					ins->m_targetType == MINE ||
					ins->m_targetType == ROCKET_SPIN)
				{
					if (ins->m_targetType == BOMB ||
						ins->m_targetType == MINE)
					{
						if (!safeLocationToBomb(m_tileX, m_tileY))
							break;
					}

					if (ins->m_targetTileX == m_tileX
						&& ins->m_targetTileY == m_tileY)
						createEntity(ins->m_targetType);
				} else {
					// check this location is safe for an egg

					bool bombOrMineFound = false;

					for (int i = 0; i < 4; ++i)
					{
						int x, y;
						x = m_tileX;
						y = m_tileY;

						switch (i)
						{
						case 0:
							x++;
							break;
						case 1:
							x--;
							break;
						case 2:
							y++;
							break;
						case 3:
							y--;
							break;
						}

						vector<EntityInfo> eiVec = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
						vector<EntityInfo>::iterator it;

						for (it = eiVec.begin(); it != eiVec.end(); ++it)
						{
							// even our team's bombs and mines are dangerous to eggs
							if (it->type == BOMB || it->type == MINE)
							{
								bombOrMineFound = true;
								break;
							}

						}

						if (bombOrMineFound)
							break;

					} // end for 0..3

					if (!bombOrMineFound)
                        createEntity(ins->m_targetType);
					else
					{
						// move elsewhere
						AIInstruction * copy = new AIInstruction(*ins);
						m_insList.push_front(copy);

						goNumTilesAway(3);
					}
				}
			}


		}
		break;
	case AIInstruction::GO_TO:
		{
			// check for simple 0 or 1 tile movement			
			int xDist, yDist;
			xDist = m_tileX - ins->m_targetTileX;
			yDist = m_tileY - ins->m_targetTileY;

			bool moved = false;


			if (xDist == 0 && yDist == 0)
			{
				// in target tile, close enough to center?
				if (nearTileCenter())
				{
					// finished this instruction
					break;
				} else {
					// keep moving in current direction
					DIRECTION dir = static_cast<DIRECTION>(m_currentAnimState - 1);
					moveInDirection(dir);	
					moved = true;
				}
			}

			// only one tile to go?

			// check target tile isn't blocked
			if (Map::getInstance()->staticTileAt(ins->m_targetTileX, ins->m_targetTileY) != Map::EMPTY)
				break;

			if (xDist == -1 && yDist == 0)
			{
				moveInDirection(RIGHT);
				moved = true;
			} 
			else if (xDist == 1 && yDist == 0)
			{
				moveInDirection(LEFT);
				moved = true;
			}
			else if (xDist == 0 && yDist == 1)
			{
				moveInDirection(UP);
				moved = true;
			}
			else if (xDist == 0 && yDist == -1)
			{
				moveInDirection(DOWN);
				moved = true;
			}

			

			if (moved)
			{
				// copy instruction
				AIInstruction * copyIns = new AIInstruction(*ins);
				m_insList.push_front(copyIns);

				incAnimFrame();
				break;
			}
			

			// there is a multiple tile distance, use path finding

			// get next tile by pathfinding
			coord next = getNextTileFromPathfinding(ins->m_targetTileX, ins->m_targetTileY);
			// did pathfinding work?
			if (next.first == getTileX() && next.second == getTileY())
			{
				// did not work, or we're already where we want to be


			} 
			else
			{
				// copy instruction
				AIInstruction * copyIns = new AIInstruction(*ins);
				m_insList.push_front(copyIns);

                // add this tile as goto
				AIInstruction * newIns = new AIInstruction(AIInstruction::GO_TO,
					0, getType(), next.first, next.second);
				m_insList.push_front(newIns);
				processNextInstruction();
			}

		}
		break;
	case AIInstruction::MOVE_FROM_LOCATION:
		{
			goNumTilesAway(ins->m_targetAmount);
		}
		break;
	case AIInstruction::WAIT:
		{
			int waitAmount = ins->m_targetAmount - 1;
			if (waitAmount > 0)
			{
				AIInstruction * newIns = new AIInstruction(ins->m_type,
					waitAmount, ins->m_targetType, ins->m_targetTileX, ins->m_targetTileY);
				m_insList.push_front(newIns);
			}
			// else nothing more to do
		}
		break;
	}

	delete ins;
}

bool PlayerCharacterEntity::staticEntityAtLocation(int tileX, int tileY)
{
	bool staticAtLocation = false;

	AIGameView * view = AIGameView::getInstance();
	vector<EntityInfo> info = view->getEntityInfoAtLocation(tileX, tileY);

	vector<EntityInfo>::iterator it;
	for (it = info.begin(); it != info.end(); ++it)
	{

		staticAtLocation = it->staticFlag;

		if (staticAtLocation)
			break;
	}

	return staticAtLocation;
}

bool PlayerCharacterEntity::goToNearestEmptyTile(int fromX, int fromY)
{
	// set an immediate GO_TO instruction if a suitable empty tile is found
	set<coord> searched;
	list<coord> toSearch;

	toSearch.push_front(coord(fromX, fromY));

	while (!toSearch.empty())
	{
		// get head of toSearch
		coord currentLocation = toSearch.front();
		toSearch.pop_front();

		// is this an empty tile we want?
		if (!staticEntityAtLocation(currentLocation.first, currentLocation.second))
		{
			// it is, set GO_TO instruction and be happy
			AIInstruction * ins = new AIInstruction(AIInstruction::GO_TO,
				0, getType(), currentLocation.first, currentLocation.second);
			m_insList.push_front(ins);

			return true;
		} else {
			// there is a static entity here, add valid adjoining tiles to toSearch list

			searched.insert(currentLocation);

			Map * staticMap = Map::getInstance();

			for (int i = 0; i < 4; ++i)
			{
				int x, y;
				x = currentLocation.first;
				y = currentLocation.second;

				switch (i)
				{
				case 0:
					x++;
					break;
				case 1:
					x--;
					break;
				case 2:
					y++;
					break;
				case 3:
					y--;
					break;
				}

				if (staticMap->staticTileAt(x, y) == Map::EMPTY)
				{
					coord newLocation(x, y);
					// check searched set
					if (searched.find(newLocation) == searched.end())
						toSearch.push_back(newLocation);
				}

			} // end for 0..3

		} // end else static entry found

	} // end while toSearch not-empty

	return false;
}

bool PlayerCharacterEntity::goNumTilesAway(int numToMove)
{
	// set an immediate GO_TO instruction if a suitable tile is found
	set<coord> searched;
	list<pair<int,coord> > toSearch;

	toSearch.push_front(pair<int, coord>(0, coord(getTileX(), getTileY())));

	while (!toSearch.empty())
	{
		// get head of toSearch
		pair<int, coord> currentLocation = toSearch.front();
		toSearch.pop_front();

		// is this the tile we want?
		if (currentLocation.first >= numToMove)
		{
			// it is, set GO_TO instruction and be happy
			AIInstruction * ins = new AIInstruction(AIInstruction::GO_TO,
				0, getType(), currentLocation.second.first, currentLocation.second.second);
			m_insList.push_front(ins);

			return true;
		} else {
			// add valid adjoining tiles to toSearch list

			searched.insert(currentLocation.second);

			Map * staticMap = Map::getInstance();
			AIGameView * view = AIGameView::getInstance();

			for (int i = 0; i < 4; ++i)
			{
				int x, y;
				x = currentLocation.second.first;
				y = currentLocation.second.second;

				switch (i)
				{
				case 0:
					x++;
					break;
				case 1:
					x--;
					break;
				case 2:
					y++;
					break;
				case 3:
					y--;
					break;
				}

				if (staticMap->staticTileAt(x, y) == Map::EMPTY)
				{
					// check for hostile mines
					bool badMineFound = false;

					vector<EntityInfo> info = view->getEntityInfoAtLocation(x, y);
					vector<EntityInfo>::iterator it;
					for (it = info.begin(); it != info.end(); ++it)
					{
						if (it->type == MINE && it->team != getTeam())
						{
							badMineFound = true;
							break;
						}
					}

					if (badMineFound)
						continue;

					coord newCoord(x, y);
					pair<int, coord> newLocation(currentLocation.first + 1, newCoord);
					// check searched set
					if (searched.find(newCoord) == searched.end())
						toSearch.push_back(newLocation);
				}

			} // end for 0..3

		} // end else static entry found

	} // end while toSearch not-empty

	return false;
}

bool PlayerCharacterEntity::nearTileCenter()
{
	float absOffsetX, absOffsetY;
	absOffsetX = (m_offsetX < 0) ? -m_offsetX : m_offsetX;
	absOffsetY = (m_offsetY < 0) ? -m_offsetY : m_offsetY;

	if ((absOffsetX <= m_velocity) && (absOffsetY <= m_velocity))
		return true;
	else
		return false;
}

struct Node
{
	coord location;
	int heuristic;
	int cost;

	Node * parent;
};

coord PlayerCharacterEntity::getNextTileFromPathfinding(int targetTileX, int targetTileY, 
														bool allowBlockBombing)
{
	// pretty standard A* pathfinding

	map<coord, Node *> closed; // previously explored nodes
	map<float, Node *> open; // nodes to be explored, with approx heuristic + cost mapping

	coord result(m_tileX, m_tileY);

	// set initial node
	Node * initial = new Node;
	initial->location.first = m_tileX;
	initial->location.second = m_tileY;
	initial->heuristic = abs(m_tileX - targetTileX) + abs(m_tileY - targetTileY);
	initial->cost = 0;
	initial->parent = NULL;

	// add to open
	open.insert(pair<float, Node *>((float)initial->heuristic, initial));

	while (!open.empty())
	{
		// get best node of open list
		Node * current = open.begin()->second;
		open.erase(open.begin()); 

		// is this the goal node?
		if (current->location.first == targetTileX && current->location.second == targetTileY)
		{
			// get the result we want

			// may be blocks on solution path need bombing
			int lastPreBlockX = -1;
			int lastPreBlockY = -1;
			bool blockEncountered = false;

			Node * walker = current;
							
			while (walker->parent != initial)
			{
				if (Map::getInstance()->staticTileAt(walker->location.first, walker->location.second)
				== Map::BLOCK)
					blockEncountered = true;

				if (walker->parent == NULL)
					break;
				walker = (walker->parent);

				if (blockEncountered)
				{
					blockEncountered = false;
					lastPreBlockX = walker->location.first;
					lastPreBlockY = walker->location.second;
				}
			}

			if (lastPreBlockX != -1)
			{
				// we've got some bombing to do
				
				AIInstruction * goToIns, * bombIns, * duckAndCoverIns, * waitIns;

				goToIns = new AIInstruction(
					AIInstruction::GO_TO, 0, getType(), lastPreBlockX, lastPreBlockY);
				bombIns = new AIInstruction(
					AIInstruction::CREATE_ENTITY, 1, BOMB,  lastPreBlockX, lastPreBlockY);
				duckAndCoverIns = new AIInstruction(
					AIInstruction::MOVE_FROM_LOCATION, 2, getType(), lastPreBlockX, lastPreBlockY);
				waitIns = new AIInstruction(
					AIInstruction::WAIT, 45 /* base decay / 2 */, getType(), lastPreBlockX, lastPreBlockY);

				m_insList.push_front(waitIns);
				m_insList.push_front(duckAndCoverIns);
				m_insList.push_front(bombIns);
				m_insList.push_front(goToIns);

			}
			result.first = walker->location.first;
			result.second = walker->location.second;
			break;
		}
		else
		{
			// not the goal node

			// generate possible child nodes
			for (int i = 0; i < 4; ++i)
			{
				int x, y;
				x = current->location.first;
				y = current->location.second;
				switch (i)
				{
				case 0:
					x++;
					break;
				case 1:
					x--;
					break;
				case 2:
					y++;
					break;
				case 3:
					y--;
					break;
				}

				// is this a valid location to move to? if not, skip this location
				if ((Map::getInstance()->staticTileAt(x, y) != Map::EMPTY && !allowBlockBombing)
					|| (Map::getInstance()->staticTileAt(x, y) == Map::WALL))
					continue;

				// ok to move here, set node data
				Node * child = new Node;
				child->cost = current->cost + 1;
				if (Map::getInstance()->staticTileAt(x, y) == Map::BLOCK)
					child->cost += 5; // extra cost for going through blocks
				child->location.first = x;
				child->location.second = y;
				child->heuristic = abs(x - targetTileX) + abs(y - targetTileY);
				child->parent = current;
				
				// is this location already on closed list?
				if (closed.find(child->location) != closed.end())
				{
					Node * onClosed = closed.find(child->location)->second;

					// is this child a better path?
					if ((child->cost + child->heuristic) < (onClosed->cost + onClosed->heuristic))
					{
						// update with this better node
						closed.erase(onClosed->location);
						delete onClosed;
						onClosed = NULL;
						closed.insert(pair<coord, Node *>(child->location, child));
					}

					// done with this child node
					continue;
				}

				// is this location already on open list?
				map<float, Node *>::iterator openIt;
				bool childReplacedOnList = false;
				float total = (float)(child->cost + child->heuristic);
				for (openIt = open.begin(); openIt != open.end(); ++openIt)
				{
					Node * onOpen = openIt->second;

					if (onOpen->location == child->location)
					{
						// this is the same location
						// better path?
						if ((child->cost + child->heuristic) < (onOpen->cost + onOpen->heuristic))
						{
							// update with this better node
							open.erase(openIt);
							delete onOpen;
							onOpen = NULL;
							// try to add to open map							
							while (!childReplacedOnList)
							{
								if (open.find(total) == open.end())
								{
									// ok to insert
									open.insert(pair<float, Node *>(total, child));
									childReplacedOnList = true;
								} 
								else
									total += 0.001f; // slightly inc total for different mapping
							} // end while

							if (childReplacedOnList)
								break;
						} 

					}
				} // end for to find same location
				if (childReplacedOnList)
					continue;

				// at this stage, child is not on open or closed, add to open
				bool addedToOpen = false;
				while (!addedToOpen)
				{
					if (open.find(total) == open.end())
					{
						// ok to insert
						open.insert(pair<float, Node *>(total, child));
						addedToOpen = true;
					} 
					else
						total += 0.001f; // slightly inc total for different mapping
				} // end while


			} // end for 0..3

			// add current to closed
			closed.insert(pair<coord, Node *>(current->location, current));


		} // end if not current goal node
	} // end while open list !empty

	// delete outstanding mem allocations
	while (!open.empty())
	{
		Node * n = open.begin()->second;
		open.erase(open.begin()); 
		delete n;
		n = NULL;
	}
	while (!closed.empty())
	{
		Node * n = closed.begin()->second;
		closed.erase(closed.begin()); 
		delete n;
		n = NULL;
	}

	return result;
}

bool PlayerCharacterEntity::safeLocationToBomb(int tileX, int tileY)
{
	AIGameView * view = AIGameView::getInstance();

	// set to true, test for disagreements with this
	bool result = true;

	for (int i = 0; i < 4; ++i)
	{
		int x, y;
		x = tileX;
		y = tileY;
		switch (i)
		{
		case 0:
			x++;
			break;
		case 1:
			x--;
			break;
		case 2:
			y++;
			break;
		case 3:
			y--;
			break;
		}

		vector<EntityInfo> info = view->getEntityInfoAtLocation(x, y);
		vector<EntityInfo>::iterator it;

		for (it = info.begin(); it != info.end(); ++it)
		{
			if ((it->groupType == EGG_GROUP && it->team == m_team) 
				|| it->type == ENERGY_DROP || it->groupType == POWERUP_GROUP)
			{
				result = false;
				break;
			}
		}

		if (result == false)
			break;

	}

	return result;
}

void PlayerCharacterEntity::checkEnvironment()
{
	// what is the closest enemy in a straight line? might want to launch a rocket
	EntityInfo nearest;
	nearest.id = -1; // marks no suitable enemy found (sorry for the "-1 bashing", free the first non-positive!)

	// modified from RocketSpinEntity::update() code
	bool dirFinished[4] = { false, false, false, false };
	int distance = 0; 
	int targetX = 0, targetY = 0;

	while ((!dirFinished[0]) || (!dirFinished[1])
			|| (!dirFinished[2]) || (!dirFinished[3]))
	{
		// try each direction in turn once
		// so that rocket targets nearest enemy
		distance++;
		for (int d = 0; d < 4; d++)
		{
			if (dirFinished[d])
				continue;
			
			DIRECTION dir;
			int xMultiplier, yMultiplier;
			// set these variables
			switch (d)
			{
			case 0:
				dir = UP; xMultiplier = 0; yMultiplier = -1;
				break;
			case 1:
				dir = DOWN; xMultiplier = 0; yMultiplier = 1;
				break;
			case 2:
				dir = LEFT; xMultiplier = -1; yMultiplier = 0;
				break;
			case 3:
				dir = RIGHT; xMultiplier = 1; yMultiplier = 0;
				break;
			}
			//unused. avoid compiler warning
			(void)dir;
			// get x and y co-ords to test
			int xTile, yTile;
            xTile = m_tileX + (distance * xMultiplier);
			yTile = m_tileY + (distance * yMultiplier);

			if (Map::getInstance()->staticTileAt(xTile, yTile) == Map::EMPTY)
			{
				// test this tile for enemies to launch at
				vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(xTile, yTile);
				vector<EntityInfo>::iterator infoIt;
				for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
				{
					if (infoIt->team != m_team && (infoIt->groupType == CHARACTER_GROUP
						|| infoIt->groupType == PLAYER_SPAWN_GROUP
						|| infoIt->groupType == ROBOT_GROUP) )
					{
						nearest = *infoIt;
						targetX = xTile;
						targetY = yTile;
						break;
					}


				}
				if (nearest.id != -1)
					break;
			} else {
				// reached a wall or block tile in this direction
				dirFinished[d] = true;
			}


		} // end for each direction

		if (nearest.id != -1)
			break;
	} // end while loop

	if (nearest.id != -1)
	{
		// enemy nearby in straight line
		// check distance is enough not to get caught in blast
		// and safe location to bomb
		if (distance > 1 && safeLocationToBomb(targetX, targetY) && m_health > 10)
		{
			// fire a rocket now! (if possible)
			AIInstruction * fireRocket = new AIInstruction(
				AIInstruction::CREATE_ENTITY, 1, ROCKET_SPIN, getTileX(), getTileY());
			m_insList.push_front(fireRocket);
			return;
		}

	}


	//	are there enemy eggs nearby?
	const int EGG_RANGE = 2;
	if (m_health > 24)
	{
		for (int x = m_tileX - EGG_RANGE; x <= m_tileX + EGG_RANGE; ++x)
		{
			if (x < 0 || x >= Map::MAP_WIDTH)
				continue;
			for (int y = m_tileY - EGG_RANGE; y <= m_tileY + EGG_RANGE; ++y)
			{
				if (y < 0 || y >= Map::MAP_HEIGHT)
					continue;

				if (x == m_tileX && y == m_tileY)
					continue;

				vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
				vector<EntityInfo>::iterator infoIt;
				for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
				{
					if (infoIt->team != m_team && infoIt->groupType == EGG_GROUP
						&& infoIt->type != EGG1 )
					{
						if (nearest.id == -1)
						{
							// set as found nearest egg entity
							nearest = *infoIt;
							targetX = x;
							targetY = y;
						} else {
							// new nearest?
							int oldDistance, newDistance;
							oldDistance = abs(targetX - m_tileX) + abs(targetY - m_tileY);
							newDistance = abs(x - m_tileX) + abs(y - m_tileY);
							if (newDistance < oldDistance)
							{
								// replace old found entity
								nearest = *infoIt;
								targetX = x;
								targetY = y;
							}		
						}
						break;
					}


				}


			}
		}
	}

	if (nearest.id != -1)
	{
		// we've found a nearby enemy egg

		// check for nearby friendly egg thieves
		bool nearbyThief = false;
		for (int x = targetX - EGG_RANGE; x <= targetX + EGG_RANGE; ++x)
		{
			if (x < 0 || x >= Map::MAP_WIDTH)
				continue;
			for (int y = targetY - EGG_RANGE; y <= targetY + EGG_RANGE; ++y)
			{
				if (y < 0 || y >= Map::MAP_HEIGHT)
					continue;

				if (x == m_tileX && y == m_tileY)
					continue;

				vector<EntityInfo> info = AIGameView::getInstance()->getEntityInfoAtLocation(x, y);
				vector<EntityInfo>::iterator infoIt;
				for (infoIt = info.begin(); infoIt != info.end(); ++infoIt)
				{
					if (infoIt->team == m_team && infoIt->type == EGG_THIEF)
					{
						nearbyThief = true;
						break;
					}
				}

				if (nearbyThief)
					break;
			}
			if (nearbyThief)
				break;
		}


		if ( safeLocationToBomb(targetX, targetY) && !nearbyThief )
		{
			// check possible bombing locations
			coord locations[4];

			for (int i = 0; i < 4; ++i)
			{
				int x, y;
				x = targetX;
				y = targetY;
				switch (i)
				{
				case 0:
					x++;
					break;
				case 1:
					x--;
					break;
				case 2:
					y++;
					break;
				case 3:
					y--;
					break;
				}

				if ( Map::getInstance()->staticTileAt(x, y) == Map::EMPTY
					&& safeLocationToBomb(x, y) )
				{
					locations[i].first = x;
					locations[i].second = y;
				} else {
					locations[i].first = -1;
					locations[i].second = -1;
				}

			} // end for 0..3

			// which location is nearest?
			int nearestLocation = -1;
			for (int i = 0; i <= 3; ++i)
			{
				if (locations[i].first != -1 && 
					safeLocationToBomb(locations[i].first, locations[i].second) )
				{
					// this is a valid location
					if (nearestLocation == -1)
						nearestLocation = i;
					else
					{
						int oldDistance, newDistance;
						oldDistance = abs(locations[nearestLocation].first - m_tileX) 
							+ abs(locations[nearestLocation].second - m_tileY);
						newDistance = abs(locations[i].first - m_tileX) 
							+ abs(locations[i].second - m_tileY);
						
						if (newDistance < oldDistance)
							nearestLocation = i;
					}
				}
			}

			if (nearestLocation != -1)
			{
				// we've got a target and a position! :D
				clearGoalsAndInstructions();

				AIInstruction * goTo, * placeBomb, * runAway, * pause;

				goTo = new AIInstruction(
					AIInstruction::GO_TO, 0, getType(), 
					locations[nearestLocation].first, 
					locations[nearestLocation].second);

				placeBomb = new AIInstruction(
					AIInstruction::CREATE_ENTITY, 1, BOMB,
					locations[nearestLocation].first, 
					locations[nearestLocation].second);

				runAway = new AIInstruction(
					AIInstruction::MOVE_FROM_LOCATION, 3,
					getType(), targetX, targetY);

				pause = new AIInstruction(
					AIInstruction::WAIT, 10, getType(), 0, 0);

				m_insList.push_front(pause);
				m_insList.push_front(runAway);
				m_insList.push_front(placeBomb);
				m_insList.push_front(goTo);

				m_checkTimer = 97;

				return;
			}
		}

	}


	// if we're pretty alone, maybe worth placing a mine




}

void PlayerCharacterEntity::clearGoalsAndInstructions()
{
	while (!m_goalList.empty())
	{
		delete m_goalList.front();
		m_goalList.pop_front();
	}
	while (!m_insList.empty())
	{
		delete m_insList.front();
		m_insList.pop_front();
	}
}
