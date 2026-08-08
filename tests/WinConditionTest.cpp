// Win-condition tests.
//
// These cover EntityManager::oneOrZeroPlayersRemain() and its helpers, which
// decide when a round ends. Two distinct rules live there:
//
//   PvP  (>= 2 configured teams): ends when <= 1 alliance team is still alive.
//   Co-op (1 configured team):    ends when the team is wiped out, OR the board
//                                 is clear of hostile NPCs and the spawning
//                                 pool can produce no more.
//
// The co-op branch is the fix for "co-op play never ends": before it, a round
// where the pool exhausted its points ran forever because nothing was left to
// kill the survivors.

#include "support/HeadlessGame.h"

#include "GameSettings.h"

namespace bitriot_test
{
namespace
{

class WinConditionTest : public HeadlessGameTest
{
};

// -- free-for-all -----------------------------------------------------------

TEST_F(WinConditionTest, FreeForAllRoundContinuesWhileTwoPlayersLive)
{
  configurePlayers(4);
  startRound();

  EXPECT_EQ(em().numPlayersAlive(), 4);
  EXPECT_EQ(em().numAliveTeams(), 4);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));

  killPlayer(1);
  killPlayer(2);

  EXPECT_EQ(em().numPlayersAlive(), 2);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));
}

TEST_F(WinConditionTest, FreeForAllRoundEndsWithOneSurvivor)
{
  configurePlayers(4);
  startRound();

  killPlayer(1);
  killPlayer(2);
  killPlayer(4);

  EXPECT_EQ(em().numPlayersAlive(), 1);
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
  ASSERT_NE(em().getWinningPlayer(true), nullptr);
  EXPECT_EQ(em().getWinningPlayer(true)->getPlayerNumber(), 3);
}

TEST_F(WinConditionTest, FreeForAllMutualDestructionIsADraw)
{
  configurePlayers(2);
  startRound();

  killPlayer(1);
  killPlayer(2);

  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
  EXPECT_EQ(em().getWinningPlayer(true), nullptr) << "nobody left alive, so no winner";
}

// -- team play --------------------------------------------------------------

TEST_F(WinConditionTest, TeamRoundContinuesWhileBothTeamsHaveASurvivor)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  EXPECT_EQ(em().numAliveTeams(), 2);

  // One player down on each side: both teams still stand.
  killPlayer(1);
  killPlayer(3);

  EXPECT_EQ(em().numPlayersAlive(), 2);
  EXPECT_EQ(em().numAliveTeams(), 2);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true))
      << "two players alive on opposing teams must not end the round";
}

TEST_F(WinConditionTest, TeamRoundEndsWhenOneTeamIsWipedOut)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  // Wipe out team 1 (slots 2 and 3, i.e. playerNumbers 3 and 4).
  killPlayer(3);
  killPlayer(4);

  EXPECT_EQ(em().numPlayersAlive(), 2) << "the winning team still has two players";
  EXPECT_EQ(em().numAliveTeams(), 1);
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
}

TEST_F(WinConditionTest, TwoSurvivingTeammatesDoNotEndTheRoundPrematurely)
{
  // Regression guard for the pre-team-play behaviour, where the round ended as
  // soon as a single player head remained. With alliances the count that
  // matters is teams, not players.
  configurePlayers(6);
  setTeams({0, 0, 0, 1, 1, 1});
  startRound();

  EXPECT_EQ(em().numAliveTeams(), 2);

  killPlayer(2);
  killPlayer(3);
  killPlayer(5);
  killPlayer(6);

  EXPECT_EQ(em().numPlayersAlive(), 2);
  EXPECT_EQ(em().numAliveTeams(), 2);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));

  killPlayer(4);
  EXPECT_EQ(em().numAliveTeams(), 1);
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
}

TEST_F(WinConditionTest, UnevenTeamsEndWhenTheLastMemberOfOneSideDies)
{
  configurePlayers(5);
  setTeams({0, 0, 0, 0, 1}); // 4 v 1
  startRound();

  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));

  killPlayer(5);

  EXPECT_EQ(em().numPlayersAlive(), 4);
  EXPECT_EQ(em().numAliveTeams(), 1);
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
}

// -- co-op (a single configured team) ---------------------------------------

TEST_F(WinConditionTest, CoopIsDetectedAsASingleConfiguredTeam)
{
  configurePlayers(4);
  setTeams({0, 0, 0, 0});
  EXPECT_EQ(std::GameSettings::getInstance()->numConfiguredTeams(), 1);
}

TEST_F(WinConditionTest, CoopRoundContinuesWhileHostilesCanStillSpawn)
{
  configurePlayers(4);
  setTeams({0, 0, 0, 0});
  startRound();

  // Everyone alive, everyone allied: numAliveTeams() is 1, which under the PvP
  // rule would have ended the round at kickoff. The co-op branch must not.
  EXPECT_EQ(em().numAliveTeams(), 1);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(/*moreHostilesPossible=*/true));
}

TEST_F(WinConditionTest, CoopRoundEndsWhenTheWholeTeamIsWipedOut)
{
  configurePlayers(3);
  setTeams({0, 0, 0});
  startRound();

  killPlayer(1);
  killPlayer(2);
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(true));

  killPlayer(3);
  EXPECT_TRUE(em().allPlayersDead());
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
}

TEST_F(WinConditionTest, CoopRoundEndsWhenBoardIsClearedAndPoolIsExhausted)
{
  // The "co-op never ends" bug: survivors, no hostiles left, pool spent.
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  EXPECT_EQ(countEntities(EGG_GROUP), 0);
  EXPECT_FALSE(em().anyHostileNpcsAlive());

  EXPECT_TRUE(em().oneOrZeroPlayersRemain(/*moreHostilesPossible=*/false))
      << "board clear and pool exhausted means the co-op team has won";
}

TEST_F(WinConditionTest, CoopRoundContinuesWhileAHostileEggRemains)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  // A CPU-owned egg: playerNumber 0.
  spawnEntity(EGG1, 0, 5, 5);

  EXPECT_TRUE(em().anyHostileNpcsAlive());
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(/*moreHostilesPossible=*/false))
      << "pool is spent, but a live hostile is still on the board";
}

TEST_F(WinConditionTest, CoopRoundContinuesWhileAHostileRobotRemains)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  spawnEntity(ROBOT1, 0, 5, 5);

  EXPECT_EQ(countEntities(ROBOT_GROUP), 1);
  EXPECT_TRUE(em().anyHostileNpcsAlive());
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(false));
}

TEST_F(WinConditionTest, CoopRoundContinuesWhileAHostileMonsterRemains)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  // EGG_THIEF / WALL_EATER / DRONE are PLAYER_SPAWN_GROUP - the creatures eggs
  // hatch into, not spawn points.
  spawnEntity(DRONE, 0, 5, 5);

  EXPECT_EQ(countEntities(PLAYER_SPAWN_GROUP), 1);
  EXPECT_TRUE(em().anyHostileNpcsAlive());
  EXPECT_FALSE(em().oneOrZeroPlayersRemain(false));
}

TEST_F(WinConditionTest, PlayerLaidEggsAreNotCountedAsHostiles)
{
  // The discriminating case for anyHostileNpcsAlive(): eggs laid by a player
  // carry that player's number (players are 1..N, the CPU is 0). If they were
  // treated as hostiles, a co-op team that had laid eggs could never win.
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  spawnEntity(EGG1, 1, 5, 5);
  spawnEntity(EGG3, 2, 6, 5);

  EXPECT_EQ(countEntities(EGG_GROUP), 2);
  EXPECT_EQ(countEntities(EGG_GROUP, 0), 0) << "neither egg belongs to the CPU";
  EXPECT_FALSE(em().anyHostileNpcsAlive());
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(false))
      << "the team's own eggs must not keep the round alive";
}

TEST_F(WinConditionTest, PlayerHatchedMonstersAreNotCountedAsHostiles)
{
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  spawnEntity(EGG_THIEF, 1, 5, 5);
  spawnEntity(ROBOT1, 2, 6, 5);

  EXPECT_FALSE(em().anyHostileNpcsAlive());
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(false));
}

TEST_F(WinConditionTest, NeutralPowerupsAreNotCountedAsHostiles)
{
  // Power-ups also carry playerNumber 0 but belong to POWERUP_GROUP, so they
  // must not block a co-op win.
  configurePlayers(2);
  setTeams({0, 0});
  startRound();

  spawnEntity(ENPLUS_POWERUP, 0, 5, 5);

  EXPECT_EQ(countEntities(POWERUP_GROUP), 1);
  EXPECT_FALSE(em().anyHostileNpcsAlive());
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(false));
}

TEST_F(WinConditionTest, MoreHostilesPossibleIsIgnoredInPvP)
{
  // The flag only gates the co-op branch. A PvP round's outcome must not
  // depend on the spawning pool's remaining budget.
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  killPlayer(3);
  killPlayer(4);

  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(false));
}

} // namespace
} // namespace bitriot_test
