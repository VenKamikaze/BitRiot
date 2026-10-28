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

std::string MatchConfig::problem() const
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
