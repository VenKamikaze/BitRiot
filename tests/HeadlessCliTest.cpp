// bitriot_headless (brief 05): the real binary is started, as 05b, 07 and 13 will start
// it, and its exit codes and output are checked. Also the config spec parser, in-process.

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "BuildId.h"
#include "HeadlessCli.h"
#include "MatchRecord.h"
#include "MatchReplay.h"
#include "Simulation.h"
#include "support/GoldenScript.h"

namespace fs = std::filesystem;

namespace
{
struct CliRun
{
  int code = -1;
  std::string out;
  std::string err;
};

std::string slurp(const fs::path& path)
{
  std::ifstream in(path.string().c_str(), std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

std::string goldenPath()
{
  return std::string(BITRIOT_SOURCE_DIR) + "/tests/data/golden-round.brmr";
}

// A scratch directory for one test.
class Scratch
{
  public:
    Scratch()
    {
      static int counter = 0;
      m_dir = fs::temp_directory_path() / ("bitriot_cli_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
      fs::create_directories(m_dir);
    }
    ~Scratch()
    {
      std::error_code ignored;
      fs::remove_all(m_dir, ignored);
    }
    std::string file(const std::string& name) const { return (m_dir / name).string(); }

    // The command line for the binary, run from the repository root (where data/ is)
    // with its output in files of the scratch directory.
    static std::string command(const std::string& args, const std::string& outFile, const std::string& errFile)
    {
      return std::string("cd \"") + BITRIOT_SOURCE_DIR + "\" && \"" + BITRIOT_HEADLESS_BINARY + "\" " + args + " > \"" + outFile
          + "\" 2> \"" + errFile + "\"";
    }

    CliRun run(const std::string& args, const std::string& tag = "r")
    {
      CliRun r;
      const int status = std::system(command(args, file(tag + ".out"), file(tag + ".err")).c_str());
      r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      r.out = slurp(file(tag + ".out"));
      r.err = slurp(file(tag + ".err"));
      return r;
    }

  private:
    fs::path m_dir;
};

bool contains(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}
} // namespace

// ---------------------------------------------------------------------------
// replay
// ---------------------------------------------------------------------------

TEST(HeadlessCliTest, ReplayOfTheGoldenRecordMatches)
{
  Scratch s;
  const CliRun r = s.run("replay \"" + goldenPath() + "\"");
  EXPECT_EQ(r.code, 0) << r.out << r.err;
  EXPECT_TRUE(contains(r.out, "replay: OK, ")) << r.out;
  EXPECT_TRUE(contains(r.out, "round over: yes")) << r.out;
  EXPECT_TRUE(contains(r.out, "checkpoints verified")) << r.out;

  // stdout is only the result. The simulation's logger writes DEBUG lines (Debug builds)
  // to std::cout; the binary sends those to stderr so that stdout is the same on every
  // build (05b diffs it).
  std::istringstream lines(r.out);
  std::string line;
  while (std::getline(lines, line))
  {
    const bool known = line.rfind("record: ", 0) == 0 || line.rfind("config: ", 0) == 0 || line.rfind("ticks: ", 0) == 0
        || line.rfind("replay: ", 0) == 0;
    EXPECT_TRUE(known) << "unexpected line on stdout: " << line;
  }
}

TEST(HeadlessCliTest, ReplayNamesTheFirstDivergingTick)
{
  Scratch s;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  size_t victim = 0;
  for (size_t i = 200; i < golden.ticks.size(); i++)
  {
    const PlayerInput in = golden.ticks[i].slot[0];
    if (in.held(PlayerInput::UP) && !in.held(PlayerInput::ACTION1) && !in.held(PlayerInput::ACTION2))
    {
      victim = i;
      break;
    }
  }
  ASSERT_GT(victim, 0u);
  golden.ticks[victim].slot[0] = PlayerInput(PlayerInput::heldBit(PlayerInput::DOWN));
  golden.writeFile(s.file("broken.brmr"));

  const CliRun r = s.run("replay \"" + s.file("broken.brmr") + "\"");
  EXPECT_EQ(r.code, 1) << r.out;
  EXPECT_TRUE(contains(r.out, "MISMATCH")) << r.out;
  EXPECT_TRUE(contains(r.out, "tick " + std::to_string(victim + 1) + ":")) << "the golden record has a checkpoint every tick, so the tick is exact\n" << r.out;
}

TEST(HeadlessCliTest, WithCoarseCheckpointsReplayNamesTheWindow)
{
  Scratch s;
  const std::string cfg = "players=3,map=13x11,humans=0";
  ASSERT_EQ(s.run("run --seed 5 --config " + cfg + " --max-ticks 400 --checkpoint-every 20 --out \"" + s.file("a.brmr") + "\"", "run").code, 0);
  MatchRecord rec = MatchRecord::readFile(s.file("a.brmr"));
  // Give the human something to do at tick 150, then replay as if it had done nothing.
  rec.ticks[149].slot[0] = PlayerInput(PlayerInput::heldBit(PlayerInput::RIGHT));
  rec.writeFile(s.file("b.brmr"));
  // (b.brmr's checkpoints were taken with no input at tick 150.)
  const CliRun r = s.run("replay \"" + s.file("b.brmr") + "\"");
  EXPECT_EQ(r.code, 1) << r.out;
  EXPECT_TRUE(contains(r.out, "state hash differs at tick 160")) << r.out;
  EXPECT_TRUE(contains(r.out, "ticks 141 to 160")) << r.out;
}

TEST(HeadlessCliTest, ReplayOfARecordMadeOnOtherDataExitsWithTheDataCode)
{
  Scratch s;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  golden.dataHash ^= 0xFF;
  golden.writeFile(s.file("other-data.brmr"));
  const CliRun r = s.run("replay \"" + s.file("other-data.brmr") + "\"");
  EXPECT_EQ(r.code, 3) << r.out;
  EXPECT_TRUE(contains(r.out, "DATA MISMATCH")) << r.out;
  EXPECT_EQ(s.run("replay --ignore-data-hash \"" + s.file("other-data.brmr") + "\"", "r2").code, 0);
}

TEST(HeadlessCliTest, ABadOrMissingFileExitsWithTheUsageCode)
{
  Scratch s;
  {
    std::ofstream out(s.file("junk.brmr").c_str(), std::ios::binary);
    out << "this is not a record";
  }
  CliRun r = s.run("replay \"" + s.file("junk.brmr") + "\"");
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "error:")) << r.err;
  EXPECT_TRUE(contains(r.err, "not a match record")) << r.err;

  r = s.run("replay \"" + s.file("missing.brmr") + "\"", "r2");
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "cannot open")) << r.err;

  const std::string golden = slurp(goldenPath());
  {
    std::ofstream out(s.file("cut.brmr").c_str(), std::ios::binary);
    out.write(golden.data(), (std::streamsize) golden.size() / 2);
  }
  r = s.run("replay \"" + s.file("cut.brmr") + "\"", "r3");
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "truncated")) << r.err;
}

TEST(HeadlessCliTest, TwoProcessesReplayingTheSameRecordPrintTheSameThing)
{
  Scratch s;
  const std::string args = "replay --hashes \"" + goldenPath() + "\"";
  // Both start together, as two peers (or two machines' runs of 05b) would.
  const std::string both = "( " + Scratch::command(args, s.file("p1.out"), s.file("p1.err")) + " ) & ( "
      + Scratch::command(args, s.file("p2.out"), s.file("p2.err")) + " ) & wait";
  ASSERT_EQ(std::system(both.c_str()), 0);
  const std::string one = slurp(s.file("p1.out"));
  const std::string two = slurp(s.file("p2.out"));
  EXPECT_FALSE(one.empty());
  EXPECT_EQ(one, two);
  EXPECT_EQ(slurp(s.file("p1.err")), slurp(s.file("p2.err")));

  // And it is the per-tick hash stream of the record's own checkpoints.
  const MatchRecord golden = MatchRecord::readFile(goldenPath());
  std::istringstream lines(one);
  std::string line;
  size_t hashes = 0;
  while (std::getline(lines, line))
  {
    if (line.rfind("hash ", 0) == 0)
    {
      std::istringstream parts(line);
      std::string word, hex;
      uint64_t tick = 0;
      parts >> word >> tick >> hex;
      ASSERT_LT(tick, golden.checkpoints.size());
      EXPECT_EQ(hex, "0x" + bitriotHex64(golden.checkpoints[tick].hash)) << "tick " << tick;
      hashes++;
    }
  }
  EXPECT_EQ(hashes, golden.checkpoints.size());
}

TEST(HeadlessCliTest, TwoProcessesDumpTheSameState)
{
  Scratch s;
  const std::string args = "dump --tick 900 \"" + goldenPath() + "\"";
  const std::string both = "( " + Scratch::command(args, s.file("p1.out"), s.file("p1.err")) + " ) & ( "
      + Scratch::command(args, s.file("p2.out"), s.file("p2.err")) + " ) & wait";
  ASSERT_EQ(std::system(both.c_str()), 0);
  const std::string one = slurp(s.file("p1.out"));
  EXPECT_EQ(one, slurp(s.file("p2.out")));
  EXPECT_TRUE(contains(one, "tick 900, round running")) << one.substr(0, 300);
  EXPECT_TRUE(contains(one, "[entities]"));
  EXPECT_TRUE(contains(one, "PLAYER_CHARACTER"));
  EXPECT_TRUE(contains(one, "entity.health"));
  EXPECT_TRUE(contains(one, "[map]"));
  EXPECT_TRUE(contains(one, "players 0:team0/human 1:team1/human 2:team2/ai 3:team3/ai")) << "slots are 0-based, as in the replay output";

  // The header names the hash of that tick, and it is the record's own checkpoint.
  const MatchRecord golden = MatchRecord::readFile(goldenPath());
  EXPECT_TRUE(contains(one, "state hash 0x" + bitriotHex64(golden.checkpoints[900].hash)));
}

TEST(HeadlessCliTest, DumpsOfDifferentTicksDifferAndTheEndOfARoundIsTheDefault)
{
  Scratch s;
  const CliRun a = s.run("dump --tick 100 \"" + goldenPath() + "\"", "a");
  const CliRun b = s.run("dump --tick 101 \"" + goldenPath() + "\"", "b");
  const CliRun last = s.run("dump \"" + goldenPath() + "\"", "c");
  EXPECT_EQ(a.code, 0);
  EXPECT_NE(a.out, b.out);
  EXPECT_TRUE(contains(last.out, "round over")) << last.out.substr(0, 200);
  const MatchRecord golden = MatchRecord::readFile(goldenPath());
  EXPECT_TRUE(contains(last.out, "tick " + std::to_string(golden.ticks.size()) + ", round over"));
  EXPECT_EQ(s.run("dump --tick 99999999 \"" + goldenPath() + "\"", "d").code, 2);
}

TEST(HeadlessCliTest, DumpOfADivergedRecordStillPrintsAndSaysSo)
{
  Scratch s;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  golden.checkpoints[10].hash ^= 1;
  golden.writeFile(s.file("bad.brmr"));
  const CliRun r = s.run("dump --tick 50 \"" + s.file("bad.brmr") + "\"");
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.out, "tick 50, round running"));
  EXPECT_TRUE(contains(r.err, "warning:")) << r.err;
  EXPECT_TRUE(contains(r.err, "tick 10")) << r.err;
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------

TEST(HeadlessCliTest, RunPlaysARoundAndWritesARecordThatReplays)
{
  Scratch s;
  const CliRun run = s.run("run --seed 0x2A --config players=4,map=13x11,blocks=40,spawner=off,teams=0:0:1:1,humans=1 --out \""
                        + s.file("r.brmr") + "\"");
  ASSERT_EQ(run.code, 0) << run.err;
  EXPECT_TRUE(contains(run.out, "round over: yes")) << run.out;

  const MatchRecord rec = MatchRecord::readFile(s.file("r.brmr"));
  EXPECT_EQ(rec.config.seed, 42u);
  EXPECT_EQ(rec.config.mapWidth, 13);
  EXPECT_EQ(rec.config.blockPercentage, 40);
  EXPECT_FALSE(rec.config.npcSpawnerEnabled);
  ASSERT_EQ(rec.config.numPlayers(), 4);
  EXPECT_EQ(rec.config.slots[2].team, 1);
  EXPECT_FALSE(rec.config.slots[1].ai);
  EXPECT_TRUE(rec.config.slots[0].ai);
  EXPECT_TRUE(rec.roundOver);
  EXPECT_EQ(rec.buildId, Simulation::buildId());
  EXPECT_EQ(rec.checkpointInterval, 30u);

  const CliRun replay = s.run("replay \"" + s.file("r.brmr") + "\"", "rep");
  EXPECT_EQ(replay.code, 0) << replay.out;
}

TEST(HeadlessCliTest, TwoRunsOfTheSameArgumentsWriteTheSameBytes)
{
  Scratch s;
  const std::string args = "run --seed 77 --config players=3,map=13x11 --checkpoint-every 7 --out ";
  const std::string both = "( " + Scratch::command(args + "\"" + s.file("1.brmr") + "\"", s.file("p1.out"), s.file("p1.err")) + " ) & ( "
      + Scratch::command(args + "\"" + s.file("2.brmr") + "\"", s.file("p2.out"), s.file("p2.err")) + " ) & wait";
  ASSERT_EQ(std::system(both.c_str()), 0);
  EXPECT_EQ(slurp(s.file("1.brmr")), slurp(s.file("2.brmr")));
  EXPECT_FALSE(slurp(s.file("1.brmr")).empty());
  EXPECT_EQ(slurp(s.file("p1.out")), slurp(s.file("p2.out")));
}

TEST(HeadlessCliTest, ARecordIsAScriptAndRerunningTheGoldenOneReproducesIt)
{
  Scratch s;
  // No --config, no --seed: the script's own. The golden record has a checkpoint on
  // every tick, so say so.
  const CliRun run = s.run("run --script \"" + goldenPath() + "\" --checkpoint-every 1 --out \"" + s.file("again.brmr") + "\"");
  ASSERT_EQ(run.code, 0) << run.err;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  MatchRecord again = MatchRecord::readFile(s.file("again.brmr"));
  golden.buildId = again.buildId; // the one field that names the build
  EXPECT_EQ(again, golden);
  EXPECT_EQ(again.serialize(), golden.serialize());
}

TEST(HeadlessCliTest, MaxTicksStopsARoundThatIsNotOver)
{
  Scratch s;
  const CliRun run = s.run("run --seed 3 --max-ticks 100 --out \"" + s.file("short.brmr") + "\" --no-rle");
  ASSERT_EQ(run.code, 0) << run.err;
  EXPECT_TRUE(contains(run.out, "run: 100 ticks, round over: no")) << run.out;
  const MatchRecord rec = MatchRecord::readFile(s.file("short.brmr"));
  EXPECT_EQ(rec.ticks.size(), 100u);
  EXPECT_FALSE(rec.roundOver);
  EXPECT_FALSE(rec.rle);
  EXPECT_EQ(s.run("replay \"" + s.file("short.brmr") + "\"", "rep").code, 0);
}

TEST(HeadlessCliTest, ARunCanBeTakenOverByAScriptWithEventsAndInputs)
{
  Scratch s;
  // The golden script's first 600 ticks drive two humans and a takeover; over a config
  // that has no humans to begin with the humans' bytes are ignored until a takeover.
  const std::vector<TickInputs> script = bitriot_test::goldenScript(1, 600);
  MatchRecord scriptRecord;
  scriptRecord.config = bitriot_test::goldenConfig(9);
  scriptRecord.ticks = script;
  scriptRecord.writeFile(s.file("script.brmr"));
  const CliRun run = s.run("run --script \"" + s.file("script.brmr") + "\" --max-ticks 600 --out \"" + s.file("out.brmr") + "\"");
  ASSERT_EQ(run.code, 0) << run.err;
  const MatchRecord out = MatchRecord::readFile(s.file("out.brmr"));
  ASSERT_EQ(out.ticks.size(), 600u);
  int events = 0;
  for (const TickInputs& t : out.ticks) { events += (int) t.control.size(); }
  EXPECT_EQ(events, 1) << "the takeover of tick 400 (the return is at 700, past this run)";
}

// ---------------------------------------------------------------------------
// The command line itself
// ---------------------------------------------------------------------------

TEST(HeadlessCliTest, UsageErrorsExitWithTwoAndSayWhy)
{
  Scratch s;
  struct Case { const char* args; const char* message; };
  const Case cases[] = {
    { "", "usage:" },
    { "frobnicate", "unknown command" },
    { "run --seed 1", "needs --out" },
    { "run --out x.brmr", "needs --seed" },
    { "run --seed 1 --out x.brmr --bogus", "unknown option --bogus" },
    { "run --seed", "needs a value" },
    { "run --seed abc --out x.brmr", "must be a non-negative number" },
    { "run --seed 1 --out x.brmr --config players=9", "players must be" },
    { "run --seed 1 --out x.brmr --config colour=red", "unknown config key" },
    { "run --seed 1 --out x.brmr --config map=13", "WIDTHxHEIGHT" },
    { "run --seed 1 --out x.brmr --config humans=5", "humans names slot 5" },
    // every number is range-checked on its full 64-bit value before it is narrowed
    { "run --seed 1 --out x.brmr --config map=4294967309x13", "map width must be 4 to 255" },
    { "run --seed 1 --out x.brmr --config map=13x4294967309", "map height must be 4 to 255" },
    { "run --seed 1 --out x.brmr --config map=3x13", "map width must be 4 to 255" },
    { "run --seed 1 --out x.brmr --config map=256x13", "map width must be 4 to 255" },
    { "run --seed 1 --out x.brmr --config players=4294967298", "players must be 2 to 6" },
    { "run --seed 1 --out x.brmr --config players=1", "players must be 2 to 6" },
    { "run --seed 1 --out x.brmr --config players=7", "players must be 2 to 6" },
    { "run --seed 1 --out x.brmr --config blocks=4294967396", "blocks must be 0 to 100" },
    { "run --seed 1 --out x.brmr --config blocks=101", "blocks must be 0 to 100" },
    { "run --seed 1 --out x.brmr --config teams=300:1", "teams must be 0 to 255" },
    { "run --seed 1 --out x.brmr --config teams=4294967296:1", "teams must be 0 to 255" },
    { "run --seed 1 --out x.brmr --config humans=4294967296", "humans must be 0 to 5" },
    { "run --seed 1 --out x.brmr --config teams=0:1:2", "teams lists 3 teams for 2 players" },
    { "run --seed 1 --out x.brmr --max-ticks 99999999999", "too big" },
    { "replay", "one record file" },
    { "replay a b", "one record file" },
    { "replay --hashes=1 x", "takes no value" },
    { "dump x --tick -1", "non-negative" },
    { "version extra", "no positional" },
    { "run --seed 1 --out /no/such/dir/x.brmr", "cannot open" },
  };
  int n = 0;
  for (const Case& c : cases)
  {
    const CliRun r = s.run(c.args, "c" + std::to_string(n++));
    EXPECT_EQ(r.code, 2) << "args: " << c.args << "\n" << r.out << r.err;
    EXPECT_TRUE(contains(r.err, c.message)) << "args: " << c.args << " expected stderr to contain: " << c.message << "\nstderr: " << r.err;
    EXPECT_TRUE(r.out.empty() || std::string(c.args).rfind("run --seed 1 --out /no", 0) == 0) << "usage errors print nothing on stdout: " << r.out;
  }
}

TEST(HeadlessCliTest, ARecordWithNoCheckpointsReplaysWithAWarning)
{
  Scratch s;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  golden.checkpoints.clear();
  golden.ticks.resize(300);
  golden.roundOver = false;
  golden.writeFile(s.file("unverified.brmr"));
  const CliRun r = s.run("replay \"" + s.file("unverified.brmr") + "\"");
  EXPECT_EQ(r.code, 0) << "it plays; there was just nothing to compare";
  EXPECT_TRUE(contains(r.out, "0 checkpoints verified")) << r.out;
  EXPECT_TRUE(contains(r.err, "no checkpoints")) << r.err;
}

TEST(HeadlessCliTest, ABrokenRecordIsNotADeterminismFailure)
{
  // An event for a slot the round does not have: the simulation refuses the tick. That
  // is a bad file (exit 2), not a replay that disagreed (exit 1), so the cross-platform
  // gate never reads a broken file as a determinism failure.
  Scratch s;
  MatchRecord golden = MatchRecord::readFile(goldenPath());
  golden.ticks[99].control.push_back(ControlEvent(5, ControlEvent::LEAVE));
  golden.writeFile(s.file("broken.brmr"));
  const CliRun r = s.run("replay \"" + s.file("broken.brmr") + "\"");
  EXPECT_EQ(r.code, 2) << r.out;
  EXPECT_TRUE(contains(r.out, "BAD RECORD")) << r.out;
  EXPECT_TRUE(contains(r.out, "tick 100")) << r.out;
}

TEST(HeadlessCliTest, HelpAndVersion)
{
  Scratch s;
  const CliRun help = s.run("help");
  EXPECT_EQ(help.code, 0);
  for (const char* word : { "replay", "run", "dump", "version", "--config", "--script", "--checkpoint-every" })
  {
    EXPECT_TRUE(contains(help.out, word)) << word;
  }
  EXPECT_EQ(s.run("--help", "h2").out, help.out);

  const CliRun version = s.run("version --items", "v");
  EXPECT_EQ(version.code, 0);
  EXPECT_TRUE(contains(version.out, "build id 0x" + bitriotBuildIdString())) << version.out;
  char hash[32];
  std::snprintf(hash, sizeof(hash), "%016llx", (unsigned long long) Simulation::dataHash());
  EXPECT_TRUE(contains(version.out, std::string("data hash 0x") + hash)) << version.out;
  EXPECT_TRUE(contains(version.out, "data/entity.txt:BOMB_FUSE=45"));
  EXPECT_TRUE(contains(version.out, "state-hash version 2"));
  EXPECT_TRUE(contains(version.out, "record format 1"));
}

TEST(HeadlessCliTest, TheBinaryNeedsNoWindowAndLinksNoSdl)
{
  // ldd on the real binary: nothing of SDL, RmlUi, OpenGL or freetype.
  Scratch s;
  const std::string cmd = std::string("ldd \"") + BITRIOT_HEADLESS_BINARY + "\" > \"" + s.file("ldd.txt") + "\" 2>&1";
  if (std::system(cmd.c_str()) != 0)
  {
    GTEST_SKIP() << "ldd is not available here";
  }
  const std::string libs = slurp(s.file("ldd.txt"));
  for (const char* banned : { "SDL", "Rml", "libGL", "freetype", "libpng" })
  {
    EXPECT_FALSE(contains(libs, banned)) << banned << " is linked into bitriot_headless:\n" << libs;
  }
}

// ---------------------------------------------------------------------------
// The config spec
// ---------------------------------------------------------------------------

TEST(HeadlessCliTest, TheConfigSpecSetsWhatItNamesAndKeepsTheRest)
{
  MatchConfig base;
  base.seed = 5;
  base.slots.resize(2);
  base.slots[1].team = 1;
  base.slots[1].ai = false;

  MatchConfig c = headless::parseConfigSpec("", base);
  EXPECT_EQ(c, base);

  c = headless::parseConfigSpec("players=4,map=21x15,blocks=30,spawner=off,teams=0:0:1:1,humans=0:3", base);
  EXPECT_EQ(c.seed, 5u) << "the seed is not part of the spec";
  EXPECT_EQ(c.numPlayers(), 4);
  EXPECT_EQ(c.mapWidth, 21);
  EXPECT_EQ(c.mapHeight, 15);
  EXPECT_EQ(c.blockPercentage, 30);
  EXPECT_FALSE(c.npcSpawnerEnabled);
  EXPECT_EQ(c.slots[1].team, 0);
  EXPECT_EQ(c.slots[3].team, 1);
  EXPECT_FALSE(c.slots[0].ai);
  EXPECT_TRUE(c.slots[1].ai) << "humans= names every human; the base's human is not kept";
  EXPECT_FALSE(c.slots[3].ai);

  c = headless::parseConfigSpec("players=3", base);
  EXPECT_EQ(c.numPlayers(), 3);
  EXPECT_EQ(c.slots[1].team, 1) << "existing slots are kept";
  EXPECT_FALSE(c.slots[1].ai);
  EXPECT_EQ(c.slots[2].team, 2) << "a new slot is on its own team";
  EXPECT_TRUE(c.slots[2].ai);
}

TEST(HeadlessCliTest, TheConfigSpecRangeChecksBeforeItNarrows)
{
  // 4294967309 is 13 once truncated to 32 bits; 4294967298 is 2.
  for (const char* bad : { "map=4294967309x13", "map=13x4294967309", "players=4294967298", "blocks=4294967396", "teams=300:1",
                           "teams=4294967297:1", "humans=4294967296", "humans=6", "map=13x3", "map=1000x13", "players=0" })
  {
    EXPECT_THROW(headless::parseConfigSpec(bad, MatchConfig()), std::invalid_argument) << bad;
  }
  MatchConfig c = headless::parseConfigSpec("map=255x4,blocks=0,players=6,teams=255:0:0:0:0:0,humans=5", MatchConfig());
  EXPECT_EQ(c.mapWidth, 255);
  EXPECT_EQ(c.mapHeight, 4);
  EXPECT_EQ(c.slots[0].team, 255);
  EXPECT_FALSE(c.slots[5].ai);
}

TEST(HeadlessCliTest, TheConfigSpecCanComeFromAFile)
{
  Scratch s;
  {
    std::ofstream out(s.file("c.cfg").c_str());
    out << "# a config\nplayers=3\r\nmap=13x11\n\nblocks=10\n";
  }
  const MatchConfig c = headless::parseConfigSpec("@" + s.file("c.cfg"), MatchConfig());
  EXPECT_EQ(c.numPlayers(), 3);
  EXPECT_EQ(c.mapWidth, 13);
  EXPECT_EQ(c.blockPercentage, 10);
  EXPECT_THROW(headless::parseConfigSpec("@" + s.file("missing.cfg"), MatchConfig()), std::invalid_argument);
}

TEST(HeadlessCliTest, NumbersAreDecimalOrHexAndNeverNegative)
{
  EXPECT_EQ(headless::parseNumber("42", "x"), 42u);
  EXPECT_EQ(headless::parseNumber("0x2a", "x"), 42u);
  EXPECT_EQ(headless::parseNumber("0", "x"), 0u);
  EXPECT_EQ(headless::parseNumber("18446744073709551615", "x"), 18446744073709551615ULL);
  for (const char* bad : { "", "-1", "+1", "4x", "0xg", "18446744073709551616", " 1" })
  {
    EXPECT_THROW(headless::parseNumber(bad, "x"), std::invalid_argument) << "[" << bad << "]";
  }
}
