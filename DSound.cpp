#include "DSound.h"

DSound * DSound::getInstance()
{
	static DSound instance;
	return &instance;
}

DSound::DSound()
{
	initialised = false;	
	m_muted = false;
}

void DSound::initialise(HWND main_window_handle)
{
	if (initialised)
		return;

	if (FAILED(DirectSoundCreate8(NULL, &lpds, NULL)))
	{
		MessageBox(main_window_handle, 
			"Failed to initialise IDirectSound8 Interface!", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(main_window_handle,WM_CLOSE,0,0);
		return;
	}

	// set cooperative level
	if (FAILED(lpds->SetCooperativeLevel(main_window_handle, DSSCL_NORMAL)))
	{
		MessageBox(main_window_handle, 
			"Failed to set IDirectSound8 Cooperative Level!", 
			NULL, MB_ICONEXCLAMATION);
		SendMessage(main_window_handle,WM_CLOSE,0,0);
		return;
	}

	loadSounds(main_window_handle);
	initialised = true;
}

DSound::~DSound()
{
	for (int i = 0; i < NUM_SOUNDS; i++)
	{
		if (buffers[i])
			buffers[i]->Release();	
	}

	if (lpds)
        lpds->Release();		
}

void DSound::loadSounds(HWND main_window_handle)
{
	std::string filenames[NUM_SOUNDS];

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
	std::string empty;
	for (int i = 0; i < NUM_SOUNDS; ++i)
	{
		if (filenames[i] != empty)
		{
			// valid entry, check it exists
			if (!PathFileExists(filenames[i].c_str()))
			{
				// file doesn't exist
				std::stringstream error;
				error << "File: " << filenames[i].c_str() << " is not found.";

				MessageBox(main_window_handle, 
					error.str().c_str(), 
					NULL, MB_ICONEXCLAMATION);
				SendMessage(main_window_handle,WM_CLOSE,0,0);
				return;
			}
		}
	}

	// load sounds
	for (int i = 0; i < NUM_SOUNDS; i++)
	{
		if (filenames[i] == empty)
			break;
        CWaveFile wave;
		wave.Open((LPSTR)filenames[i].c_str(), 0, WAVEFILE_READ);
		WAVEFORMATEX * waveFormat = wave.GetFormat();

		// set up directsound buffer description
		DSBUFFERDESC dsbdesc; 
		memset(&dsbdesc, 0, sizeof(dsbdesc));
		dsbdesc.dwSize = sizeof(dsbdesc);
		
		dsbdesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_STATIC;	
		dsbdesc.dwBufferBytes = wave.GetSize();
		dsbdesc.lpwfxFormat = waveFormat;

		// create the buffer
		HRESULT result = lpds->CreateSoundBuffer(&dsbdesc, &buffers[i], NULL);
		if (FAILED(result))
		{
			MessageBox(main_window_handle, 
				"Failed to create a DirectSound buffer!", 
				NULL, MB_ICONEXCLAMATION);
			SendMessage(main_window_handle,WM_CLOSE,0,0);
			return;
		}

		// write buffer from wave data
        BYTE * writeBuffer1;
		BYTE * writeBuffer2;
		DWORD write1, write2;

		buffers[i]->Lock(0, NULL, (LPVOID *)&writeBuffer1, &write1, (LPVOID *)&writeBuffer2, &write2, DSBLOCK_ENTIREBUFFER);

		DWORD bytesWritten1 = 0, bytesWritten2 = 0;
		DWORD bytesWrittenTotal = 0;
		while (bytesWrittenTotal < write1 - 1)
		{
			wave.Read(writeBuffer1, write1, &bytesWrittenTotal);
		}
		if (bytesWrittenTotal < wave.GetSize())
		{
			// also write to buffer 2
			while (bytesWrittenTotal < wave.GetSize())
			{
				wave.Read(writeBuffer2, write2, &bytesWrittenTotal);
			}
		}
		buffers[i]->Unlock(&writeBuffer1, bytesWritten1, &writeBuffer2, bytesWritten2);

		wave.Close();
	}
	

}

void DSound::playSound(SOUND_NAME name)
{
	if (m_muted)
		return;

	if (buffers[name])
	{
		buffers[name]->Play(0, 0, 0);	
	}
}