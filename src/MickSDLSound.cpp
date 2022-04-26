/*
 * MickSDLSound.cpp
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#include "MickSDLSound.h"
#include "MickLogger.h"
#include "SDL.h"
#include "SDL_audio.h"
#include <string>

using namespace std;

static MickSDLSound *s_instance = NULL;

MickSDLSound::MickSDLSound() : MickBaseSound()
{
  // TODO Auto-generated constructor stub
}

MickSDLSound::~MickSDLSound()
{
  map<string, MickSoundFile>::iterator it;
  for ( it = cachedSounds.begin() ; it != cachedSounds.end(); it++ )
  {
    Mix_FreeChunk(it->second.sound);
    //printf("free'd sound chunk %s\n",it->first.c_str());
  }
  cachedSounds.clear();

  destructAudio();
}

void MickSDLSound::destructAudio()
{
  if(initialised && SDL_WasInit(SDL_INIT_AUDIO))
  {
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    initialised = false;
  }
}

MickSDLSound* MickSDLSound::getInstance()
{
  if (!s_instance)
  {
    s_instance = new MickSDLSound();
    s_instance->setBaseInstance(s_instance);
  }
  return s_instance;
}

void MickSDLSound::initAudio()
{
  if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
  {
    std::string msg = "Failed to initialise SDL Audio subsystem!";
    MickLogger::getInstance()->error(this, msg);
    // we can continue without audio.
    return;
  };
  //Initialize SDL_mixer
  if( Mix_OpenAudio( 44100, MIX_DEFAULT_FORMAT, 2, 4096 ) == -1 )
  {
    exception e;
    throw e;
  }

  initialised = true;
}

/**
 * Not thread safe.
 */
void MickSDLSound::playSound(string fileName)
{
  MickSoundFile sound;
  if(cachedSounds.count(fileName))
  {
    sound = cachedSounds.find(fileName)->second;
  }
  else
  {
    loadSound(fileName, &sound);
    cachedSounds.insert(std::pair<string, MickSoundFile>(fileName, sound));
  }

  if(initialised)
  {
    Mix_PlayChannel(-1, sound.sound, 0);
  }
}

void MickSDLSound::loadSound(string fileName, MickSoundFile* sound)
{
  if(initialised)
  {
    sound->sound = Mix_LoadWAV(fileName.c_str());
  }
}
