/*
 * MickSDLInput.cpp
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#include "MickSDLInput.h"

using namespace std;

static map<KEY, SDLKey>* translateMap;
static map<SDLKey, KEY>* reverseTranslateMap;
static MickSDLInput *s_instance = NULL;

MickSDLInput::MickSDLInput() : MickBaseInput()
{
	// TODO Auto-generated constructor stub
	aKeyIsDown = false;
	quitEvent = false;
	translateMap = new map<KEY, SDLKey>();
	reverseTranslateMap = new map<SDLKey, KEY>();
	setupKeymap();
}

MickBaseInput* MickSDLInput::getInstance()
{
	if (!s_instance)
	{
		s_instance = new MickSDLInput();
	}
	return s_instance;
}
/*
SDL_Event* MickSDLInput::popEvent()
{
	if(SDL_PollEvent(&event) != 0)
	{
		return &event;
	}

	return NULL;
}
*/

/**
 * Fix this. Shouldn't use isKeyDown to set aKeyIsDown or the vector of keysDown

bool MickSDLInput::isKeyDown(KEY k)
{
	if (keysCurrentlyDown.end() == keysCurrentlyDown.find(k))
		return true;

	if(! event.type == SDL_KEYDOWN)
		return false;

	SDLKey keySDL = translateMap->find(k)->second;
	if(keySDL == event.key.keysym.sym)
	{
		aKeyIsDown = true;
		keysCurrentlyDown.insert(k);
		return true;
	}
	return false;

}

bool MickSDLInput::isKeyReleased(KEY k)
{
	if(! event.type == SDL_KEYUP)
		return false;

	SDLKey keySDL = translateMap->find(k)->second;
	if(keySDL == event.key.keysym.sym)
	{
		keysCurrentlyDown.erase(k);
		aKeyIsDown = (!keysCurrentlyDown.empty());
		return true;

	}
	return false;
}
*/
/**
 * Useless.. started function and decided it didn't work.
 * Figure out if I can use it in some way...
 */
void MickSDLInput::keyEvent(KEY k)
{
	if(event.type == SDL_KEYUP)
	{
		isKeyReleased(k);
	}

	if(event.type == SDL_KEYDOWN)
	{
		if (keysCurrentlyDown.end() == keysCurrentlyDown.find(k))
			return;
	}
}

bool MickSDLInput::isKeyDown(KEY k)
{
	return ( !(keysCurrentlyDown.end() == keysCurrentlyDown.find(k)) );
}

void MickSDLInput::updateEventQueue()
{
	while(SDL_PollEvent(&event))
	{
		if(event.type == SDL_KEYUP)
		{
			MickEvent myevent;
			myevent.type = A_KEY_IS_UP;
			myevent.key  = reverseTranslateMap->find(event.key.keysym.sym)->second;
			//newEvents.push(myevent);

			keysCurrentlyDown.erase(myevent.key);
			aKeyIsDown = (!keysCurrentlyDown.empty());
			//printf("keyup: %d, sdl: %d\n", myevent.key, event.key.keysym.sym);
			//printf("actual key: %d", KEY_UP);
		}
		else if (event.type == SDL_KEYDOWN)
		{
			MickEvent myevent;
			myevent.type = A_KEY_IS_DOWN;
			myevent.key  = reverseTranslateMap->find(event.key.keysym.sym)->second;
			//newEvents.push(myevent);

			aKeyIsDown = true;
			keysCurrentlyDown.insert(myevent.key);
		}
		else if (event.type == SDL_QUIT)
		{
			//unused
			//MickEvent myevent;
			//myevent.type = KEY_QUIT_EVENT;
			//newEvents.push(myevent);
			quitEvent = true;
		}
	}
}

set<KEY>* MickSDLInput::getKeysDown()
{
	return &keysCurrentlyDown;
}

void MickSDLInput::setupKeymap()
{
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_UNKNOWN	, SDLK_UNKNOWN	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_FIRST		, SDLK_FIRST		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_BACKSPACE	, SDLK_BACKSPACE	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_TAB		, SDLK_TAB		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_CLEAR		, SDLK_CLEAR		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RETURN		, SDLK_RETURN		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_PAUSE		, SDLK_PAUSE		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_ESCAPE		, SDLK_ESCAPE		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_SPACE		, SDLK_SPACE		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_EXCLAIM	, SDLK_EXCLAIM	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_QUOTE	, SDLK_QUOTEDBL	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_HASH		, SDLK_HASH		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_DOLLAR		, SDLK_DOLLAR		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_AMPERSAND	, SDLK_AMPERSAND	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_APOSTROPHE	, SDLK_QUOTE		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LEFTPAREN	, SDLK_LEFTPAREN	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RIGHTPAREN	, SDLK_RIGHTPAREN	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_ASTERISK	, SDLK_ASTERISK	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_PLUS		, SDLK_PLUS		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_COMMA		, SDLK_COMMA		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_MINUS		, SDLK_MINUS		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_PERIOD		, SDLK_PERIOD		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_SLASH		, SDLK_SLASH		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_0			, SDLK_0			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_1			, SDLK_1			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_2			, SDLK_2			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_3			, SDLK_3			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_4			, SDLK_4			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_5			, SDLK_5			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_6			, SDLK_6			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_7			, SDLK_7			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_8			, SDLK_8			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_9			, SDLK_9			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_COLON		, SDLK_COLON		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_SEMICOLON	, SDLK_SEMICOLON	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LESS		, SDLK_LESS		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_EQUALS		, SDLK_EQUALS		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_GREATER	, SDLK_GREATER	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_QUESTION	, SDLK_QUESTION	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_AT, SDLK_AT));

	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LEFTBRACKET	, SDLK_LEFTBRACKET	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_BACKSLASH		, SDLK_BACKSLASH		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RIGHTBRACKET	, SDLK_RIGHTBRACKET	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_CARET		, SDLK_CARET		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_UNDERSCORE	, SDLK_UNDERSCORE	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_BACKQUOTE	, SDLK_BACKQUOTE	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_a			, SDLK_a			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_b			, SDLK_b			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_c			, SDLK_c			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_d			, SDLK_d			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_e			, SDLK_e			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_f			, SDLK_f			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_g			, SDLK_g			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_h			, SDLK_h			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_i			, SDLK_i			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_j			, SDLK_j			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_k			, SDLK_k			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_l			, SDLK_l			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_m			, SDLK_m			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_n			, SDLK_n			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_o			, SDLK_o			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_p			, SDLK_p			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_q			, SDLK_q			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_r			, SDLK_r			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_s			, SDLK_s			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_t			, SDLK_t			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_u			, SDLK_u			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_v			, SDLK_v			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_w			, SDLK_w			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_x			, SDLK_x			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_y			, SDLK_y			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_z			, SDLK_z			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_DELETE		, SDLK_DELETE		));

	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP0		, SDLK_KP0		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP1		, SDLK_KP1		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP2		, SDLK_KP2		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP3		, SDLK_KP3		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP4		, SDLK_KP4		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP5		, SDLK_KP5		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP6		, SDLK_KP6		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP7		, SDLK_KP7		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP8		, SDLK_KP8		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP9		, SDLK_KP9		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_PERIOD	, SDLK_KP_PERIOD	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_DIVIDE	, SDLK_KP_DIVIDE	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_MULTIPLY, SDLK_KP_MULTIPLY));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_MINUS	, SDLK_KP_MINUS	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_PLUS	, SDLK_KP_PLUS	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_ENTER	, SDLK_KP_ENTER	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_KP_EQUALS	, SDLK_KP_EQUALS	));

	translateMap->insert(std::pair<KEY, SDLKey>(KEY_UP			, SDLK_UP			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_DOWN		, SDLK_DOWN		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RIGHT		, SDLK_RIGHT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LEFT		, SDLK_LEFT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_INSERT		, SDLK_INSERT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_HOME		, SDLK_HOME		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_END		, SDLK_END		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_PAGEUP		, SDLK_PAGEUP		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_PAGEDOWN	, SDLK_PAGEDOWN	));

	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F1			, SDLK_F1			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F2			, SDLK_F2			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F3			, SDLK_F3			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F4			, SDLK_F4			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F5			, SDLK_F5			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F6			, SDLK_F6			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F7			, SDLK_F7			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F8			, SDLK_F8			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F9			, SDLK_F9			));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F10		, SDLK_F10		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F11		, SDLK_F11		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F12		, SDLK_F12		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F13		, SDLK_F13		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F14		, SDLK_F14		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_F15		, SDLK_F15		));

	translateMap->insert(std::pair<KEY, SDLKey>(KEY_NUMLOCK	, SDLK_NUMLOCK	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_CAPSLOCK	, SDLK_CAPSLOCK	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_SCROLLOCK	, SDLK_SCROLLOCK	));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RSHIFT		, SDLK_RSHIFT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LSHIFT		, SDLK_LSHIFT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RCTRL		, SDLK_RCTRL		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LCTRL		, SDLK_LCTRL		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RALT		, SDLK_RALT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LALT		, SDLK_LALT		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RMETA		, SDLK_RMETA		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LMETA		, SDLK_LMETA		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_LSUPER		, SDLK_LSUPER		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_RSUPER		, SDLK_RSUPER		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_MODE		, SDLK_MODE		));
	translateMap->insert(std::pair<KEY, SDLKey>(KEY_COMPOSE	, SDLK_COMPOSE	));

	setupReverseKeymap(translateMap);
}

void MickSDLInput::setupReverseKeymap(map<KEY, SDLKey>* forwardMap)
{
	map<KEY, SDLKey>::reverse_iterator rTransIt;
	for ( rTransIt=translateMap->rbegin() ; rTransIt != translateMap->rend(); rTransIt++ )
	{
		reverseTranslateMap->insert(std::pair<SDLKey, KEY>(rTransIt->second, rTransIt->first));
		printf("reversemap size: %lu\n",reverseTranslateMap->size());
	}
}

MickSDLInput::~MickSDLInput()
{
	translateMap->clear();
	delete translateMap;
	translateMap = NULL;

	reverseTranslateMap->clear();
	delete reverseTranslateMap;
	reverseTranslateMap = NULL;
	delete s_instance;
}

