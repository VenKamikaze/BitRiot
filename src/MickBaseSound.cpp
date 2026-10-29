/*
 * MickBaseSound.cpp
 *
 *  Created on: Mar 10, 2012
 *      Author: msaun
 */

#include "MickBaseSound.h"

static MickBaseSound *s_base_instance = 0;

namespace
{
// What the simulation talks to when no audio backend is installed (a headless
// run, a test, the web build before audio is set up): every sound request is
// dropped. The game installs the real backend (MickSDLSound registers itself).
class SilentSound : public MickBaseSound
{
  public:
    void initAudio() override {}
    void playSound(std::string) override {}
};
}

MickBaseSound::MickBaseSound()
{
  initialised = false;
}

void MickBaseSound::setBaseInstance(MickBaseSound* base_inst)
{
  s_base_instance = base_inst;
}

MickBaseSound* MickBaseSound::getInstance()
{
  if (s_base_instance)
  {
    return s_base_instance;
  }
  static SilentSound silent;
  return &silent;
}

MickBaseSound::~MickBaseSound()
{
  // A backend that goes away must not stay registered: the simulation would play
  // into freed memory. It falls back to the silent default instead.
  if (s_base_instance == this)
  {
    s_base_instance = 0;
  }
}
