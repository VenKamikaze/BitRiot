// EntityRenderer.h

#ifndef _ENTITYRENDERERCLASS
#define _ENTITYRENDERERCLASS

// An EntityRender object contains all the
// data such as the DD surface and number of animation 
// frames per animation sequence, as well as methods to render

//#include <ddraw.h>
//#include "ddutil.h"
#include "Map.h"
#include "DataReader.h"
#include "SDLHelper.h"

const int NUM_TEAMS = 5;

class EntityRenderer
{
public:	
	EntityRenderer(SDL_Surface* sdl_primary, const char * filename);
    ~EntityRenderer();

	static const int MAX_ANIM_STATES = 5;		
	static const int MAX_ANIM_FRAMES = 16;    

	static Uint32 TEAM_HIGHLIGHT_COLOR[NUM_TEAMS];
	static Uint32 TEAM_BASE_COLOR[NUM_TEAMS];
	static Uint32 TEAM_SHADOW_COLOR[NUM_TEAMS];

	enum ANIM_STATE 
	{ IDLE = 0, MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT };
	

	bool render(SDL_Surface* dest, int x, int y, int state, int animFrame, int team);
	int getNumFrames(int team) { return m_numFrames[team]; }
	bool replaceTeamSurfaceWithImage(const char * filename, int team);

private:
	void setupColours(SDL_Surface* sdl_primary);

	SDL_Surface* m_animSurface[NUM_TEAMS]; // surface pointers for different teams
	int m_numFrames[NUM_TEAMS]; // number of animation frames for each team

	void changeToTeamColour(SDL_Surface* surface, int team);

};

#endif
