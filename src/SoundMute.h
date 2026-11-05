/*
 * SoundMute.h
 *
 * Silences the simulation's sound for a scope (brief 13): a late joiner fast-forwards a whole round's ticks in a few
 * milliseconds, and every explosion and fuse of it would otherwise play at once. The simulation plays through
 * MickBaseSound::getInstance(), so installing a silent backend for the scope is all it takes: no simulation source
 * changes. The previous backend is put back, never destroyed, when the scope ends.
 */
#ifndef _SOUNDMUTE_H
#define _SOUNDMUTE_H

#include <string>

#include "MickBaseSound.h"

class SoundMute : public MickBaseSound
{
  public:
    SoundMute() : m_previous(MickBaseSound::getInstance()) { setBaseInstance(this); }
    ~SoundMute() override { setBaseInstance(m_previous); }
    SoundMute(const SoundMute&) = delete;
    SoundMute& operator=(const SoundMute&) = delete;

    void initAudio() override {}
    void playSound(std::string) override {}

  private:
    MickBaseSound* m_previous;
};

#endif /* _SOUNDMUTE_H */
