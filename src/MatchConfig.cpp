#include "MatchConfig.h"

MatchConfig MatchConfig::fromSettings(std::GameSettings& settings)
{
  MatchConfig config;
  config.mapWidth = settings.getMapWidth();
  config.mapHeight = settings.getMapHeight();
  config.blockPercentage = settings.getBlockSpawnPercentage();
  config.npcSpawnerEnabled = settings.isNpcSpawnerEnabled();
  const int numPlayers = settings.getNumberOfPlayers();
  for (int i = 0; i < numPlayers; i++)
  {
    Slot slot;
    slot.team = settings.getPlayer(i)->team;
    slot.ai = (settings.getPlayer(i)->human == 0);
    config.slots.push_back(slot);
  }
  return config;
}

void MatchConfig::applyTo(std::GameSettings& settings) const
{
  // setNumberOfPlayers keeps existing slots (and their devices) and ignores a
  // count outside 2-6, which problem() has already ruled out for a real match.
  settings.setNumberOfPlayers(numPlayers());
  for (int i = 0; i < numPlayers() && i < settings.getNumberOfPlayers(); i++)
  {
    settings.setPlayerAI(i, slots[i].ai);
    settings.setPlayerTeam(i, slots[i].team);
  }
  settings.setMapWidth(mapWidth);
  settings.setMapHeight(mapHeight);
  settings.setBlockSpawnPercentage(blockPercentage);
  settings.setNpcSpawnerEnabled(npcSpawnerEnabled);
}

std::string MatchConfig::runnableProblem() const
{
  if (numPlayers() < std::GameSettings::MIN_PLAYERS || numPlayers() > std::GameSettings::MAX_PLAYERS)
  {
    return "a match needs " + std::to_string(std::GameSettings::MIN_PLAYERS) + " to "
        + std::to_string(std::GameSettings::MAX_PLAYERS) + " players, the config has "
        + std::to_string(numPlayers());
  }
  for (int i = 0; i < numPlayers(); i++)
  {
    if (slots[i].team < 0)
    {
      return "slot " + std::to_string(i) + " has a negative team";
    }
  }
  if (mapWidth < 4 || mapHeight < 4)
  {
    return "the map is too small to play on: " + std::to_string(mapWidth) + "x" + std::to_string(mapHeight);
  }
  return "";
}

std::string MatchConfig::problem() const
{
  const std::string runnable = runnableProblem();
  if (!runnable.empty())
  {
    return runnable;
  }
  for (int i = 0; i < numPlayers(); i++)
  {
    if (slots[i].team >= numPlayers())
    {
      return "slot " + std::to_string(i) + " is on team " + std::to_string(slots[i].team)
          + " but there are only " + std::to_string(numPlayers()) + " players";
    }
  }
  typedef std::GameSettings GS;
  if (mapWidth < GS::MIN_MAP_WIDTH || mapWidth > GS::MAX_MAP_WIDTH || mapWidth % 2 == 0
      || mapHeight < GS::MIN_MAP_HEIGHT || mapHeight > GS::MAX_MAP_HEIGHT || mapHeight % 2 == 0)
  {
    return "the map " + std::to_string(mapWidth) + "x" + std::to_string(mapHeight)
        + " is not an odd size inside " + std::to_string(GS::MIN_MAP_WIDTH) + "-" + std::to_string(GS::MAX_MAP_WIDTH)
        + " by " + std::to_string(GS::MIN_MAP_HEIGHT) + "-" + std::to_string(GS::MAX_MAP_HEIGHT);
  }
  // InfoPanel stacks one portrait per player down the side; GameSettings::canStartGame
  // applies the same rule beyond the keyboard players.
  if (numPlayers() > GS::MAX_KEYBOARD_PLAYERS)
  {
    const int needed = 3 * numPlayers() + ((3 * numPlayers()) % 2 == 0 ? 1 : 0);
    if (mapHeight < needed)
    {
      return std::to_string(numPlayers()) + " players need a map at least " + std::to_string(needed) + " tall";
    }
  }
  if (blockPercentage < GS::MIN_BLOCK_PCT || blockPercentage > GS::MAX_BLOCK_PCT)
  {
    return "block percentage " + std::to_string(blockPercentage) + " is outside "
        + std::to_string(GS::MIN_BLOCK_PCT) + "-" + std::to_string(GS::MAX_BLOCK_PCT);
  }
  return "";
}
