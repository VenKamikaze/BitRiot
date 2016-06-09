// default behaviour for all game entities

#include "GameEntity.h"

GameEntity::GameEntity()
{
	// standard behaviour for most entities
	m_staticFlag = false;
	m_solidFlag = false;
	m_walkThroughBlocks = false;
 
	m_tileX = m_tileY = 0;
	m_offsetX = m_offsetY = 0;
			
	m_currentAnimState = EntityRenderer::IDLE;
	m_currentAnimFrame = 0;

}

void GameEntity::moveInDirection(DIRECTION d)
{
	Map * staticMap = Map::getInstance();
	
	//unsused
	//EntityRenderer * er = getEntityRenderer();


	float thisMoveVelocity = m_velocity;

	switch (d)
	{
	case UP:
		// check for collision with block moving to
		if (staticMap->staticTileAt(m_tileX, m_tileY - 1) == Map::WALL
			|| (staticMap->staticTileAt(m_tileX, m_tileY - 1) == Map::BLOCK && !m_walkThroughBlocks))
		{
			// limit max velocity for this move
			if (m_offsetY - m_velocity < 0)
				thisMoveVelocity = m_offsetY;
		}

		// now try and move up in Y
		// if offsetX is not 0.0, move towards center
		if (m_offsetX < 0.0f)
		{
			// move +ve in x direction
			if (m_offsetX + thisMoveVelocity <= 0.0f)
			{
				// need to move entire move in x direction
				m_offsetX += thisMoveVelocity;
			} else {
				// only move part of move in x direction
				float remainingMove = m_offsetX + thisMoveVelocity;
				m_offsetX = 0.0f;
				m_offsetY -= remainingMove;
			}
		} else {
			if (m_offsetX > 0.0f)
			{
				// move +ve in x direction
				if (m_offsetX - thisMoveVelocity >= 0.0f)
				{
					// need to move entire move in x direction
					m_offsetX -= thisMoveVelocity;
				} else {
					// only move part of move in x direction
					float remainingMove = thisMoveVelocity - m_offsetX;
					m_offsetX = 0.0f;
					m_offsetY -= remainingMove;
				}

			} else {
				// centered in offset, move only in Y
				m_offsetY -= thisMoveVelocity;
			}
		}

		// update animation
		if (m_currentAnimState != EntityRenderer::MOVE_UP)
		{
			// if moving up animation exists, change to it			
            m_currentAnimState = EntityRenderer::MOVE_UP;
			m_currentAnimFrame = 0;
		}

		break;
	case DOWN:
		if (staticMap->staticTileAt(m_tileX, m_tileY + 1) == Map::WALL
			|| (staticMap->staticTileAt(m_tileX, m_tileY + 1) == Map::BLOCK && !m_walkThroughBlocks))
		{			
			// limit max velocity for this move
			if (m_offsetY + m_velocity > 0)
				thisMoveVelocity = -m_offsetY;
		}

		// now try and move down in Y
		// if offsetX is not 0.0, move towards center
		if (m_offsetX < 0.0f)
		{
			// move +ve in x direction
			if (m_offsetX + thisMoveVelocity <= 0.0f)
			{
				// need to move entire move in x direction
				m_offsetX += thisMoveVelocity;
			} else {
				// only move part of move in x direction
				float remainingMove = m_offsetX + thisMoveVelocity;
				m_offsetX = 0.0f;
				m_offsetY += remainingMove;
			}
		} else {
			if (m_offsetX > 0.0f)
			{
				// move +ve in x direction
				if (m_offsetX - thisMoveVelocity >= 0.0f)
				{
					// need to move entire move in x direction
					m_offsetX -= thisMoveVelocity;
				} else {
					// only move part of move in x direction
					float remainingMove = thisMoveVelocity - m_offsetX;
					m_offsetX = 0.0f;
					m_offsetY += remainingMove;
				}

			} else {
				// centered in offset, move only in Y
				m_offsetY += thisMoveVelocity;
			}
		}

		// update animation
		if (m_currentAnimState != EntityRenderer::MOVE_DOWN)
		{
			// if moving up animation exists, change to it
            m_currentAnimState = EntityRenderer::MOVE_DOWN;
			m_currentAnimFrame = 0;
		}

		break;
	case LEFT:
		if (staticMap->staticTileAt(m_tileX - 1, m_tileY) == Map::WALL
			|| (staticMap->staticTileAt(m_tileX - 1, m_tileY) == Map::BLOCK && !m_walkThroughBlocks))
		{
			// limit max velocity for this move
			if (m_offsetX - m_velocity < 0)
				thisMoveVelocity = m_offsetX;
		}

		
		// now try and move left in X
		// if offsetY is not 0.0, move towards center
		if (m_offsetY < 0.0f)
		{
			// move +ve in y direction
			if (m_offsetY + thisMoveVelocity <= 0.0f)
			{
				// need to move entire move in y direction
				m_offsetY += thisMoveVelocity;
			} else {
				// only move part of move in y direction
				float remainingMove = m_offsetY + thisMoveVelocity;
				m_offsetY = 0.0f;
				m_offsetX -= remainingMove;
			}
		} else {
			if (m_offsetY > 0.0f)
			{
				// move +ve in y direction
				if (m_offsetY - thisMoveVelocity >= 0.0f)
				{
					// need to move entire move in y direction
					m_offsetY -= thisMoveVelocity;
				} else {
					// only move part of move in y direction
					float remainingMove = thisMoveVelocity - m_offsetY;
					m_offsetY = 0.0f;
					m_offsetX -= remainingMove;
				}

			} else {
				// centered in offset, move only in X
				m_offsetX -= thisMoveVelocity;
			}
		}

		// update animation
		if (m_currentAnimState != EntityRenderer::MOVE_LEFT)
		{
			// if moving up animation exists, change to it
			m_currentAnimState = EntityRenderer::MOVE_LEFT;
			m_currentAnimFrame = 0;
		}

		break;
	case RIGHT:
		if (staticMap->staticTileAt(m_tileX + 1, m_tileY) == Map::WALL
			|| (staticMap->staticTileAt(m_tileX +  1, m_tileY) == Map::BLOCK && !m_walkThroughBlocks))
		{
			// limit max velocity for this move
			if (m_offsetX + m_velocity > 0)
				thisMoveVelocity = -m_offsetX;
		}

		if (m_offsetY < 0.0f)
		{
			// move +ve in y direction
			if (m_offsetY + thisMoveVelocity <= 0.0f)
			{
				// need to move entire move in y direction
				m_offsetY += thisMoveVelocity;
			} else {
				// only move part of move in y direction
				float remainingMove = m_offsetY + thisMoveVelocity;
				m_offsetY = 0.0f;
				m_offsetX += remainingMove;
			}
		} else {
			if (m_offsetY > 0.0f)
			{
				// move +ve in y direction
				if (m_offsetY - thisMoveVelocity >= 0.0f)
				{
					// need to move entire move in y direction
					m_offsetY -= thisMoveVelocity;
				} else {
					// only move part of move in x direction
					float remainingMove = thisMoveVelocity - m_offsetY;
					m_offsetY = 0.0f;
					m_offsetX += remainingMove;
				}

			} else {
				// centered in offset, move only in x
				m_offsetX += thisMoveVelocity;
			}
		}

		// update animation
		if (m_currentAnimState != EntityRenderer::MOVE_RIGHT)
		{
			m_currentAnimState = EntityRenderer::MOVE_RIGHT;
			m_currentAnimFrame = 0;
		}

		break;
	}

	// see if tile positions need updating
	EntityMessageQueue * emq = EntityMessageQueue::getInstance();

	if (m_offsetX < -1.0f)
	{
		m_offsetX+= 2.0f;
		m_tileX--;
		emq->postMoveMessage(m_ID, m_tileX + 1, m_tileY, m_tileX, m_tileY);
	} else {
        if (m_offsetX > 1.0f)
		{
			m_offsetX-= 2.0f;
			m_tileX++;
			emq->postMoveMessage(m_ID, m_tileX - 1, m_tileY, m_tileX, m_tileY);
		}
	}

	if (m_offsetY < -1.0f)
	{
		m_offsetY += 2.0f;
		m_tileY--;
		emq->postMoveMessage(m_ID, m_tileX, m_tileY + 1, m_tileX, m_tileY);
	} else {
		if (m_offsetY > 1.0f)
		{
			m_offsetY -= 2.0f;
			m_tileY++;
			emq->postMoveMessage(m_ID, m_tileX, m_tileY - 1, m_tileX, m_tileY);
		}
	}

}




EntityRenderer * GameEntity::getEntityRenderer()
{
	EntityRendererFactory * erf = EntityRendererFactory::getInstance();
	return erf->getEntityRenderer(m_type);
}

bool GameEntity::render(SDL_Surface* dest)
{	
	EntityRenderer * er = getEntityRenderer();

	int destX = (m_tileX * Map::TILE_WIDTH) + (int)(m_offsetX * (Map::TILE_WIDTH/2));
	int destY = (m_tileY * Map::TILE_WIDTH) + (int)(m_offsetY * (Map::TILE_WIDTH/2));

	return er->render(dest, destX, destY, m_currentAnimState, m_currentAnimFrame, m_team);
}

void GameEntity::incAnimFrame()
{
	EntityRenderer * er = getEntityRenderer();

	m_currentAnimFrame++;

	if (m_currentAnimFrame >= er->getNumFrames(m_team))
	{
		m_currentAnimFrame = 0;
	}
}

void GameEntity::explode()
{
	EntityMessageQueue * emq = EntityMessageQueue::getInstance();
	emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY, 0);

	// create explosions at x +- 1 and y +- 1
	Map * staticMap = Map::getInstance();
	if (staticMap->staticTileAt(m_tileX - 1, m_tileY) != Map::WALL)
		emq->postCreateMessage(EXPLOSION, 0, m_tileX - 1, m_tileY, 0);
	if (staticMap->staticTileAt(m_tileX + 1, m_tileY) != Map::WALL)
		emq->postCreateMessage(EXPLOSION, 0, m_tileX + 1, m_tileY, 0);
	if (staticMap->staticTileAt(m_tileX, m_tileY - 1) != Map::WALL)
		emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY - 1, 0);
	if (staticMap->staticTileAt(m_tileX, m_tileY + 1) != Map::WALL)
		emq->postCreateMessage(EXPLOSION, 0, m_tileX, m_tileY + 1, 0);

	emq->postDestroyMessage(m_ID);
	////M2S SOUND DSound::getInstance()->playSound(DSound::BOOM); // M2S SOUND
	//MickBaseSound::getInstance()->playSound("audio/boom.wav");
	GameSound::playSound(GameSound::BOOM);
}
