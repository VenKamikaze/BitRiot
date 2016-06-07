/*
 * MickSDLInput.h
 *
 *  Created on: Mar 8, 2012
 *      Author: msaun
 */

#ifndef MICKSDLINPUT_H_
#define MICKSDLINPUT_H_

#include "MickBaseInput.h"
#include "SDL/SDL.h"
#include <map>
#include <set>

class MickSDLInput : MickBaseInput
{
	public:
		MickSDLInput();
		virtual ~MickSDLInput();

		SDL_Event* popEvent();
	    bool isKeyDown(KEY k); // cannot inline {return ( !(keysCurrentlyDown.end() == keysCurrentlyDown.find(k)) ); };

		bool isKeyReleased(KEY k) { return false; };

		std::set<KEY>* getKeysDown();

		void keyEvent(KEY k);

		//KEY_EVENT newEvent();
		void updateEventQueue();

		static MickBaseInput* getInstance();

	protected:
		void setupKeymap();
		void setupReverseKeymap(std::map<KEY, SDLKey>* forwardMap);

	private:
		SDL_Event event;
		std::set<KEY> keysCurrentlyDown;

};


#endif /* MICKSDLINPUT_H_ */
