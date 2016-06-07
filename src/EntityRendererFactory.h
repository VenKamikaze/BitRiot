// EntityRendererFactory
#ifndef _ERFACTORYCLASS
#define _ERFACTORYCLASS
// supplies a pointer to a valid EntityRenderer depending on type
// this is a singleton class
#include "EntityRenderer.h"
#include "EntityEnum.h"
#include <string>
#include <sstream>
#include "SDL/SDL.h"
#include "MickUtil.h"


class EntityRendererFactory
{
public:
	static EntityRendererFactory * getInstance();

	void initSurfaces(SDL_Surface* sdl_primary);

	EntityRenderer * getEntityRenderer(EntityType t);

private:
	// array of pointers to EntityRenderers
	EntityRenderer * m_pList[NUM_ENTITY_TYPES];

	EntityRendererFactory();
	~EntityRendererFactory();
};


#endif
