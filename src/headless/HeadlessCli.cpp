#include "HeadlessCli.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include "BuildId.h"
#include "DataHash.h"
#include "MatchRecord.h"
#include "MatchReplay.h"
#include "Simulation.h"

namespace headless
{
namespace
{
const uint64_t DEFAULT_MAX_TICKS = 100000;
const uint32_t DEFAULT_CHECKPOINT_INTERVAL = 30;
// The map sides a spec may name: the simulation needs 4 or more, and a record holds
// 16 bits; 255 is far past the biggest map the menu offers (33x25) and keeps a typo from
// asking for a map that takes gigabytes.
const unsigned long long MIN_MAP_SIDE = 4;
const unsigned long long MAX_MAP_SIDE = 255;

// Thrown for a bad command line; run() turns it into EXIT_USAGE and the message.
class UsageError : public std::runtime_error
{
  public:
    explicit UsageError(const std::string& what) : std::runtime_error(what) {}
};

std::string hex(uint64_t v)
{
  return "0x" + bitriotHex64(v);
}

void printHelp(std::ostream& out)
{
  out << "bitriot_headless: Bit Riot's simulation with no window, audio or bitmaps.\n"
         "\n"
         "usage:\n"
         "  bitriot_headless replay <file> [--hashes] [--ignore-data-hash] [--perturb-at TICK]\n"
         "      Play the record back and check every checkpoint. Exit 0 if all agree,\n"
         "      1 at the first tick that does not (it is named), 3 if the record was made\n"
         "      on different game data, 2 if the file is bad. --hashes also prints the\n"
         "      state hash of every tick. --perturb-at is a test-only negative control for\n"
         "      the cross-platform gate (05b): after tick TICK-1 it adds one unit (1/65536)\n"
         "      to the x offset of the first block (no step moves one), so the replay must\n"
         "      fail at exactly tick TICK (exit 1) if the record has a checkpoint there.\n"
         "  bitriot_headless run --out <file> [--config SPEC] [--seed N] [--script FILE]\n"
         "                       [--max-ticks N] [--checkpoint-every K] [--no-rle]\n"
         "      Play a fresh round (AI players, plus the inputs and control events of the\n"
         "      script, if any) and write a record. The script is a record file: its ticks\n"
         "      are the inputs; with no --config / --seed its config and seed are used too.\n"
         "      The round ends when it is over or after --max-ticks (default 100000).\n"
         "      --checkpoint-every defaults to 30 (0: only the first and last tick).\n"
         "  bitriot_headless dump <file> [--tick N]\n"
         "      Replay to tick N (default: the last) and print every hashed field of every\n"
         "      entity, to diff two runs that disagree.\n"
         "  bitriot_headless version [--items]\n"
         "      Print this build's build id, data hash and format versions; --items lists\n"
         "      what the data hash is made of.\n"
         "\n"
         "SPEC is comma-separated key=value pairs, or @file with the same:\n"
         "  players=N  map=WxH  blocks=P  spawner=on|off  teams=a:b:c  humans=0:1\n"
         "Slots are 0-based (slot 0 is player 1). humans lists the slots that start as\n"
         "humans (driven by the script); the others are AI. teams gives each slot its team\n"
         "(default: every slot its own). Defaults: players=2 map=17x13 blocks=55 spawner=on.\n"
         "Numbers may be decimal or 0x hex.\n";
}

// Splits "--name value", "--name=value" and flags; collects the positionals.
struct Args
{
  std::vector<std::string> positional;
  std::map<std::string, std::string> values;
  std::set<std::string> flags;

  bool flag(const std::string& name) const { return flags.count(name) != 0; }
  bool has(const std::string& name) const { return values.count(name) != 0; }
  std::string get(const std::string& name) const { return values.at(name); }
};

Args parseArgs(const std::vector<std::string>& args, size_t first, const std::set<std::string>& valueOptions,
               const std::set<std::string>& flagOptions)
{
  Args a;
  for (size_t i = first; i < args.size(); i++)
  {
    const std::string& arg = args[i];
    if (arg.size() > 2 && arg[0] == '-' && arg[1] == '-')
    {
      std::string name = arg.substr(2);
      std::string value;
      bool inlineValue = false;
      const size_t eq = name.find('=');
      if (eq != std::string::npos)
      {
        value = name.substr(eq + 1);
        name = name.substr(0, eq);
        inlineValue = true;
      }
      if (flagOptions.count(name) != 0)
      {
        if (inlineValue)
        {
          throw UsageError("--" + name + " takes no value");
        }
        a.flags.insert(name);
      }
      else if (valueOptions.count(name) != 0)
      {
        if (!inlineValue)
        {
          if (i + 1 >= args.size())
          {
            throw UsageError("--" + name + " needs a value");
          }
          value = args[++i];
        }
        if (a.values.count(name) != 0)
        {
          throw UsageError("--" + name + " given twice");
        }
        a.values[name] = value;
      }
      else
      {
        throw UsageError("unknown option --" + name);
      }
    }
    else if (arg.size() > 1 && arg[0] == '-')
    {
      throw UsageError("unknown option " + arg);
    }
    else
    {
      a.positional.push_back(arg);
    }
  }
  return a;
}

std::string describeConfig(const MatchConfig& c)
{
  std::ostringstream s;
  s << "config: seed " << hex(c.seed) << ", map " << c.mapWidth << "x" << c.mapHeight << ", blocks " << c.blockPercentage
    << "%, spawner " << (c.npcSpawnerEnabled ? "on" : "off") << ", " << c.numPlayers() << " players";
  for (int i = 0; i < c.numPlayers(); i++)
  {
    s << (i == 0 ? " (" : " ") << i << ":team" << c.slots[i].team << (c.slots[i].ai ? "/ai" : "/human");
  }
  if (c.numPlayers() > 0)
  {
    s << ")";
  }
  return s.str();
}

void describeRecord(const MatchRecord& r, std::ostream& out)
{
  out << "record: format " << r.formatVersion << ", state-hash version " << r.stateHashVersion << ", rle " << (r.rle ? "yes" : "no")
      << ", build " << hex(r.buildId) << ", data " << hex(r.dataHash) << "\n";
  out << describeConfig(r.config) << "\n";
  out << "ticks: " << r.ticks.size() << ", checkpoints: " << r.checkpoints.size() << " (interval " << r.checkpointInterval
      << "), round over: " << (r.roundOver ? "yes" : "no") << "\n";
}

uint64_t parseCount(const std::string& text, const std::string& what, uint64_t max)
{
  const unsigned long long v = parseNumber(text, what);
  if (v > max)
  {
    throw UsageError(what + " " + text + " is too big (at most " + std::to_string(max) + ")");
  }
  return v;
}

// -- replay ----------------------------------------------------------------------

int cmdReplay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  const Args a = parseArgs(args, 1, { "perturb-at" }, { "hashes", "ignore-data-hash" });
  if (a.positional.size() != 1)
  {
    throw UsageError("replay takes one record file");
  }
  uint64_t perturbAt = 0;
  if (a.has("perturb-at"))
  {
    perturbAt = parseNumber(a.get("perturb-at"), "--perturb-at");
    if (perturbAt < 1)
    {
      throw UsageError("--perturb-at must be at least 1 (the tick that should diverge)");
    }
  }
  MatchRecord record;
  try
  {
    record = MatchRecord::readFile(a.positional[0]);
  }
  catch (const MatchRecordError& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
  describeRecord(record, out);
  if (record.buildId != Simulation::buildId())
  {
    err << "note: the record was made by build " << hex(record.buildId) << ", this is build " << hex(Simulation::buildId())
        << " (the simulation source differs; the replay may well still agree)\n";
  }

  Simulation sim;
  ReplayOptions options;
  options.checkDataHash = !a.flag("ignore-data-hash");
  bool perturbed = false;
  if (a.flag("hashes") || perturbAt != 0)
  {
    const bool print = a.flag("hashes");
    options.onTick = [&out, &sim, &perturbed, perturbAt, print](uint64_t tick, uint64_t hash)
    {
      if (print)
      {
        out << "hash " << tick << " " << hex(hash) << "\n";
      }
      if (perturbAt != 0 && tick + 1 == perturbAt && !perturbed)
      {
        // The negative control: one unit of one offset, between two ticks, as if one
        // platform had rounded a single value differently.
        // A block is the target: the simulation never moves it, so nothing can
        // quietly absorb the change (a player's offset is pulled back to the tile's
        // centre by the next step). If there is no block, the first entity.
        const auto& entities = sim.entityManager().getDynamicMap()->entityList;
        std::shared_ptr<GameEntity> target;
        for (const auto& entry : entities)
        {
          if (target == nullptr)
          {
            target = entry.second;
          }
          if (entry.second->getType() == BLOCK)
          {
            target = entry.second;
            break;
          }
        }
        if (target != nullptr)
        {
          target->setOffsetX(target->getOffsetX() + 1);
          perturbed = true;
        }
      }
    };
  }
  const ReplayResult r = replayRecord(record, sim, options);
  if (perturbAt != 0 && !perturbed)
  {
    err << "error: --perturb-at " << perturbAt << ": the replay never reached tick " << (perturbAt - 1) << ", or it had no entity to perturb\n";
    return EXIT_USAGE;
  }
  switch (r.status)
  {
    case ReplayResult::OK:
      if (record.checkpoints.empty())
      {
        err << "warning: the record has no checkpoints, so nothing was verified: the replay only ran\n";
      }
      out << "replay: OK, " << r.ticksPlayed << " ticks, " << r.checkpointsChecked << " checkpoints verified, final hash "
          << hex(r.finalHash) << ", round over: " << (r.roundOver ? "yes" : "no") << "\n";
      return EXIT_OK;
    case ReplayResult::DATA_HASH_MISMATCH:
      out << "replay: DATA MISMATCH: " << r.message << "\n";
      return EXIT_DATA;
    case ReplayResult::BAD_CONFIG:
    case ReplayResult::DATA_ERROR:
    case ReplayResult::HASH_VERSION_MISMATCH:
      out << "replay: CANNOT REPLAY: " << r.message << "\n";
      return EXIT_USAGE;
    case ReplayResult::CHECKPOINT_MISMATCH:
      out << "replay: MISMATCH: " << r.message << "\n";
      if (r.lastGoodTick >= 0 && (int64_t) r.mismatchTick - r.lastGoodTick > 1)
      {
        out << "replay: the divergence is in ticks " << (r.lastGoodTick + 1) << " to " << r.mismatchTick
            << " (record with --checkpoint-every 1 to pin the exact tick)\n";
      }
      return EXIT_FAILED;
    case ReplayResult::SIM_ERROR:
      // The simulation refused a tick (an event for a slot the round lacks, an unknown
      // kind): the file is broken, which is not a determinism failure.
      out << "replay: BAD RECORD: " << r.message << "\n";
      return EXIT_USAGE;
    case ReplayResult::ROUND_END_MISMATCH:
      out << "replay: MISMATCH: " << r.message << "\n";
      return EXIT_FAILED;
  }
  return EXIT_FAILED;
}

// -- run -------------------------------------------------------------------------

int cmdRun(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  const Args a = parseArgs(args, 1, { "out", "config", "seed", "script", "max-ticks", "checkpoint-every" }, { "no-rle" });
  if (!a.positional.empty())
  {
    throw UsageError("run takes no positional arguments");
  }
  if (!a.has("out"))
  {
    throw UsageError("run needs --out <file>");
  }

  MatchRecord scriptRecord;
  const bool haveScript = a.has("script");
  if (haveScript)
  {
    try
    {
      scriptRecord = MatchRecord::readFile(a.get("script"));
    }
    catch (const MatchRecordError& e)
    {
      err << "error: " << e.what() << "\n";
      return EXIT_USAGE;
    }
  }

  MatchConfig base;
  if (haveScript)
  {
    base = scriptRecord.config;
  }
  else
  {
    base.slots.assign(2, MatchConfig::Slot());
    base.slots[0].team = 0;
    base.slots[1].team = 1;
  }
  MatchConfig config = a.has("config") ? parseConfigSpec(a.get("config"), base) : base;
  if (a.has("seed"))
  {
    config.seed = parseNumber(a.get("seed"), "--seed");
  }
  else if (!haveScript)
  {
    throw UsageError("run needs --seed N (or a --script whose seed is used)");
  }
  const uint64_t maxTicks = a.has("max-ticks") ? parseCount(a.get("max-ticks"), "--max-ticks", MatchRecord::MAX_TICKS) : DEFAULT_MAX_TICKS;
  const uint32_t interval = a.has("checkpoint-every")
      ? (uint32_t) parseCount(a.get("checkpoint-every"), "--checkpoint-every", 0xffffffffu)
      : DEFAULT_CHECKPOINT_INTERVAL;

  Simulation sim;
  MatchRecord record;
  try
  {
    record = recordRound(sim, config, haveScript ? &scriptRecord.ticks : nullptr, maxTicks, interval, !a.flag("no-rle"));
  }
  catch (const std::exception& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
  try
  {
    record.writeFile(a.get("out"));
  }
  catch (const MatchRecordError& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }

  out << describeConfig(config) << "\n";
  out << "run: " << record.ticks.size() << " ticks, round over: " << (record.roundOver ? "yes" : "no");
  if (record.roundOver)
  {
    const std::shared_ptr<PlayerCharacterEntity> winner = sim.winner();
    if (winner)
    {
      out << ", winner: player " << winner->getPlayerNumber();
    }
    else
    {
      out << ", draw";
    }
  }
  out << ", final hash " << hex(sim.stateHash()) << ", " << record.checkpoints.size() << " checkpoints, wrote "
      << record.serialize().size() << " bytes\n";
  return EXIT_OK;
}

// -- dump ------------------------------------------------------------------------

int cmdDump(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  const Args a = parseArgs(args, 1, { "tick" }, {});
  if (a.positional.size() != 1)
  {
    throw UsageError("dump takes one record file");
  }
  uint64_t wantedTick = 0;
  if (a.has("tick"))
  {
    wantedTick = parseNumber(a.get("tick"), "--tick");
  }
  MatchRecord record;
  try
  {
    record = MatchRecord::readFile(a.positional[0]);
  }
  catch (const MatchRecordError& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
  uint64_t tick = record.ticks.size();
  if (a.has("tick"))
  {
    tick = wantedTick;
    if (tick > record.ticks.size())
    {
      throw UsageError("--tick " + std::to_string(tick) + " is past the record's last tick (" + std::to_string(record.ticks.size()) + ")");
    }
  }

  Simulation sim;
  ReplayOptions options;
  options.stopAfterTick = tick;
  options.continueAfterMismatch = true;
  const ReplayResult r = replayRecord(record, sim, options);
  if (r.status == ReplayResult::DATA_HASH_MISMATCH)
  {
    err << "error: " << r.message << "\n";
    return EXIT_DATA;
  }
  if (!sim.started())
  {
    err << "error: " << r.message << "\n";
    return EXIT_USAGE;
  }
  sim.dumpState(out);
  if (!r.ok())
  {
    err << "warning: " << r.message << "\n";
    return EXIT_FAILED;
  }
  return EXIT_OK;
}

// -- version ---------------------------------------------------------------------

int cmdVersion(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  (void) err;
  const Args a = parseArgs(args, 1, {}, { "items" });
  if (!a.positional.empty())
  {
    throw UsageError("version takes no positional arguments");
  }
  out << "bitriot_headless\n";
  out << "build id " << hex(Simulation::buildId()) << "\n";
  try
  {
    out << "data hash " << hex(Simulation::dataHash()) << "\n";
  }
  catch (const std::exception& e)
  {
    out << "data hash unavailable: " << e.what() << "\n";
  }
  out << "state-hash version " << Simulation::STATE_HASH_VERSION << "\n";
  out << "record format " << MatchRecord::FORMAT_VERSION << "\n";
  if (a.flag("items"))
  {
    for (const std::string& item : DataHash::items())
    {
      out << item << "\n";
    }
  }
  return EXIT_OK;
}
} // namespace

unsigned long long parseNumber(const std::string& text, const std::string& what)
{
  if (text.empty() || text[0] < '0' || text[0] > '9')
  {
    throw std::invalid_argument(what + " must be a non-negative number (decimal or 0x hex), not \"" + text + "\"");
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long v = std::strtoull(text.c_str(), &end, 0);
  if (end == nullptr || *end != '\0' || errno == ERANGE)
  {
    throw std::invalid_argument(what + " must be a non-negative number (decimal or 0x hex), not \"" + text + "\"");
  }
  return v;
}

namespace
{
std::vector<std::string> splitOn(const std::string& s, char sep)
{
  std::vector<std::string> parts;
  std::string current;
  for (char c : s)
  {
    if (c == sep)
    {
      parts.push_back(current);
      current.clear();
    }
    else
    {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  return parts;
}

// A number that must lie in [lo, hi]; checked on the full 64-bit value, before it is
// narrowed to an int (a value like 4294967309 must not wrap around into range).
int parseBounded(const std::string& text, const std::string& what, unsigned long long lo, unsigned long long hi)
{
  const unsigned long long v = parseNumber(text, what);
  if (v < lo || v > hi)
  {
    throw std::invalid_argument(what + " must be " + std::to_string(lo) + " to " + std::to_string(hi) + ", not \"" + text + "\"");
  }
  return (int) v;
}

std::vector<int> parseIntList(const std::string& text, const std::string& key, unsigned long long lo, unsigned long long hi)
{
  std::vector<int> out;
  if (text.empty())
  {
    return out;
  }
  for (const std::string& part : splitOn(text, ':'))
  {
    out.push_back(parseBounded(part, key, lo, hi));
  }
  return out;
}
} // namespace

MatchConfig parseConfigSpec(const std::string& spec, const MatchConfig& base)
{
  std::string text = spec;
  if (!text.empty() && text[0] == '@')
  {
    std::ifstream in(text.substr(1).c_str());
    if (!in.is_open())
    {
      throw std::invalid_argument("cannot open the config file " + text.substr(1));
    }
    std::ostringstream all;
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      if (!line.empty() && line[0] != '#')
      {
        all << line << ",";
      }
    }
    text = all.str();
  }

  MatchConfig c = base;
  std::vector<int> teams;
  std::vector<int> humans;
  bool haveTeams = false;
  bool haveHumans = false;
  int players = c.numPlayers();
  for (const std::string& pair : splitOn(text, ','))
  {
    if (pair.empty())
    {
      continue;
    }
    const size_t eq = pair.find('=');
    if (eq == std::string::npos)
    {
      throw std::invalid_argument("config item \"" + pair + "\" is not key=value");
    }
    const std::string key = pair.substr(0, eq);
    const std::string value = pair.substr(eq + 1);
    if (key == "players")
    {
      players = parseBounded(value, "players", std::GameSettings::MIN_PLAYERS, std::GameSettings::MAX_PLAYERS);
    }
    else if (key == "map")
    {
      const size_t x = value.find('x');
      if (x == std::string::npos)
      {
        throw std::invalid_argument("map must be WIDTHxHEIGHT, not \"" + value + "\"");
      }
      c.mapWidth = parseBounded(value.substr(0, x), "map width", MIN_MAP_SIDE, MAX_MAP_SIDE);
      c.mapHeight = parseBounded(value.substr(x + 1), "map height", MIN_MAP_SIDE, MAX_MAP_SIDE);
    }
    else if (key == "blocks")
    {
      c.blockPercentage = parseBounded(value, "blocks", 0, 100);
    }
    else if (key == "spawner")
    {
      if (value != "on" && value != "off")
      {
        throw std::invalid_argument("spawner must be on or off, not \"" + value + "\"");
      }
      c.npcSpawnerEnabled = value == "on";
    }
    else if (key == "teams")
    {
      teams = parseIntList(value, "teams", 0, 255);
      haveTeams = true;
    }
    else if (key == "humans")
    {
      humans = parseIntList(value, "humans", 0, std::GameSettings::MAX_PLAYERS - 1);
      haveHumans = true;
    }
    else
    {
      throw std::invalid_argument("unknown config key \"" + key + "\" (players, map, blocks, spawner, teams, humans)");
    }
  }

  // Resize the roster; slots that are new get their own team and start as bots.
  const int before = c.numPlayers();
  c.slots.resize(players);
  for (int i = before; i < players; i++)
  {
    c.slots[i].team = i;
    c.slots[i].ai = true;
  }
  if (haveTeams)
  {
    if ((int) teams.size() != players)
    {
      throw std::invalid_argument("teams lists " + std::to_string(teams.size()) + " teams for " + std::to_string(players) + " players");
    }
    for (int i = 0; i < players; i++)
    {
      c.slots[i].team = teams[i];
    }
  }
  if (haveHumans)
  {
    for (int i = 0; i < players; i++)
    {
      c.slots[i].ai = true;
    }
    for (int slot : humans)
    {
      if (slot < 0 || slot >= players)
      {
        throw std::invalid_argument("humans names slot " + std::to_string(slot) + ", but slots are 0 to " + std::to_string(players - 1));
      }
      c.slots[slot].ai = false;
    }
  }
  return c;
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
{
  if (args.empty())
  {
    printHelp(err);
    return EXIT_USAGE;
  }
  const std::string& command = args[0];
  try
  {
    if (command == "help" || command == "--help" || command == "-h")
    {
      printHelp(out);
      return EXIT_OK;
    }
    if (command == "replay")
    {
      return cmdReplay(args, out, err);
    }
    if (command == "run")
    {
      return cmdRun(args, out, err);
    }
    if (command == "dump")
    {
      return cmdDump(args, out, err);
    }
    if (command == "version")
    {
      return cmdVersion(args, out, err);
    }
    err << "error: unknown command \"" << command << "\"\n\n";
    printHelp(err);
    return EXIT_USAGE;
  }
  catch (const UsageError& e)
  {
    err << "error: " << e.what() << "\n(bitriot_headless help shows the usage)\n";
    return EXIT_USAGE;
  }
  catch (const std::invalid_argument& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
  catch (const std::exception& e)
  {
    err << "error: " << e.what() << "\n";
    return EXIT_USAGE;
  }
}
} // namespace headless
