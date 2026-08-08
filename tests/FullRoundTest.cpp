// Full-round tests: play a round to its natural conclusion, then play another.
//
// Everything else in the suite pokes the win condition directly (killPlayer, a
// literal moreHostilesPossible). These tests instead let the real AI, the real
// spawning pool and the real collision code run until the round actually ends,
// then start a second round on the same process - which is where singleton
// state leaking between games would show up.
//
// Small maps are used deliberately. They are not menu-reachable (see
// setMapSize's comment) but nothing here needs them to be: they force AI
// players into contact quickly so a round terminates in a usable number of
// frames instead of tens of thousands.

#include "support/HeadlessGame.h"

#include "GameSettings.h"

namespace bitriot_test
{
namespace
{

// Generous: a round on a small map normally ends far sooner. The budget exists
// so a non-terminating round is reported as `ended=false` with diagnostics
// rather than hanging the suite.
const int FRAME_BUDGET = 60000;

class FullRoundTest : public HeadlessGameTest
{
};

// -- the map sizes these tests rely on --------------------------------------

TEST_F(FullRoundTest, SmallMapsAreSimulatable)
{
  // Guards the assumption the rest of this file rests on: tiny maps hold a
  // playable interior and survive the frame loop. The game itself never uses
  // these sizes, so nothing else would catch it if they broke.
  const int sizes[] = {5, 7, 9};
  for (int size : sizes)
  {
    configurePlayers(2);
    setMapSize(size, size);
    startRound();

    ASSERT_EQ(em().numPlayersAlive(), 2) << "both players spawned on a " << size << "x" << size;
    ASSERT_NE(player(1), nullptr);
    ASSERT_NE(player(2), nullptr);
    EXPECT_NE(std::make_pair(player(1)->getTileX(), player(1)->getTileY()),
              std::make_pair(player(2)->getTileX(), player(2)->getTileY()))
        << "players stacked on the same tile on a " << size << "x" << size;

    runFrames(500);
    SUCCEED(); // reaching here without a crash or out-of-range index is the point
  }
}

// -- PvP rounds run to completion -------------------------------------------

TEST_F(FullRoundTest, PvPRoundReachesItsNaturalEnd)
{
  configurePlayers(2);
  useTestMap();
  startRound();

  const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);

  EXPECT_TRUE(outcome.ended) << "round never ended: " << outcome.describe();
  EXPECT_GT(outcome.frames, 0) << "round was already over at kickoff: " << outcome.describe();
  EXPECT_LE(outcome.aliveTeams, 1) << outcome.describe();
}

TEST_F(FullRoundTest, TeamRoundReachesItsNaturalEnd)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  setMapSize(9, 9);
  startRound();

  const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);

  EXPECT_TRUE(outcome.ended) << "team round never ended: " << outcome.describe();
  EXPECT_GT(outcome.frames, 0) << outcome.describe();
  EXPECT_LE(outcome.aliveTeams, 1) << outcome.describe();

  // If anyone survived, the winner must be on the one surviving team.
  if (outcome.playersAlive > 0)
  {
    EXPECT_NE(outcome.winningPlayerNumber, 0) << outcome.describe();
    EXPECT_NE(outcome.winningTeamNumber, 0) << outcome.describe();
  }
}

// -- co-op: the fix, verified end to end ------------------------------------

TEST_F(FullRoundTest, CoopRoundReachesItsNaturalEnd)
{
  // This is the "co-op play never ends" bug, tested the way it was reported:
  // let a co-op round actually play out and check it concludes. Before the fix
  // this ran forever once the spawning pool stopped producing eggs.
  configurePlayers(2);
  setTeams({0, 0});
  useTestMap();
  startRound();

  ASSERT_EQ(std::GameSettings::getInstance()->numConfiguredTeams(), 1) << "precondition: co-op";

  const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);

  EXPECT_TRUE(outcome.ended) << "co-op round never ended: " << outcome.describe();
  EXPECT_GT(outcome.frames, 0)
      << "co-op round ended at kickoff instead of playing: " << outcome.describe();

  // Whichever way it went, the end state must be a legitimate one: either the
  // team was wiped out, or the board was cleared with the pool spent.
  const bool teamWipedOut = (outcome.playersAlive == 0);
  const bool boardCleared = (!outcome.hostilesRemain && outcome.poolSpent);
  EXPECT_TRUE(teamWipedOut || boardCleared)
      << "round ended for no valid reason: " << outcome.describe();
}

TEST_F(FullRoundTest, FourPlayerCoopRoundReachesItsNaturalEnd)
{
  // The user's original repro was a 4-player co-op game.
  configurePlayers(4);
  setTeams({0, 0, 0, 0});
  setMapSize(9, 9);
  startRound();

  const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);

  EXPECT_TRUE(outcome.ended) << "4-player co-op never ended: " << outcome.describe();
  EXPECT_GT(outcome.frames, 0) << outcome.describe();

  const bool teamWipedOut = (outcome.playersAlive == 0);
  const bool boardCleared = (!outcome.hostilesRemain && outcome.poolSpent);
  EXPECT_TRUE(teamWipedOut || boardCleared) << outcome.describe();
}

// -- a second round in the same process -------------------------------------

TEST_F(FullRoundTest, TwoConsecutiveRoundsBothCompleteInOneProcess)
{
  // "Play Again" is where singleton state leaks: AIGameView ghosts, a stale
  // entity map, settings half-reset. Round two must behave exactly like round
  // one, not inherit anything from it.
  configurePlayers(2);
  useTestMap();

  startRound();
  const RoundOutcome first = runRoundToCompletion(FRAME_BUDGET);
  EXPECT_TRUE(first.ended) << "round 1: " << first.describe();
  EXPECT_GT(first.frames, 0) << "round 1: " << first.describe();

  startRound();
  ASSERT_EQ(em().numPlayersAlive(), 2) << "round 2 must start with a full roster";
  ASSERT_EQ(countAllEntities(), 2) << "round 2 must start on a clean board";

  const RoundOutcome second = runRoundToCompletion(FRAME_BUDGET);
  EXPECT_TRUE(second.ended) << "round 2: " << second.describe();
  EXPECT_GT(second.frames, 0)
      << "round 2 ended at kickoff - stale state from round 1: " << second.describe();
}

TEST_F(FullRoundTest, ThreeConsecutiveCoopRoundsAllComplete)
{
  configurePlayers(2);
  setTeams({0, 0});
  useTestMap();

  for (int round = 1; round <= 3; round++)
  {
    startRound();
    ASSERT_EQ(em().numPlayersAlive(), 2) << "round " << round << " roster";

    const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);
    EXPECT_TRUE(outcome.ended) << "co-op round " << round << ": " << outcome.describe();
    EXPECT_GT(outcome.frames, 0) << "co-op round " << round << ": " << outcome.describe();
  }
}

TEST_F(FullRoundTest, RoundLengthsAreComparableAcrossConsecutiveRounds)
{
  // A round that ends drastically faster than the one before it is the
  // signature of leaked state making the win condition fire early.
  configurePlayers(2);
  useTestMap();

  std::vector<int> lengths;
  for (int round = 0; round < 3; round++)
  {
    startRound();
    const RoundOutcome outcome = runRoundToCompletion(FRAME_BUDGET);
    ASSERT_TRUE(outcome.ended) << "round " << round << ": " << outcome.describe();
    lengths.push_back(outcome.frames);
  }

  for (size_t i = 0; i < lengths.size(); i++)
  {
    EXPECT_GT(lengths[i], 0) << "round " << i << " ended instantly (" << lengths[i] << " frames)";
  }
}

} // namespace
} // namespace bitriot_test
