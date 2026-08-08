// Entity simulation tests.
//
// These drive the real EntityManager frame loop (AI + message queue +
// collisions) and check the bookkeeping that the rest of the game depends on:
// entity creation, the AIGameView shadow map that AI targeting reads from, and
// per-entity team identity.
//
// AIGameView gets particular attention. It is a process-lifetime singleton that
// mirrors entity positions for AI lookups, and stale entries in it were the
// cause of the "spinning rockets fire at nothing" bug: DynamicMap is rebuilt
// from scratch each round without posting destroy messages, so anything left in
// AIGameView became a permanent untargetable ghost.

#include "support/HeadlessGame.h"

#include "AIGameView.h"
#include "EntityMessageQueue.h"
#include "GameSettings.h"
#include "Map.h"

namespace bitriot_test
{
namespace
{

class EntitySimTest : public HeadlessGameTest
{
  protected:
    // Total EntityInfo records AIGameView is holding across the whole map.
    int aiGameViewEntryCount(int width, int height)
    {
      int count = 0;
      for (int x = 0; x < width; x++)
      {
        for (int y = 0; y < height; y++)
        {
          count += (int) AIGameView::getInstance()->getEntityInfoAtLocation(x, y).size();
        }
      }
      return count;
    }

    // The ids DynamicMap is keyed by; there is no accessor for an entity's own
    // id, but the map key is it.
    std::vector<int> liveEntityIds()
    {
      std::vector<int> ids;
      DynamicMap* map = em().getDynamicMap();
      for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
      {
        ids.push_back(it->first);
      }
      return ids;
    }
};

// -- round setup ------------------------------------------------------------

TEST_F(EntitySimTest, EveryConfiguredPlayerGetsALiveCharacter)
{
  configurePlayers(6);
  setMapSize(17, std::GameSettings::getInstance()->requiredMapHeightFor(6));
  startRound();

  for (int i = 1; i <= 6; i++)
  {
    std::shared_ptr<PlayerCharacterEntity> p = player(i);
    ASSERT_NE(p, nullptr) << "no character for player " << i;
    EXPECT_TRUE(p->isAlive());
    EXPECT_EQ(p->getPlayerNumber(), i);
  }
  EXPECT_EQ(em().numPlayersAlive(), 6);
}

TEST_F(EntitySimTest, PlayersStartOnDistinctTiles)
{
  configurePlayers(6);
  setMapSize(17, std::GameSettings::getInstance()->requiredMapHeightFor(6));
  startRound();

  std::set<std::pair<int, int>> tiles;
  for (int i = 1; i <= 6; i++)
  {
    std::shared_ptr<PlayerCharacterEntity> p = player(i);
    ASSERT_NE(p, nullptr);
    tiles.insert(std::make_pair(p->getTileX(), p->getTileY()));
  }
  EXPECT_EQ((int) tiles.size(), 6) << "players must not be stacked on the same tile";
}

TEST_F(EntitySimTest, PlayerCharactersAreNotHostileNpcs)
{
  configurePlayers(4);
  startRound();

  EXPECT_EQ(countEntities(CHARACTER_GROUP), 4);
  EXPECT_FALSE(em().anyHostileNpcsAlive())
      << "player characters are CHARACTER_GROUP, never hostiles";
}

// -- the frame loop ---------------------------------------------------------

TEST_F(EntitySimTest, SimulationRunsManyFramesWithoutLosingPlayers)
{
  configurePlayers(4);
  startRound();

  runFrames(300);

  // AI players roam, lay eggs and fight, so the exact survivor count is not
  // deterministic; what must hold is that the manager is still coherent.
  EXPECT_LE(em().numPlayersAlive(), 4);
  EXPECT_GE(em().numPlayersAlive(), 0);
  EXPECT_GT(countAllEntities(), 0);
}

TEST_F(EntitySimTest, SixPlayerRoundSimulatesOnATallMap)
{
  // 6-player play needs a taller map; make sure the simulation is happy there
  // and nothing indexes off the end of the resized maps.
  configurePlayers(6);
  const int height = std::GameSettings::getInstance()->requiredMapHeightFor(6);
  setMapSize(17, height);
  startRound();

  ASSERT_EQ(em().numPlayersAlive(), 6);
  runFrames(200);

  EXPECT_LE(em().numPlayersAlive(), 6);
  EXPECT_GT(countAllEntities(), 0);
}

// -- AIGameView bookkeeping -------------------------------------------------

TEST_F(EntitySimTest, AIGameViewSeesNewlyCreatedEntities)
{
  configurePlayers(2);
  startRound();

  const int before = (int) AIGameView::getInstance()->getEntityInfoAtLocation(5, 5).size();
  spawnEntity(EGG1, 0, 5, 5);
  const std::vector<EntityInfo>& at = AIGameView::getInstance()->getEntityInfoAtLocation(5, 5);

  ASSERT_EQ((int) at.size(), before + 1);
  EXPECT_EQ(at.back().groupType, EGG_GROUP);
  EXPECT_EQ(at.back().playerNumber, 0);
}

TEST_F(EntitySimTest, AIGameViewDropsDestroyedEntities)
{
  configurePlayers(2);
  startRound();

  spawnEntity(EGG1, 0, 5, 5);
  ASSERT_EQ((int) AIGameView::getInstance()->getEntityInfoAtLocation(5, 5).size(), 1);

  // Find the egg's id (the DynamicMap key) and destroy it the way the game does.
  int eggId = -1;
  DynamicMap* map = em().getDynamicMap();
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getGroupType() == EGG_GROUP)
    {
      eggId = it->first;
      break;
    }
  }
  ASSERT_NE(eggId, -1);

  EntityMessageQueue::getInstance()->postDestroyMessage(eggId);
  em().processMessageQueue();

  EXPECT_EQ(countEntities(EGG_GROUP), 0);
  EXPECT_TRUE(AIGameView::getInstance()->getEntityInfoAtLocation(5, 5).empty())
      << "a destroyed entity must not linger in the AI's view of the map";
}

TEST_F(EntitySimTest, AIGameViewIsClearedWhenANewRoundStarts)
{
  // Regression guard for the "ghost rocket targets" bug. DynamicMap is thrown
  // away and rebuilt each round without destroy messages, so AIGameView must be
  // reset by DynamicMap's constructor - otherwise last round's entities stay
  // visible to AI targeting forever.
  configurePlayers(4);
  startRound();

  // Populate the board well beyond the starting characters.
  for (int x = 3; x <= 9; x++)
  {
    spawnEntity(EGG1, 0, x, 5);
  }
  runFrames(60);
  ASSERT_GT(aiGameViewEntryCount(DEFAULT_MAP_WIDTH, DEFAULT_MAP_HEIGHT), 4);

  startRound(); // "play again"

  // Exactly the four fresh player characters and nothing else.
  EXPECT_EQ(countAllEntities(), 4);
  EXPECT_EQ(aiGameViewEntryCount(DEFAULT_MAP_WIDTH, DEFAULT_MAP_HEIGHT), 4)
      << "stale entries from the previous round became untargetable ghosts";
}

TEST_F(EntitySimTest, AIGameViewHasNoGhostsAcrossThreeRounds)
{
  configurePlayers(2);

  for (int round = 0; round < 3; round++)
  {
    startRound();
    for (int x = 3; x <= 8; x++)
    {
      spawnEntity(EGG1, 0, x, 6);
    }
    runFrames(40);
  }

  startRound();
  EXPECT_EQ(countAllEntities(), 2);
  EXPECT_EQ(aiGameViewEntryCount(DEFAULT_MAP_WIDTH, DEFAULT_MAP_HEIGHT), 2);
}

TEST_F(EntitySimTest, AIGameViewIsResizedWhenTheMapGrows)
{
  configurePlayers(2);
  startRound();
  spawnEntity(EGG1, 0, 8, 6);

  setMapSize(25, 19);
  startRound();

  // (23, 10) is inside the grown map but outside the old 17x13 one, so reading
  // it exercises the resize. It is deliberately not a player start tile -
  // player 2 spawns at (width - 2, height - 2), i.e. (23, 17).
  EXPECT_TRUE(AIGameView::getInstance()->getEntityInfoAtLocation(23, 10).empty());
  EXPECT_EQ(aiGameViewEntryCount(25, 19), 2) << "only the two fresh characters";
}

// -- team identity on entities ----------------------------------------------

TEST_F(EntitySimTest, EntityTeamNumberFollowsTheConfiguredAlliance)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  EXPECT_EQ(player(1)->getTeamNumber(), player(2)->getTeamNumber());
  EXPECT_EQ(player(3)->getTeamNumber(), player(4)->getTeamNumber());
  EXPECT_NE(player(1)->getTeamNumber(), player(3)->getTeamNumber());
}

TEST_F(EntitySimTest, TeammateCheckIsSymmetricAndExcludesOpponents)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  EXPECT_TRUE(player(1)->isTeammatePlayer(2));
  EXPECT_TRUE(player(2)->isTeammatePlayer(1));
  EXPECT_TRUE(player(1)->isTeammatePlayer(1)) << "a player is their own teammate";

  EXPECT_FALSE(player(1)->isTeammatePlayer(3));
  EXPECT_FALSE(player(3)->isTeammatePlayer(1));
}

TEST_F(EntitySimTest, InFreeForAllNobodyIsATeammate)
{
  configurePlayers(4);
  startRound(); // configurePlayers leaves each player on their own team

  for (int a = 1; a <= 4; a++)
  {
    for (int b = 1; b <= 4; b++)
    {
      EXPECT_EQ(player(a)->isTeammatePlayer(b), a == b)
          << "player " << a << " vs " << b;
    }
  }
}

TEST_F(EntitySimTest, NobodyIsATeammateOfTheCpu)
{
  configurePlayers(4);
  setTeams({0, 0, 0, 0}); // even in co-op, the CPU is the enemy
  startRound();

  for (int i = 1; i <= 4; i++)
  {
    EXPECT_FALSE(player(i)->isTeammatePlayer(0))
        << "player " << i << " must never be allied with the CPU";
  }
}

TEST_F(EntitySimTest, EntitiesInheritTheirOwnersTeam)
{
  configurePlayers(4);
  setTeams({0, 0, 1, 1});
  startRound();

  spawnEntity(EGG1, 1, 5, 5);

  DynamicMap* map = em().getDynamicMap();
  bool checked = false;
  for (auto it = map->entityList.begin(); it != map->entityList.end(); ++it)
  {
    if (it->second->getGroupType() != EGG_GROUP)
    {
      continue;
    }
    EXPECT_EQ(it->second->getTeamNumber(), player(2)->getTeamNumber())
        << "player 1's egg is on player 2's team";
    EXPECT_TRUE(it->second->isTeammatePlayer(2));
    EXPECT_FALSE(it->second->isTeammatePlayer(3));
    checked = true;
  }
  EXPECT_TRUE(checked) << "the egg should still be on the map";
}

} // namespace
} // namespace bitriot_test
