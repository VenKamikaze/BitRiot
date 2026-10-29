/*
 * MickSDLSound.h
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#ifndef MICKSDLSOUND_H_
#define MICKSDLSOUND_H_

#include "MickBaseSound.h"
#include <SDL2/SDL_mixer.h>
#include <set>
#include <map>


struct MickSoundFile
{
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

    // How many sounds Mix_PlayChannel has accepted so far (a sound that failed to
    // load or found no free channel is not counted). Lets the web smoke test check
    // that audio reaches the mixer.
    unsigned long soundsPlayed() const { return m_soundsPlayed; }

  protected:
    void loadSound(std::string fileName, MickSoundFile* sound);
    void destructAudio();

  private:
    std::map<std::string, MickSoundFile> cachedSounds;
    unsigned long m_soundsPlayed = 0;

};

#endif /* MICKSDLSOUND_H_ */
