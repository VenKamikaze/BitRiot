/*
 * MickSDLSound.h
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#ifndef MICKSDLSOUND_H_
#define MICKSDLSOUND_H_

#include "MickBaseSound.h"
#include "SDL/SDL_mixer.h"
#include <set>
#include <map>


struct MickSoundFile {
	Mix_Chunk *sound = 0; //TODO: switch to nullptr (c++11)?
};

//class MickSDLSound: public std::MickBaseSound
class MickSDLSound : MickBaseSound
{
	public:
		MickSDLSound();
		virtual ~MickSDLSound();

		void initAudio();
		void playSound(std::string fileName);

		static MickSDLSound* getInstance();

	protected:
		void loadSound(std::string fileName, MickSoundFile* sound);

	private:
		std::map<std::string, MickSoundFile> cachedSounds;

};

#endif /* MICKSDLSOUND_H_ */
