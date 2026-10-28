// SimRandomTest.cpp
//
// Brief 02: the simulation draws from its own seeded PRNG, per round, and
// presentation randomness never touches that stream.
//
//   * PRNG: known answers (including the PCG reference implementation's own
//     output), nextInt edge cases, determinism.
//   * Round seed plumbing: GameSettings::pickRoundSeed() and the reseed.
//   * Whole rounds: the same seed plays the same round, different seeds play
//     different rounds, and cosmetic draws change nothing.
//   * A source guard: no libc rand()/srand() in src/.
//
// Rounds are played with Simulation::step() (win check, pool.update(), runFrame(),
// as GameEngine does) and digested by the per-tick state hash.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "support/HeadlessGame.h"
#include "support/SourceScan.h"

#include "GameSettings.h"
#include "RmlUI/binders/ScoreBoardBinder.h"
#include "SimRandom.h"

namespace bitriot_test
{
namespace
{

// -- the generator itself ---------------------------------------------------

std::vector<uint32_t> firstOutputs(uint64_t seed, int count)
{
  SimRandom rng(seed);
  std::vector<uint32_t> out;
  for (int i = 0; i < count; i++)
  {
    out.push_back(rng.nextU32());
  }
  return out;
}

TEST(SimRandomTest, MatchesThePcg32ReferenceImplementation)
{
  // The pcg32 reference demo (initstate 42, initseq 54) prints exactly these.
  EXPECT_EQ(firstOutputs(42, 6),
            (std::vector<uint32_t>{0xa15c02b7, 0x7b47f409, 0xba1d3330,
                                   0x83d2f293, 0xbfa4784b, 0xcbed606e}));
}

TEST(SimRandomTest, KnownAnswersForOtherSeeds)
{
  // Produced by an independent implementation of the algorithm documented in
  // SimRandom.h, so other languages' ports can be checked against the same list.
  EXPECT_EQ(firstOutputs(0, 6),
            (std::vector<uint32_t>{0x47c28b93, 0xb98f6a27, 0x7d3dcb1e,
                                   0xf0761116, 0x9cc33f5b, 0xbe0e744d}));
  EXPECT_EQ(firstOutputs(12345, 6),
            (std::vector<uint32_t>{0xf60105d9, 0xcec02ee0, 0x8774b855,
                                   0xf398bc09, 0x356207f1, 0xbc780c34}));
  // A seed that needs all 64 bits.
  EXPECT_EQ(firstOutputs(0xDEADBEEFCAFEF00DULL, 6),
            (std::vector<uint32_t>{0xfbaadbfe, 0x2ff6cf82, 0x393ab328,
                                   0xc2085c5b, 0x09adef4f, 0x1f23eda6}));
}

TEST(SimRandomTest, NextIntIsPlainModuloOfNextU32)
{
  SimRandom rng(7);
  std::vector<int> got;
  for (int i = 0; i < 10; i++)
  {
    got.push_back(rng.nextInt(6));
  }
  EXPECT_EQ(got, (std::vector<int>{1, 2, 1, 2, 0, 1, 2, 5, 1, 3}));
}

TEST(SimRandomTest, NextIntOfZeroOneOrNegativeIsZeroAndStillConsumesOneDraw)
{
  // rand() % 0 used to be undefined behaviour (SpawningPool with equal delays).
  for (int n : {0, 1, -1, -100})
  {
    SimRandom rng(99);
    SimRandom shadow(99);
    EXPECT_EQ(rng.nextInt(n), 0) << "n=" << n;
    shadow.nextU32();
    // Exactly one draw gone, so the stream position never depends on n.
    EXPECT_EQ(rng.nextU32(), shadow.nextU32()) << "n=" << n;
  }
}

TEST(SimRandomTest, NextIntStaysInRangeAndReachesEveryValue)
{
  SimRandom rng(2024);
  std::set<int> seen;
  for (int i = 0; i < 2000; i++)
  {
    const int v = rng.nextInt(7);
    ASSERT_GE(v, 0);
    ASSERT_LT(v, 7);
    seen.insert(v);
  }
  EXPECT_EQ(seen.size(), 7u);
}

TEST(SimRandomTest, SeedingRestartsTheSequenceAndSeedsDiffer)
{
  SimRandom a(5);
  const uint32_t first = a.nextU32();
  a.nextU32();
  a.seed(5);
  EXPECT_EQ(a.nextU32(), first);
  EXPECT_NE(firstOutputs(5, 4), firstOutputs(6, 4));
}

TEST(SimRandomTest, TheTwoStreamsAreIndependent)
{
  SimRandom::sim().seed(1000);
  SimRandom::cosmetic().seed(1000);
  const uint32_t simFirst = SimRandom::sim().nextU32();

  SimRandom::sim().seed(1000);
  for (int i = 0; i < 50; i++)
  {
    SimRandom::cosmetic().nextU32();
  }
  EXPECT_EQ(SimRandom::sim().nextU32(), simFirst);
  EXPECT_NE(&SimRandom::sim(), &SimRandom::cosmetic());
}

// -- round seed plumbing -----------------------------------------------------

class RoundSeedTest : public HeadlessGameTest {};

TEST_F(RoundSeedTest, BeginRoundSeedUsesAPinnedSeedAndSeedsTheSimStream)
{
  std::GameSettings* settings = std::GameSettings::getInstance();
  settings->setRoundSeed(0x1234567890ULL);
  ASSERT_TRUE(settings->isRoundSeedPinned());

  EXPECT_EQ(bitriot_test::beginRoundSeed(settings), 0x1234567890ULL);
  EXPECT_EQ(settings->getRoundSeed(), 0x1234567890ULL);

  SimRandom expected(0x1234567890ULL);
  EXPECT_EQ(SimRandom::sim().nextU32(), expected.nextU32());
}

TEST_F(RoundSeedTest, APinLastsForOneRoundOnly)
{
  std::GameSettings* settings = std::GameSettings::getInstance();
  settings->setRoundSeed(77);
  EXPECT_EQ(bitriot_test::beginRoundSeed(settings), 77u);
  EXPECT_FALSE(settings->isRoundSeedPinned());

  // "Play again" gets a fresh seed (2^-64 chance of a collision).
  const uint64_t second = bitriot_test::beginRoundSeed(settings);
  const uint64_t third = bitriot_test::beginRoundSeed(settings);
  EXPECT_NE(second, 77u);
  EXPECT_NE(second, third);
  EXPECT_EQ(settings->getRoundSeed(), third);

  SimRandom expected(third);
  EXPECT_EQ(SimRandom::sim().nextU32(), expected.nextU32());
}

TEST_F(RoundSeedTest, StartRoundSeedsFromThePinnedValue)
{
  configurePlayers(2);
  useTestMap();
  setRoundSeed(31337);
  startRound();
  EXPECT_EQ(std::GameSettings::getInstance()->getRoundSeed(), 31337u);
}

// -- whole rounds ------------------------------------------------------------

// FNV-1a over 32-bit values.
class Digest
{
  public:
    void add(uint32_t v)
    {
      for (int i = 0; i < 4; i++)
      {
        m_hash ^= (uint8_t) (v >> (8 * i));
        m_hash *= 1099511628211ULL;
      }
    }
    uint64_t value() const { return m_hash; }
  private:
    uint64_t m_hash = 1469598103934665603ULL;
};

class SimRoundTest : public HeadlessGameTest
{
  protected:
    struct Result
    {
      bool ended = false;
      int frames = 0;
      int winner = 0;
      uint64_t hashDigest = 0; // the state hash of every tick
      uint64_t simStreamAfter = 0;
      bool operator==(const Result& o) const
      {
        return ended == o.ended && frames == o.frames && winner == o.winner &&
               hashDigest == o.hashDigest && simStreamAfter == o.simStreamAfter;
      }
    };

    // Plays one round in engine order. cosmeticNoise draws from the cosmetic
    // stream at the start of every tick (and a burst before the round starts).
    Result playRound(uint64_t seed, int players, int maxFrames, bool cosmeticNoise = false)
    {
      configurePlayers(players);
      if (players == 2)
      {
        useTestMap();
      }
      else
      {
        setMapSize(DEFAULT_MAP_WIDTH, DEFAULT_MAP_HEIGHT);
      }
      if (cosmeticNoise)
      {
        for (int i = 0; i < 100; i++) SimRandom::cosmetic().nextU32();
      }
      setRoundSeed(seed);
      startRound();

      Result r;
      Digest d;
      for (; r.frames < maxFrames && !sim().roundOver(); r.frames++)
      {
        if (cosmeticNoise)
        {
          SimRandom::cosmetic().nextU32();
        }
        sim().step();
        const uint64_t hash = sim().stateHash();
        d.add((uint32_t) hash);
        d.add((uint32_t) (hash >> 32));
      }
      r.ended = sim().roundOver();
      r.winner = sim().winner() ? sim().winner()->getPlayerNumber() : 0;
      r.hashDigest = d.value();
      r.simStreamAfter = SimRandom::sim().nextU32();
      return r;
    }
};

TEST_F(SimRoundTest, SameSeedPlaysTheSameFullRoundTwice)
{
  const Result first = playRound(4242, 2, 20000);
  const Result second = playRound(4242, 2, 20000);
  ASSERT_TRUE(first.ended) << "round did not finish in " << first.frames << " frames";
  ASSERT_GT(first.frames, 10);
  EXPECT_EQ(first.frames, second.frames);
  EXPECT_EQ(first.winner, second.winner);
  EXPECT_EQ(first.hashDigest, second.hashDigest);
  EXPECT_EQ(first.simStreamAfter, second.simStreamAfter);
}

TEST_F(SimRoundTest, SameSeedPlaysTheSameFourPlayerRoundOnTheDefaultMap)
{
  // A busier world: four bots, the spawning pool, many eggs and monsters.
  const Result first = playRound(99, 4, 1500);
  const Result second = playRound(99, 4, 1500);
  EXPECT_EQ(first.frames, second.frames);
  EXPECT_EQ(first.hashDigest, second.hashDigest);
  EXPECT_EQ(first.simStreamAfter, second.simStreamAfter);
}

TEST_F(SimRoundTest, DifferentSeedsPlayDifferentRounds)
{
  std::set<uint64_t> digests;
  std::set<uint64_t> streamPositions;
  const int seeds = 8;
  for (int s = 1; s <= seeds; s++)
  {
    const Result r = playRound((uint64_t) s, 4, 600);
    digests.insert(r.hashDigest);
    streamPositions.insert(r.simStreamAfter);
  }
  EXPECT_EQ((int) digests.size(), seeds);
  EXPECT_EQ((int) streamPositions.size(), seeds);
}

TEST_F(SimRoundTest, CosmeticDrawsDoNotChangeTheSimulation)
{
  const Result quiet = playRound(555, 2, 20000);
  const Result noisy = playRound(555, 2, 20000, true);
  ASSERT_TRUE(quiet.ended);
  EXPECT_EQ(quiet.frames, noisy.frames);
  EXPECT_EQ(quiet.winner, noisy.winner);
  EXPECT_EQ(quiet.hashDigest, noisy.hashDigest);
  EXPECT_EQ(quiet.simStreamAfter, noisy.simStreamAfter);

  const Result quietBusy = playRound(556, 4, 1000);
  const Result noisyBusy = playRound(556, 4, 1000, true);
  EXPECT_EQ(quietBusy.hashDigest, noisyBusy.hashDigest);
  EXPECT_EQ(quietBusy.simStreamAfter, noisyBusy.simStreamAfter);
}

TEST_F(SimRoundTest, ADamageEffectDrawsOneCosmeticValueAndNoSimValue)
{
  configurePlayers(2);
  startRound();

  SimRandom simBefore = SimRandom::sim();
  SimRandom cosmeticBefore = SimRandom::cosmetic();

  spawnEntity(DAMAGE_EFFECT, 0, 3, 3);

  EXPECT_EQ(SimRandom::sim().nextU32(), simBefore.nextU32());
  cosmeticBefore.nextU32(); // the one frame pick
  EXPECT_EQ(SimRandom::cosmetic().nextU32(), cosmeticBefore.nextU32());
}

TEST_F(SimRoundTest, TheScoreboardWinTextDrawsOneCosmeticValueAndNoSimValue)
{
  configurePlayers(2);
  useTestMap();
  startRound();

  SimRandom simBefore = SimRandom::sim();
  SimRandom cosmeticBefore = SimRandom::cosmetic();

  std::ScoreBoardBinder::mapScoreBoardData(player(1), em().getRoundPlayerRoster(), 1);

  EXPECT_EQ(SimRandom::sim().nextU32(), simBefore.nextU32());
  cosmeticBefore.nextU32();
  EXPECT_EQ(SimRandom::cosmetic().nextU32(), cosmeticBefore.nextU32());
}

// -- source guard -------------------------------------------------------------

TEST(SimRandomSourceGuardTest, NoLibcRandomNumbersInSrc)
{
  namespace fs = std::filesystem;
  const fs::path srcDir = fs::path(BITRIOT_SOURCE_DIR) / "src";
  ASSERT_TRUE(fs::exists(srcDir)) << srcDir;

  const std::regex libcRandom(R"((^|[^A-Za-z0-9_])(std::)?(rand|srand|rand_r|random|srandom|drand48|random_shuffle)\s*\()");
  // <random> itself, which covers every engine and distribution (ranlux, knuth_b,
  // uniform_int_distribution, ...), plus the common names in case a header gets
  // them transitively.
  const std::regex otherEngines(R"(#\s*include\s*<random>|random_device|mt19937|default_random_engine|minstd_rand)");

  int scanned = 0;
  std::vector<std::string> offenders;
  for (const auto& entry : fs::recursive_directory_iterator(srcDir))
  {
    const std::string ext = entry.path().extension().string();
    if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".hpp"))
    {
      continue;
    }
    scanned++;
    std::ifstream in(entry.path());
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string code = stripCommentsAndStrings(buf.str());

    if (std::regex_search(code, libcRandom))
    {
      offenders.push_back(entry.path().string() + " (libc random call)");
    }
    // The only place allowed to touch an OS entropy source or a std engine.
    if (entry.path().filename() != "SimRandom.cpp" && std::regex_search(code, otherEngines))
    {
      offenders.push_back(entry.path().string() + " (<random> or a std random engine; use SimRandom)");
    }
  }
  EXPECT_GT(scanned, 50);
  for (const std::string& o : offenders)
  {
    ADD_FAILURE() << o;
  }
}

} // namespace
} // namespace bitriot_test
