#include "DataHash.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "DataReader.h"
#include "Fixed.h"
#include "GameOverTimer.h"
#include "GameSettings.h"
#include "PlayerCharacterEntity.h"
#include "SimpleBrain.h"
#include "Simulation.h"
#include "StateHash.h"

namespace DataHash
{
const char* const ENTITY_FILE = "data/entity.txt";
const char* const PLAYER_FILE = "data/player.txt";

namespace
{
// The simulation's tuning constants that live in code. Adding a named constant to
// the simulation means adding it here (and anything that is not named is covered by
// the build id).
std::vector<std::pair<std::string, int> > simConstantItems()
{
  std::vector<std::pair<std::string, int> > c;
  c.push_back(std::make_pair(std::string("Simulation.STATE_HASH_VERSION"), (int) Simulation::STATE_HASH_VERSION));
  c.push_back(std::make_pair(std::string("Simulation.POOL_POINTS_PER_PLAYER"), (int) Simulation::POOL_POINTS_PER_PLAYER));
  c.push_back(std::make_pair(std::string("GameOverTimer.GAME_OVER_COUNTDOWN_TICKS"), (int) std::GameOverTimer::GAME_OVER_COUNTDOWN_TICKS));
  c.push_back(std::make_pair(std::string("GameSettings.MAX_PLAYERS"), (int) std::GameSettings::MAX_PLAYERS));
  c.push_back(std::make_pair(std::string("GameSettings.MIN_PLAYERS"), (int) std::GameSettings::MIN_PLAYERS));
  c.push_back(std::make_pair(std::string("Fixed.FIXED_SHIFT"), (int) FIXED_SHIFT));
  c.push_back(std::make_pair(std::string("SimpleBrain.INITIAL_MIN_MOVE_TIMER"), SimpleBrain::initialMinMoveTimer()));
  PlayerCharacterEntity::appendSimConstants(c);
  return c;
}
} // namespace

void loadSimData()
{
  DataReader* reader = DataReader::getInstance();
  const char* const files[] = { ENTITY_FILE, PLAYER_FILE };
  for (const char* file : files)
  {
    if (!reader->loadFile(file))
    {
      throw std::runtime_error(std::string("cannot load simulation data ") + file + ": " + reader->lastError());
    }
  }
}

std::vector<std::string> items()
{
  DataReader* reader = DataReader::getInstance();
  std::vector<std::pair<std::string, std::string> > all; // (name, value)
  const char* const files[] = { ENTITY_FILE, PLAYER_FILE };
  for (const char* file : files)
  {
    std::vector<std::pair<std::string, std::string> > entries;
    if (!reader->normalisedEntries(file, entries))
    {
      throw std::runtime_error(std::string("cannot load simulation data ") + file + ": " + reader->lastError());
    }
    for (const auto& e : entries)
    {
      all.push_back(std::make_pair(std::string(file) + ":" + e.first, e.second));
    }
  }
  const std::vector<std::pair<std::string, int> > constants = simConstantItems();
  for (const auto& c : constants)
  {
    all.push_back(std::make_pair("sim:" + c.first, std::to_string(c.second)));
  }
  // Sorted by NAME (names are unique). Sorting the finished "name=value" strings
  // could order two names differently when one is a prefix of the other.
  std::sort(all.begin(), all.end());
  std::vector<std::string> out;
  for (const auto& item : all)
  {
    out.push_back(item.first + "=" + item.second);
  }
  return out;
}

uint64_t hashOfItems(const std::vector<std::string>& sortedItems)
{
  uint64_t h = StateHasher::OFFSET_BASIS;
  for (const std::string& item : sortedItems)
  {
    for (unsigned char byte : item)
    {
      h ^= byte;
      h *= StateHasher::PRIME;
    }
    h ^= 0x0A;
    h *= StateHasher::PRIME;
  }
  return h;
}

uint64_t hash()
{
  return hashOfItems(items());
}
} // namespace DataHash
