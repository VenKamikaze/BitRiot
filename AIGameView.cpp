#include "AIGameView.h"

AIGameView * AIGameView::getInstance()
{
	static AIGameView instance;
	return &instance;
}

void AIGameView::setMapSize(int width, int height)
{
	m_pEntityMap->resize(width);
	for (int x = 0; x < width; x++)
	{
		m_pEntityMap->at(x).resize(height);
	}
}

AIGameView::AIGameView()
{
	m_pEntityMap = new vector< vector< vector< EntityInfo > > >;
}

AIGameView::~AIGameView()
{
	if (m_pEntityMap)
		delete m_pEntityMap;
	m_pEntityMap = NULL;
}

const vector< EntityInfo > & AIGameView::getEntityInfoAtLocation(const int tileX, const int tileY) const
{
	return m_pEntityMap->at(tileX).at(tileY);
}

bool AIGameView::addNewEntity(const EntityInfo & r_info, const int tileX, const int tileY)
{
	m_pEntityMap->at(tileX).at(tileY).push_back(r_info);
	return true;
}

bool AIGameView::moveEntity(const int id, const int oldX, const int oldY, const int newX, const int newY)
{
	// check entity is at old map location
	vector<EntityInfo>::iterator locationIterator;
	for (locationIterator = m_pEntityMap->at(oldX).at(oldY).begin();
		locationIterator != m_pEntityMap->at(oldX).at(oldY).end();
		locationIterator++)
	{
		if (locationIterator->id == id)
		{
			// entity found here

			// get info 
			EntityInfo info = (*locationIterator);
			// erase old position data
			m_pEntityMap->at(oldX).at(oldY).erase(locationIterator);
			// insert copied info to new position
			m_pEntityMap->at(newX).at(newY).push_back(info);

			return true;
		}
	}

	// not found
	assert("AIGameView moveEntity() failed to find entity!" && 0);
	return false;

}

bool AIGameView::removeEntity(const int id, const int tileX, const int tileY)
{
	// check entity is at old map location
	vector<EntityInfo>::iterator locationIterator;
	for (locationIterator = m_pEntityMap->at(tileX).at(tileY).begin();
		locationIterator != m_pEntityMap->at(tileX).at(tileY).end();
		locationIterator++)
	{
		if (locationIterator->id == id)
		{
			// entity found here
			m_pEntityMap->at(tileX).at(tileY).erase(locationIterator);
			return true;
		}
	}

	assert("AIGameView removeEntity() failed to find entity!" && 0);
	return false;
}

