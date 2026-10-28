/*
 * MatchConfig.h
 *
 * Everything that must be identical on every peer for a round to play out the
 * same everywhere: the seed, the map, the block density, who is on which team and
 * whether each slot starts as a human or a bot, and whether the NPC spawner is on.
 * A plain value type: a netplay host builds one, sends it, and every peer hands
 * it to Simulation::start().
 *
 * NOT in here, because they never affect the simulation: player genders and
 * portraits, which keyboard set or controller drives a slot, window and audio
 * options. Those stay in GameSettings and are local to each machine.
 *
 * HOW THE SIM READS IT. The simulation reads GameSettings all over the place
 * (teams on every collision, the map size, the spawner toggle), and converting
 * every read to a config pointer would be a very large change. So
 * Simulation::start() *applies* the config into GameSettings (applyTo(), below)
 * and the sim keeps reading GameSettings. Consequences:
 *
 *   - After start(), GameSettings holds the match's values. Nothing in the sim
 *     may be changed through GameSettings while a match runs (the NPC spawner
 *     toggle is the one the menu can reach; it is frozen per match by the config
 *     and covered by the state hash).
 *   - A netplay client has local menu choices (player count, teams, ...) that a
 *     match overwrites. To get them back afterwards, take a snapshot BEFORE
 *     start(): `MatchConfig saved = MatchConfig::fromSettings(gs);` and later
 *     `saved.applyTo(gs);`. The snapshot covers exactly the fields a match
 *     changes, apart from the seed (which it leaves alone) and the per-slot data
 *     the config does not carry (genders, devices), which start() never touches.
 */

#ifndef _MATCHCONFIG_H
#define _MATCHCONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "GameSettings.h"

struct MatchConfig
{
  // One entry per player slot, in slot order (slot 0 is player 1).
  struct Slot
  {
    int team = 0;      // 0-based alliance team; slots sharing a team are allies
    bool ai = true;    // starts as a bot; a human takes over by controller or key
    bool operator==(const Slot& o) const { return team == o.team && ai == o.ai; }
  };

  uint64_t seed = 0;            // seeds SimRandom::sim() at the very start of start()
  int mapWidth = 17;            // tiles, odd in the real game
  int mapHeight = 13;
  int blockPercentage = 55;     // 0-100 (start() clamps); chance per eligible tile
  bool npcSpawnerEnabled = true;
  std::vector<Slot> slots;      // the number of players is slots.size()

  int numPlayers() const { return (int) slots.size(); }

  // Snapshots the match-relevant parts of GameSettings. The seed is left at 0:
  // the caller picks it (GameSettings::pickRoundSeed() for a local round).
  static MatchConfig fromSettings(std::GameSettings& settings);

  // Writes this config into GameSettings, the way Simulation::start() does.
  // Does not touch the Map (that needs the primary SDL surface until brief 03b)
  // and does not touch the seed pin.
  void applyTo(std::GameSettings& settings) const;

  // "" when the config is playable, otherwise what is wrong with it.
  std::string problem() const;

  bool operator==(const MatchConfig& o) const
  {
    return seed == o.seed && mapWidth == o.mapWidth && mapHeight == o.mapHeight
        && blockPercentage == o.blockPercentage && npcSpawnerEnabled == o.npcSpawnerEnabled
        && slots == o.slots;
  }
};

#endif /* _MATCHCONFIG_H */
