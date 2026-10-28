// Round roster tests.
//
// EntityManager erases a dead player character from its live entity list, so
// getAllPlayerCharacters() only ever returns survivors. The end-of-round
// scoreboard needs everyone who played, which is what getRoundPlayerRoster()
// provides. These tests pin both halves: the roster keeps the dead, and the
// live-set semantics the win conditions rely on are untouched.
//
// destroyPlayer() (not killPlayer()) is used throughout: killPlayer() only
// flips the alive flag, whereas the real death path posts DESTROY_ENTITY and
// removes the entity from the list - the case that caused the bug.

#include "support/HeadlessGame.h"

#include <algorithm>
#include <set>

#include "GameSettings.h"

namespace bitriot_test
{
namespace
{

class RoundRosterTest : public HeadlessGameTest
{
  protected:
    std::shared_ptr<PlayerCharacterEntity> rosterEntry(int playerNumber)
    {
      for (const auto& p : em().getRoundPlayerRoster())
      {
        if (p->getPlayerNumber() == playerNumber)
        {
          return p;
        }
      }
      return nullptr;
    }
};

TEST_F(RoundRosterTest, RosterHoldsEveryPlayerFromTheStartOfTheRound)
{
  configurePlayers(4);
  startRound();

  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 4);
  for (int i = 1; i <= 4; i++)
  {
    ASSERT_NE(rosterEntry(i), nullptr) << "player " << i << " missing from roster";
    EXPECT_TRUE(rosterEntry(i)->isAlive());
  }
}

TEST_F(RoundRosterTest, RosterKeepsDeadPlayersWhileLiveSetDropsThem)
{
  configurePlayers(4);
  startRound();

  destroyPlayer(1);
  destroyPlayer(2);
  destroyPlayer(4);

  // The live set is what win conditions read: only the survivor remains.
  EXPECT_EQ(em().getAllPlayerCharacters()->size(), 1u);
  EXPECT_EQ(em().numPlayersAlive(), 1);
  EXPECT_TRUE(em().oneOrZeroPlayersRemain(true));
  ASSERT_NE(em().getWinningPlayer(true), nullptr);
  EXPECT_EQ(em().getWinningPlayer(true)->getPlayerNumber(), 3);

  // The roster still has all four, with correct alive flags.
  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 4);
  for (int i = 1; i <= 4; i++)
  {
    ASSERT_NE(rosterEntry(i), nullptr) << "player " << i;
    EXPECT_EQ(rosterEntry(i)->isAlive(), i == 3) << "player " << i;
  }
}

TEST_F(RoundRosterTest, EveryoneDeadStillListsEveryone)
{
  configurePlayers(3);
  startRound();

  destroyPlayer(1);
  destroyPlayer(2);
  destroyPlayer(3);

  EXPECT_EQ(em().getAllPlayerCharacters()->size(), 0u);
  EXPECT_TRUE(em().allPlayersDead());
  EXPECT_EQ((int) em().getRoundPlayerRoster().size(), 3);
}

TEST_F(RoundRosterTest, DeadPlayersKeepTheirStatsAndIdentity)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  // Distinct stats per player so a mix-up between entries is detectable.
  for (int i = 1; i <= 4; i++)
  {
    for (int e = 0; e < i; e++)
    {
      player(i)->incNumberEggsPlaced();
    }
    for (int b = 0; b < i * 2; b++)
    {
      player(i)->incNumberBombsPlaced();
    }
  }

  destroyPlayer(2);
  destroyPlayer(3);
  destroyPlayer(4);

  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 4);
  for (int i = 1; i <= 4; i++)
  {
    std::shared_ptr<PlayerCharacterEntity> p = rosterEntry(i);
    ASSERT_NE(p, nullptr) << "player " << i;
    EXPECT_EQ(p->getPlayerNumber(), i);
    EXPECT_EQ(p->getNumberEggsPlaced(), i) << "eggs, player " << i;
    EXPECT_EQ(p->getNumberBombsPlaced(), i * 2) << "bombs, player " << i;
    // 0-based slot teams {0,0,1,1} map to 1-based team numbers {1,1,2,2}.
    EXPECT_EQ(p->getTeamNumber(), i <= 2 ? 1 : 2) << "team, player " << i;
    EXPECT_EQ(p->isAlive(), i == 1) << "alive, player " << i;
  }
}

TEST_F(RoundRosterTest, DuplicateDestroyMessagesDoNotAffectTheRoster)
{
  configurePlayers(2);
  startRound();

  const int id = player(1)->getID();
  EntityMessageQueue::getInstance()->postDestroyMessage(id);
  EntityMessageQueue::getInstance()->postDestroyMessage(id);
  em().processMessageQueue();

  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 2);
  std::set<int> ids;
  for (const auto& p : em().getRoundPlayerRoster())
  {
    ids.insert(p->getID());
  }
  EXPECT_EQ((int) ids.size(), 2) << "roster entries must be distinct";
}

TEST_F(RoundRosterTest, RosterOnlyContainsPlayerCharacters)
{
  configurePlayers(2);
  startRound();

  // Eggs, bombs etc. are created through the same message path.
  spawnEntity(EGG1, 0, 3, 3);
  spawnEntity(BOMB, 1, 5, 5);

  EXPECT_EQ((int) em().getRoundPlayerRoster().size(), 2);
  for (const auto& p : em().getRoundPlayerRoster())
  {
    EXPECT_EQ(p->getType(), PLAYER_CHARACTER);
  }
}

TEST_F(RoundRosterTest, DeadPlayersAreNotSimulatedOrKeptOnTheMap)
{
  configurePlayers(2);
  startRound();

  destroyPlayer(2);

  runFrames(50);

  // Holding the pointer in the roster must not resurrect the entity in game
  // logic: it stays out of the entity list and out of the player lookup.
  EXPECT_EQ(em().getPlayerPointer(2), nullptr);
  EXPECT_EQ(countEntities(CHARACTER_GROUP, 2), 0);
  EXPECT_FALSE(rosterEntry(2)->isAlive());
}

TEST_F(RoundRosterTest, RosterResetsBetweenRounds)
{
  configurePlayers(4);
  startRound();
  destroyPlayer(1);
  destroyPlayer(2);
  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 4);

  // "Play again" with a smaller field: nobody from round one may leak in.
  configurePlayers(2);
  startRound();

  ASSERT_EQ((int) em().getRoundPlayerRoster().size(), 2);
  for (const auto& p : em().getRoundPlayerRoster())
  {
    EXPECT_TRUE(p->isAlive()) << "player " << p->getPlayerNumber() << " carried over dead";
    EXPECT_LE(p->getPlayerNumber(), 2);
  }
  EXPECT_EQ(em().getAllPlayerCharacters()->size(), 2u);
}

TEST_F(RoundRosterTest, RosterCoversMaximumPlayerCount)
{
  const int maxPlayers = std::GameSettings::MAX_PLAYERS;
  configurePlayers(maxPlayers);
  setMapSize(17, std::GameSettings::getInstance()->requiredMapHeightFor(maxPlayers));
  startRound();

  for (int i = 2; i <= maxPlayers; i++)
  {
    destroyPlayer(i);
  }

  EXPECT_EQ((int) em().getRoundPlayerRoster().size(), maxPlayers);
  EXPECT_EQ(em().numPlayersAlive(), 1);
}

} // namespace
} // namespace bitriot_test
