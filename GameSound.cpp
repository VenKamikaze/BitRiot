/*
 * GameSound.cpp
 *
 *  Created on: Mar 29, 2012
 *      Author: msaun
 */

#include "GameSound.h"


namespace std
{
	GameSound* GameSound::s_instance = NULL; // not needed

	string GameSound::filenames[FINAL_ENTRY_SOUND_NAME];

	GameSound::GameSound() {
		// TODO Auto-generated constructor stub
		loadSounds();
	}

	void GameSound::loadSounds()
	{
		DataReader * data = DataReader::getInstance();

		filenames[BOMB_FUSE] = data->getStringFromFile("BOMB_FUSE", "data/audio.txt");
		filenames[EGG_DROP] = data->getStringFromFile("EGG_DROP", "data/audio.txt");
		filenames[MINE_DROP] = data->getStringFromFile("MINE_DROP", "data/audio.txt");
		filenames[BLOCK_DROP] = data->getStringFromFile("BLOCK_DROP", "data/audio.txt");
		filenames[BOOM] = data->getStringFromFile("BOOM", "data/audio.txt");
		filenames[MUNCHY] = data->getStringFromFile("MUNCHY", "data/audio.txt");
		filenames[ENERGY_DROP] = data->getStringFromFile("ENERGY_DROP", "data/audio.txt");
		filenames[ENERGY_PICKUP] = data->getStringFromFile("ENERGY_PICKUP", "data/audio.txt");
		filenames[EGG_STOLEN] = data->getStringFromFile("EGG_STOLEN", "data/audio.txt");
		filenames[SPLAT] = data->getStringFromFile("SPLAT", "data/audio.txt");
		filenames[ROCKET_SPIN] = data->getStringFromFile("ROCKET_SPIN", "data/audio.txt");
		filenames[ROCKET_FIRED] = data->getStringFromFile("ROCKET_FIRED", "data/audio.txt");
		filenames[ROBOT_CREATE] = data->getStringFromFile("ROBOT_CREATE", "data/audio.txt");
		filenames[THIEF_CREATE] = data->getStringFromFile("THIEF_CREATE", "data/audio.txt");
		filenames[EATER_CREATE] = data->getStringFromFile("EATER_CREATE", "data/audio.txt");
		filenames[DRONE_CREATE] = data->getStringFromFile("DRONE_CREATE", "data/audio.txt");

		// check files exist
		string empty;
		for (int i = 0; i < FINAL_ENTRY_SOUND_NAME; ++i)
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

					return;
				}
			}
		}
	}

	void GameSound::playSound(SOUND_NAME sound)
	{
		if(!s_instance)
			getInstance();

		MickBaseSound::getInstance()->playSound(filenames[sound]);
	}

	// not needed
	GameSound* GameSound::getInstance()
	{
		if (!s_instance)
		{
			s_instance = new GameSound();
		}
		return s_instance;
	}


	GameSound::~GameSound() {

	}

} /* namespace std */
