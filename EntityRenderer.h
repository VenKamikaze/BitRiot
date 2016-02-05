// EntityRenderer.h

#ifndef _ENTITYRENDERERCLASS
#define _ENTITYRENDERERCLASS

// An EntityRender object contains all the
// data such as the DD surface and number of animation 
// frames per animation sequence, as well as methods to render

#include <ddraw.h>
#include "ddutil.h"
#include "Map.h"
#include "DataReader.h"

const int NUM_TEAMS = 5;

class EntityRenderer
{
public:	
	EntityRenderer(LPDIRECTDRAW7 lpdd, const char * filename);
	~EntityRenderer();

	static const int MAX_ANIM_STATES = 5;		
	static const int MAX_ANIM_FRAMES = 16;    



	enum ANIM_STATE 
	{ IDLE = 0, MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT };
	

	bool render(LPDIRECTDRAWSURFACE7 dest, int x, int y, int state, int animFrame, int team);
	int getNumFrames(int team) { return m_numFrames[team]; }
	bool replaceTeamSurfaceWithImage(LPDIRECTDRAW7 lpdd, const char * filename, int team);

private:
	LPDIRECTDRAWSURFACE7 m_animSurface[NUM_TEAMS]; // surface pointers for different teams
	int m_numFrames[NUM_TEAMS]; // number of animation frames for each team

	void changeToTeamColour(LPDIRECTDRAWSURFACE7 surface, int team);

};

#endif