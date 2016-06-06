// implementation of ERFactory
#include "EntityRendererFactory.h"

void EntityRendererFactory::initSurfaces(SDL_Surface* sdl_primary)
{	
	// set file names
	std::string filenames[NUM_ENTITY_TYPES];

	DataReader * data = DataReader::getInstance();

	// maybe I should replace the enum with strings...
	filenames[BLOCK] = data->getStringFromFile("BLOCK_IMAGE", "data/renderer.txt");
	filenames[PLAYER_CHARACTER] = data->getStringFromFile("PLAYER1_MALE", "data/renderer.txt");
	filenames[BOMB] = data->getStringFromFile("BOMB", "data/renderer.txt");
	filenames[EXPLOSION] = data->getStringFromFile("EXPLOSION", "data/renderer.txt"); 
	filenames[MINE] = data->getStringFromFile("MINE", "data/renderer.txt");
	filenames[ROCKET_SPIN] = data->getStringFromFile("ROCKET_SPIN", "data/renderer.txt");
	filenames[ROCKET_FIRED] = data->getStringFromFile("ROCKET_FIRED", "data/renderer.txt");
	filenames[ENERGY_DROP] = data->getStringFromFile("ENERGY_DROP", "data/renderer.txt");

	filenames[ENPLUS_POWERUP] = data->getStringFromFile("ENPLUS_POWERUP", "data/renderer.txt");
	filenames[IQPLUS_POWERUP] = data->getStringFromFile("IQPLUS_POWERUP", "data/renderer.txt");

	filenames[EGG1] = data->getStringFromFile("EGG1", "data/renderer.txt");
	filenames[EGG2] = data->getStringFromFile("EGG2", "data/renderer.txt");
	filenames[EGG3] = data->getStringFromFile("EGG3", "data/renderer.txt");
	filenames[EGG4] = data->getStringFromFile("EGG4", "data/renderer.txt");
	filenames[EGG5] = data->getStringFromFile("EGG5", "data/renderer.txt");

	filenames[EGG_THIEF] = data->getStringFromFile("EGG_THIEF", "data/renderer.txt");
	filenames[WALL_EATER] = data->getStringFromFile("WALL_EATER", "data/renderer.txt");
	filenames[DRONE] = data->getStringFromFile("DRONE", "data/renderer.txt");

	filenames[ROBOT1] = data->getStringFromFile("ROBOT1", "data/renderer.txt");

	filenames[MUNCH_EFFECT] = data->getStringFromFile("MUNCH_EFFECT", "data/renderer.txt");
	filenames[DAMAGE_EFFECT] = data->getStringFromFile("DAMAGE_EFFECT", "data/renderer.txt");

	// check files exist
	std::string empty;
	for (int i = 0; i < NUM_ENTITY_TYPES; ++i)
	{
		if (filenames[i] != empty)
		{
			// valid entry, check it exists
			if (!MickUtil::CheckFileExists(filenames[i].c_str()))
			{
				// file doesn't exist
				std::stringstream error;
				error << "File: " << filenames[i].c_str() << " is not found.";

				cerr << error.str();

//				MessageBox(windowHandle,
//					error.str().c_str(),
//					NULL, MB_ICONEXCLAMATION);
//				SendMessage(windowHandle,WM_CLOSE,0,0);
				return;
			}

			// initialise renderer
			m_pList[i] = new EntityRenderer(sdl_primary, filenames[i].c_str());
		}
	}

}

EntityRenderer * EntityRendererFactory::getEntityRenderer(EntityType t)
{
	if ((t >= 0) && (t < NUM_ENTITY_TYPES))
        return m_pList[t];
	else
		return NULL; // requested bad value
}

EntityRendererFactory::EntityRendererFactory()
{
	// constructor
	// zero out array of pointers
	for (int i = 0; i < NUM_ENTITY_TYPES; ++i)
		m_pList[i] = NULL;
}

EntityRendererFactory::~EntityRendererFactory()
{
	// destructor
	// delete any valid pointers
	for (int i = 0; i < NUM_ENTITY_TYPES; ++i)
		if (m_pList[i] != 0)
		{
			delete m_pList[i];
            m_pList[i] = 0;
		}
}

EntityRendererFactory * EntityRendererFactory::getInstance()
{
	static EntityRendererFactory instance;
	
	return &instance;
}
