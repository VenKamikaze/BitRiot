// GameSettings tests.
//
// GameSettings is the canonical model behind the player-select and options
// menus: player count, per-slot human/AI, alliance teams, device attachment and
// map size. Several of its rules are load-bearing for 6-player play (the map
// must be tall enough to show every portrait) and for team play (the 0-based
// slot team vs the 1-based entity team number).

#include "support/HeadlessGame.h"

#include "GameSettings.h"

namespace bitriot_test
{
namespace
{

using std::GameSettings;

class GameSettingsTest : public HeadlessGameTest
{
  protected:
    GameSettings* settings() { return GameSettings::getInstance(); }
};

// -- player count -----------------------------------------------------------

TEST_F(GameSettingsTest, PlayerCountAcceptsTheSupportedRange)
{
  for (int n = GameSettings::MIN_PLAYERS; n <= GameSettings::MAX_PLAYERS; n++)
  {
    settings()->setNumberOfPlayers(n);
    EXPECT_EQ(settings()->getNumberOfPlayers(), n);
    EXPECT_EQ((int) settings()->getPlayers()->size(), n);
  }
}

TEST_F(GameSettingsTest, PlayerCountOutsideTheSupportedRangeIsRejected)
{
  settings()->setNumberOfPlayers(4);

  settings()->setNumberOfPlayers(GameSettings::MIN_PLAYERS - 1);
  EXPECT_EQ(settings()->getNumberOfPlayers(), 4) << "too few players must be ignored";

  settings()->setNumberOfPlayers(GameSettings::MAX_PLAYERS + 1);
  EXPECT_EQ(settings()->getNumberOfPlayers(), 4) << "too many players must be ignored";
}

TEST_F(GameSettingsTest, GrowingTheRosterPreservesExistingSlots)
{
  configurePlayers(4);
  settings()->setPlayerAI(1, false);
  settings()->setPlayerController(1, 7);

  settings()->setNumberOfPlayers(6);

  EXPECT_FALSE(settings()->getPlayerAIs()->at(1)) << "existing slot must keep its human flag";
  EXPECT_EQ(settings()->getPlayer(1)->attachedControllerId, 7)
      << "existing slot must keep its device assignment";
  EXPECT_EQ(settings()->getNumberOfPlayers(), 6);
}

TEST_F(GameSettingsTest, ShrinkingTheRosterResetsNowInvalidTeams)
{
  // A team number must always be < the player count. Slot 1 is put on team 4,
  // which stops existing when the roster shrinks to 3.
  configurePlayers(6);
  settings()->setPlayerTeam(1, 4);
  ASSERT_EQ(settings()->getPlayer(1)->team, 4);

  settings()->setNumberOfPlayers(3);

  EXPECT_LT(settings()->getPlayer(1)->team, 3);
  EXPECT_EQ(settings()->getPlayer(1)->team, settings()->getPlayer(1)->id)
      << "an invalidated team falls back to the player's own slot (free-for-all)";
}

TEST_F(GameSettingsTest, HumanCountTracksThePerSlotAiFlags)
{
  configurePlayers(4);
  EXPECT_EQ(settings()->getNumberOfHumanPlayers(), 0) << "the harness defaults every slot to AI";

  settings()->setPlayerAI(0, false);
  settings()->setPlayerAI(2, false);
  EXPECT_EQ(settings()->getNumberOfHumanPlayers(), 2);
}

// -- alliance teams ---------------------------------------------------------

TEST_F(GameSettingsTest, EntityTeamNumberIsTheOneBasedFormOfTheSlotTeam)
{
  // Slots are 0-based and entity playerNumbers are 1-based; team 0 is reserved
  // for the CPU. So slot team t maps to entity team number t + 1.
  configurePlayers(4);
  setTeams({0, 0, 1, 1});

  EXPECT_EQ(settings()->teamNumberForPlayer(1), 1);
  EXPECT_EQ(settings()->teamNumberForPlayer(2), 1);
  EXPECT_EQ(settings()->teamNumberForPlayer(3), 2);
  EXPECT_EQ(settings()->teamNumberForPlayer(4), 2);
}

TEST_F(GameSettingsTest, CpuAndOutOfRangePlayersAreNeutralTeamZero)
{
  configurePlayers(4);

  EXPECT_EQ(settings()->teamNumberForPlayer(0), 0) << "0 is the CPU/neutral owner";
  EXPECT_EQ(settings()->teamNumberForPlayer(-1), 0);
  EXPECT_EQ(settings()->teamNumberForPlayer(5), 0) << "beyond the roster is neutral, not a crash";
  EXPECT_EQ(settings()->teamNumberForPlayer(99), 0);
}

TEST_F(GameSettingsTest, ConfiguredTeamCountDistinguishesCoopFromPvP)
{
  configurePlayers(4);

  setTeams({0, 1, 2, 3});
  EXPECT_EQ(settings()->numConfiguredTeams(), 4) << "free-for-all";

  setTeams({0, 0, 1, 1});
  EXPECT_EQ(settings()->numConfiguredTeams(), 2) << "two pairs";

  setTeams({0, 0, 0, 1});
  EXPECT_EQ(settings()->numConfiguredTeams(), 2) << "3 v 1 is still PvP";

  setTeams({0, 0, 0, 0});
  EXPECT_EQ(settings()->numConfiguredTeams(), 1) << "everyone allied is co-op";
}

TEST_F(GameSettingsTest, TeamCountCountsDistinctTeamsNotContiguousOnes)
{
  // Nothing requires team ids to be dense; the menu can leave gaps.
  configurePlayers(4);
  setTeams({0, 3, 0, 3});
  EXPECT_EQ(settings()->numConfiguredTeams(), 2);
}

// -- map size and the start gate --------------------------------------------

TEST_F(GameSettingsTest, RequiredMapHeightGrowsWithPlayerCountAndStaysOdd)
{
  for (int n = GameSettings::MIN_PLAYERS; n <= GameSettings::MAX_PLAYERS; n++)
  {
    const int needed = settings()->requiredMapHeightFor(n);
    EXPECT_GE(needed, 3 * n) << "must fit " << n << " stacked portraits";
    EXPECT_EQ(needed % 2, 1) << "map dimensions are always odd";
  }
}

TEST_F(GameSettingsTest, UpToFourPlayersCanStartOnAnyMapHeight)
{
  // Only slots beyond MAX_KEYBOARD_PLAYERS are gated on map height.
  configurePlayers(4);
  settings()->setMapHeight(GameSettings::MIN_MAP_HEIGHT);
  EXPECT_TRUE(settings()->canStartGame());
}

TEST_F(GameSettingsTest, SixPlayersNeedATallEnoughMapToStart)
{
  configurePlayers(6);
  // Slots 5 and 6 are controller-only and would otherwise trip the
  // undrivable-human check; the harness leaves them AI, which is fine.
  const int needed = settings()->requiredMapHeightFor(6);

  settings()->setMapHeight(needed - GameSettings::MAP_SIZE_STEP);
  EXPECT_FALSE(settings()->canStartGame()) << "too short to show six portraits";

  settings()->setMapHeight(needed);
  EXPECT_TRUE(settings()->canStartGame());
}

TEST_F(GameSettingsTest, AControllerOnlyHumanWithNoDeviceBlocksTheStart)
{
  // Slot index >= MAX_KEYBOARD_PLAYERS cannot be driven by the keyboard, so a
  // human there with neither a pad nor a keyset would sit frozen all round.
  configurePlayers(6);
  settings()->setMapHeight(settings()->requiredMapHeightFor(6));
  ASSERT_TRUE(settings()->canStartGame());

  settings()->setPlayerAI(GameSettings::MAX_KEYBOARD_PLAYERS, false);

  EXPECT_TRUE(settings()->hasUndrivableHuman());
  EXPECT_FALSE(settings()->canStartGame());

  settings()->setPlayerController(GameSettings::MAX_KEYBOARD_PLAYERS, 0);
  EXPECT_FALSE(settings()->hasUndrivableHuman()) << "a pad makes the slot drivable";
  EXPECT_TRUE(settings()->canStartGame());
}

TEST_F(GameSettingsTest, AKeyboardSlotHumanNeverBlocksTheStart)
{
  configurePlayers(4);
  settings()->setPlayerAI(0, false);
  settings()->setPlayerAI(3, false);

  EXPECT_FALSE(settings()->hasUndrivableHuman())
      << "slots below MAX_KEYBOARD_PLAYERS always have a keyboard set available";
  EXPECT_TRUE(settings()->canStartGame());
}

// -- options ----------------------------------------------------------------

TEST_F(GameSettingsTest, NpcSpawnerToggleRoundTrips)
{
  EXPECT_TRUE(settings()->isNpcSpawnerEnabled()) << "default is on";
  settings()->setNpcSpawnerEnabled(false);
  EXPECT_FALSE(settings()->isNpcSpawnerEnabled());
}

} // namespace
} // namespace bitriot_test
