/*
 * MickBaseSound.h
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#ifndef MICKBASESOUND_H_
#define MICKBASESOUND_H_

#include <string>

class MickBaseSound
{
  protected:
    bool initialised;
    void setBaseInstance(MickBaseSound* backend);

  public:
    MickBaseSound();
    virtual ~MickBaseSound();

    // The installed backend, or - if none is installed - a built-in silent one, so
    // simulation code can play sounds without checking and without linking SDL_mixer.
    // Never null.
    static MickBaseSound* getInstance();

    virtual void initAudio() = 0;
    virtual void playSound(std::string fileName) = 0;
    //virtual void loadSound(string fileName) = 0; // implement this for pre-caching

};

#endif /* MICKBASESOUND_H_ */
