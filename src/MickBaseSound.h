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

    static MickBaseSound* getInstance();

    virtual void initAudio() = 0;
    virtual void playSound(std::string fileName) = 0;
    //virtual void loadSound(string fileName) = 0; // implement this for pre-caching

};

#endif /* MICKBASESOUND_H_ */
