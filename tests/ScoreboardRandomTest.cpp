// The scoreboard's presentation randomness (brief 02), in the executable that links
// the UI. The scoreboard binder is game code, not simulation, so since brief 03b
// this test lives apart from SimRandomTest.cpp (which is built into the
// simulation-only executable too). The body is unchanged.

#include <gtest/gtest.h>

#include "support/HeadlessGame.h"

#include "RmlUI/binders/ScoreBoardBinder.h"
#include "SimRandom.h"

namespace bitriot_test
{
namespace
{

class ScoreboardRandomTest : public HeadlessGameTest
{
};

TEST_F(ScoreboardRandomTest, TheScoreboardWinTextDrawsOneCosmeticValueAndNoSimValue)
{
  configurePlayers(2);
  useTestMap();
  startRound();

  SimRandom simBefore = SimRandom::sim();
  SimRandom cosmeticBefore = SimRandom::cosmetic();

  std::ScoreBoardBinder::mapScoreBoardData(player(1), em().getRoundPlayerRoster(), 1);

  EXPECT_EQ(SimRandom::sim().nextU32(), simBefore.nextU32());
  cosmeticBefore.nextU32();
  EXPECT_EQ(SimRandom::cosmetic().nextU32(), cosmeticBefore.nextU32());
}

} // namespace
} // namespace bitriot_test
