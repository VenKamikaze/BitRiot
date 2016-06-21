// AIGameView, for AI controlled entities to determine where
// other entities are without requiring the similar
// DynamicMap class, as that would mean a circular dependency.
// So, a little bit like a facade class then?

// Yet another Singleton class thanks to there only ever being one map

#ifndef _AIGAMEVIEWCLASS
#define _AIGAMEVIEWCLASS

#include "EntityEnum.h" 

#include <vector>
#include <assert.h>

using namespace std;

struct EntityInfo
{
	int id;
	int team;
	EntityType type;
	EntityGroupType groupType;
	bool staticFlag;
};

class AIGameView
{
public:
	static AIGameView * getInstance();
	void setMapSize(const int width, const int height); // sets size of map
	~AIGameView();

	const vector< EntityInfo > & getEntityInfoAtLocation(const int tileX, const int tileY) const;

	bool addNewEntity(const EntityInfo & r_info, const int tileX, const int tileY);
	bool moveEntity(const int id, const int oldX, const int oldY, const int newX, const int newY);
	bool removeEntity(const int id, const int tileX, const int tileY);


private:
	AIGameView();

	vector< vector< vector< EntityInfo > > > * m_pEntityMap;


};

#endif