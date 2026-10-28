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
 * Simulation::start() does not read them. It can still destroy them: applying a
 * config with fewer players than the local roster truncates the roster
 * (GameSettings::setNumberOfPlayers), taking slots 3 and 4's gender, controller
 * and keyboard set with it. See the snapshot note below.
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
 *   - A netplay client has local menu choices (player count, teams, genders,
 *     devices, ...) that a match overwrites or truncates. To get them back
 *     afterwards, snapshot the whole local settings BEFORE start() and restore
 *     them after the match:
 *         GameSettings::Snapshot saved = gs.snapshot();
 *         ...                     // MatchConfig from the host; Simulation::start()
 *         gs.restore(saved);
 *     The snapshot keeps the entire roster (every field of every Player, however
 *     many there are), the player count, block percentage, spawner toggle and map
 *     size. Restoring through MatchConfig::applyTo() alone is NOT enough: it
 *     brings the roster back to the right length with default players. The Map and
 *     the window are not part of the snapshot; resize them back separately.
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
  // Does not touch the Map (Simulation::start() resizes it right after this) and
  // does not touch the seed pin.
  void applyTo(std::GameSettings& settings) const;

  // "" when the config can be handed to Simulation::start(), otherwise what is
  // wrong with it. This is the loosest check the sim needs (2-6 players, no
  // negative team, a map of at least 4x4), so tests can run on small maps.
  // start() applies it.
  std::string runnableProblem() const;

  // "" when the config is one the game itself could have produced, otherwise
  // what is wrong with it. Everything runnableProblem() checks, plus what the
  // menus enforce: every team below the player count, map width and height odd and
  // inside GameSettings' limits (and tall enough for the player count), block
  // percentage inside the menu's range. Netplay must call THIS on a config that
  // arrives from another machine, before start().
  std::string problem() const;

  bool operator==(const MatchConfig& o) const
  {
    return seed == o.seed && mapWidth == o.mapWidth && mapHeight == o.mapHeight
        && blockPercentage == o.blockPercentage && npcSpawnerEnabled == o.npcSpawnerEnabled
        && slots == o.slots;
  }
};

#endif /* _MATCHCONFIG_H */
