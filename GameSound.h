/*
 * GameSound.h
 *
 *  Created on: Mar 29, 2012
 *      Author: msaun
 */

#ifndef GAMESOUND_H_
#define GAMESOUND_H_

#include <string>
#include "DataReader.h"
#include "MickUtil.h"
#include "MickBaseSound.h"

namespace std {

	class GameSound
	{
		public:
			GameSound();

			// this part shouldn't really be hard-coded
			//static const int NUM_SOUNDS = 16;
			enum SOUND_NAME { EGG_DROP, MINE_DROP, BLOCK_DROP, BOOM, MUNCHY, BOMB_FUSE,
									 ENERGY_DROP, ENERGY_PICKUP, EGG_STOLEN, SPLAT, ROCKET_SPIN,
									 ROBOT_CREATE, ROCKET_FIRED, THIEF_CREATE, EATER_CREATE,
									 DRONE_CREATE, FINAL_ENTRY_SOUND_NAME }; // make sure FINAL_ENTRY is always the last entry in the enum
			static GameSound* getInstance();

			static void playSound(SOUND_NAME sound);
			virtual ~GameSound();

		private:
			void loadSounds();
			static string filenames[FINAL_ENTRY_SOUND_NAME];
			static GameSound* s_instance;

	};

} /* namespace std */
#endif /* GAMESOUND_H_ */
