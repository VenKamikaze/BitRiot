// Wrapper class for DirectSound using singleton pattern
#ifndef _DSOUNDCLASS
#define _DSOUNDCLASS

#include <dsound.h> // Direct Sound include
#include "dsutil.h" // for loading wave files

// following for displaying error messages
#include <string>
#include <sstream>
#include <shlwapi.h>
#include "DataReader.h"

class DSound
{
public:
	static DSound * getInstance();
	void initialise(HWND windowHandle); // has to be called once on startup
	~DSound();

	// this part shouldn't really be hard-coded
	static const int NUM_SOUNDS = 16;
	static enum SOUND_NAME { EGG_DROP, MINE_DROP, BLOCK_DROP, BOOM, MUNCHY, BOMB_FUSE,
							 ENERGY_DROP, ENERGY_PICKUP, EGG_STOLEN, SPLAT, ROCKET_SPIN,
							 ROBOT_CREATE, ROCKET_FIRED, THIEF_CREATE, EATER_CREATE,
							 DRONE_CREATE };

    void playSound(SOUND_NAME name);

	void setMuted(bool b) { m_muted = (b) ? true : false; }

private:
	DSound();	
	bool initialised;
	void loadSounds(HWND windowHandle);

	bool m_muted;

	LPDIRECTSOUND8 lpds;

	LPDIRECTSOUNDBUFFER buffers[NUM_SOUNDS];

};


#endif