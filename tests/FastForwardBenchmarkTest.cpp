// How long does a late joiner (brief 13) need to fast-forward a long round? It steps the host's record without a clock:
// Simulation::step() for every tick, MatchRecorder::onStep() (NetMatch records the round as it goes), and a state hash at every
// 30th tick, exactly what NetMatch::fastForward does per tick except the network. The test REPORTS numbers (stdout, and
// RecordProperty in an XML report) and enforces nothing about speed: a CI machine's clock proves nothing. What it does enforce
// is that the fast-forwarded copy ends in the same state as the round it replayed, which is the point of the design.
//
// The target is 9,000 ticks (five minutes of play) with four players doing A* in under 3 s natively and 10 s in wasm. The
// numbers measured are in doco/netplay/13-disconnect-and-hot-join.md (Outcome); BITRIOT_FF_TICKS changes the length.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "support/HeadlessGame.h"

#include "MatchRecord.h"
#include "MatchReplay.h"

using namespace bitriot_test;

namespace
{
class FastForwardBenchmark : public HeadlessGameTest
{
};

double seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
  return std::chrono::duration<double>(b - a).count();
}
} // namespace

TEST_F(FastForwardBenchmark, NineThousandTicksOfFourBotsAreStepsAndTheEndStateIsTheSame)
{
  const char* env = std::getenv("BITRIOT_FF_TICKS");
  const uint64_t ticks = env != nullptr ? std::strtoull(env, nullptr, 10) : 9000;

  // Four players, all bots, a big map with plenty of blocks to path around (A* is the cost): seed 1 on this map does not end
  // within 20,000 ticks, so the record is as long as asked.
  MatchConfig config;
  config.seed = 1;
  config.mapWidth = 33;
  config.mapHeight = 19;
  config.blockPercentage = 75;
  config.npcSpawnerEnabled = true;
  config.slots.resize(4);
  for (int i = 0; i < 4; i++)
  {
    config.slots[i].team = i;
    config.slots[i].ai = true;
  }

  // The host's round: made once (this is the part a joiner does not do).
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point t0 = Clock::now();
  const MatchRecord record = recordRound(sim(), config, nullptr, ticks, 30, true);
  const Clock::time_point t1 = Clock::now();
  ASSERT_EQ(record.ticks.size(), ticks) << "the round ended early; pick another seed";
  const uint64_t expected = sim().stateHash();

  // The joiner's fast-forward, as NetMatch does it.
  sim().start(record.config);
  MatchRecorder recorder(30, true);
  recorder.begin(sim());
  uint64_t checkpoints = 0;
  const Clock::time_point f0 = Clock::now();
  for (size_t i = 0; i < record.ticks.size(); i++)
  {
    sim().step(record.ticks[i]);
    recorder.onStep(record.ticks[i], sim());
    if ((sim().tick() % 30) == 0)
    {
      sim().stateHash();
      checkpoints++;
    }
  }
  const Clock::time_point f1 = Clock::now();
  EXPECT_EQ(sim().stateHash(), expected) << "the fast-forwarded copy ends in another state";
  EXPECT_EQ(recorder.finish(sim()).ticks.size(), ticks);

  const double host = seconds(t0, t1);
  const double ff = seconds(f0, f1);
  std::printf("[ fast-forward ] %llu ticks, 4 bots, 33x19 map, 75%% blocks: round played in %.3f s, fast-forwarded in %.3f s "
              "(%.0f ticks/s, %llu checkpoint hashes); 9000 ticks would take %.3f s\n",
              (unsigned long long) ticks, host, ff, ticks / ff, (unsigned long long) checkpoints, ff * 9000.0 / (double) ticks);
  RecordProperty("fastForwardSeconds", std::to_string(ff));
  RecordProperty("fastForwardTicks", std::to_string(ticks));
}
