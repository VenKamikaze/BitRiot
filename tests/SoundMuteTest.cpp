// SoundMute (brief 13): the late joiner's fast-forward plays no sound, and the real backend is back afterwards.
#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include "SoundMute.h"

namespace bitriot_test
{
TEST(SoundMuteTest, SilencesWhatTheSimulationPlaysForAScopeAndPutsTheBackendBack)
{
  // The harness's recording backend stands in for the game's MickSDLSound.
  NullSound* backendPtr = dynamic_cast<NullSound*>(MickBaseSound::getInstance());
  ASSERT_NE(backendPtr, nullptr);
  NullSound& backend = *backendPtr;
  backend.clearSoundLog();
  MickBaseSound::getInstance()->playSound("before");
  const size_t before = backend.soundLog().size();
  ASSERT_EQ(before, 1u);
  {
    SoundMute quiet;
    EXPECT_NE(MickBaseSound::getInstance(), &backend);
    MickBaseSound::getInstance()->playSound("during");
  }
  EXPECT_EQ(MickBaseSound::getInstance(), &backend) << "restored, not left silent";
  EXPECT_EQ(backend.soundLog().size(), before) << "nothing was played during the scope";
  MickBaseSound::getInstance()->playSound("after");
  EXPECT_EQ(backend.soundLog().size(), before + 1);
}
}
