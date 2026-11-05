// LockstepSession (brief 07), with N machines in one process on vnet's loopback network and
// virtual time: nothing here sleeps. The fake game on top of each session samples a
// deterministic byte, steps when canStep() allows and logs the TickInputs it stepped
// (tests/support/SessionHarness.h). The assertions are about those logs: identical on every
// machine, in order, with each slot's bytes from its owner exactly D ticks late.

#include <gtest/gtest.h>

#include "support/SessionHarness.h"

using namespace netharness;
using netplay::LockstepSession;

namespace
{
const uint32_t D_DEFAULT = 3;

// Every machine in `which` stepped at least n ticks, and their first n ticks are byte for
// byte the same. Returns the first n ticks of the first machine for further checks.
void expectIdentical(Cluster& c, const std::vector<int>& which, uint32_t n)
{
  ASSERT_FALSE(which.empty());
  const Machine& ref = c.machine(which[0]);
  ASSERT_GE(ref.log.size(), n) << "machine " << which[0] << " stepped only " << ref.log.size();
  for (int id : which)
  {
    const Machine& m = c.machine(id);
    ASSERT_GE(m.log.size(), n) << "machine " << id << " stepped only " << m.log.size();
    for (uint32_t t = 0; t < n; t++)
    {
      for (int s = 0; s < 6; s++)
      {
        ASSERT_EQ(m.log[t].slot[s], ref.log[t].slot[s]) << "machine " << id << " tick " << t + 1 << " slot " << s;
      }
      ASSERT_EQ(m.log[t].control, ref.log[t].control) << "machine " << id << " tick " << t + 1;
    }
  }
}

// The content is what the model says: ticks 1..D empty, then each human slot carries its
// owner's sample for that very tick (sampled D ticks earlier), bots' slots are zero.
void expectModel(const Machine& m, uint32_t n, const std::vector<uint8_t>& owners)
{
  const uint32_t d = (uint32_t) m.round.delay;
  ASSERT_GE(m.log.size(), n);
  for (uint32_t t = 1; t <= n; t++)
  {
    for (int s = 0; s < 6; s++)
    {
      uint8_t expected = 0;
      if (t > d && s < (int) owners.size() && owners[s] != netplay::NO_PEER)
      {
        expected = sampleByte(s, t, owners[s]);
      }
      ASSERT_EQ(m.log[t - 1].slot[s].bits, expected) << "tick " << t << " slot " << s;
    }
    ASSERT_TRUE(m.log[t - 1].control.empty()) << "tick " << t;
  }
}

std::vector<uint8_t> ownersOf(const Machine& m)
{
  std::vector<uint8_t> o;
  for (int s = 0; s < m.round.config.numPlayers(); s++) o.push_back(m.round.owner[s]);
  return o;
}

// Counts events of a kind for a slot across a log.
int countEvents(const Machine& m, int slot, ControlEvent::Kind kind)
{
  int n = 0;
  for (const TickInputs& t : m.log)
  {
    for (const ControlEvent& e : t.control)
    {
      if (e.slot == slot && e.kind == kind) n++;
    }
  }
  return n;
}

int firstTickWithEvent(const Machine& m, int slot, ControlEvent::Kind kind)
{
  for (size_t i = 0; i < m.log.size(); i++)
  {
    for (const ControlEvent& e : m.log[i].control)
    {
      if (e.slot == slot && e.kind == kind) return (int) i + 1;
    }
  }
  return -1;
}

ClusterOptions withDelay(uint32_t ms, const std::string& impair = "")
{
  ClusterOptions o;
  o.delayMs = ms;
  o.impair = impair;
  return o;
}
} // namespace

// -- the lobby ---------------------------------------------------------------------------------

TEST(LockstepSession, PeersJoinAndTheLobbyListsEveryone)
{
  ClusterOptions o;
  o.localPlayers[1] = 2;
  Cluster c(3, o);
  for (int i = 0; i < 3; i++)
  {
    EXPECT_EQ(c.machine(i).s->state(), LockstepSession::LOBBY) << i;
  }
  EXPECT_EQ(c.machine(1).s->myId(), 1);
  EXPECT_EQ(c.machine(2).s->myId(), 2);
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  // The LOBBY broadcast reaches the peers: the host, then peer 1 with two slots, then peer 2.
  const netplay::LobbyView& v = c.machine(2).s->lobby();
  ASSERT_EQ(v.peers.size(), 3u);
  EXPECT_EQ(v.peers[0].name, "host");
  EXPECT_EQ(v.peers[1].name, "peer1");
  EXPECT_EQ(v.peers[1].slotMask, 0b0110);
  EXPECT_EQ(v.peers[2].slotMask, 0b1000);
  ASSERT_EQ(v.slots.size(), 4u);
  EXPECT_EQ(v.slots[0].owner, 0);
  EXPECT_EQ(v.slots[1].owner, 1);
  EXPECT_EQ(v.slots[2].owner, 1);
  EXPECT_EQ(v.slots[3].owner, 2);
}

TEST(LockstepSession, KeepalivesKeepTheLobbyAliveFarPastTheTimeout)
{
  Cluster c(3);
  c.run(60000);   // six timeouts of nothing but keepalives and pings
  for (int i = 0; i < 3; i++)
  {
    EXPECT_EQ(c.machine(i).s->state(), LockstepSession::LOBBY) << i;
  }
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
}

TEST(LockstepSession, WithoutKeepalivesTheLobbyWouldTimeOut)
{
  // The same idle lobby, with a peer that cannot get a packet to the host: the host drops
  // it after the timeout. (What the previous test keeps from happening.)
  Cluster c(3);
  c.impair(2, "loss=100");
  c.run(12000);
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
  EXPECT_EQ(c.machine(1).s->state(), LockstepSession::LOBBY);
}

TEST(LockstepSession, KeepalivesFlowOnTheScoreboardBetweenRounds)
{
  Cluster c(3, withDelay(20));
  ASSERT_TRUE(c.startRound(1, 60));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 20000));
  for (int i = 0; i < 3; i++) EXPECT_EQ(c.machine(i).s->state(), LockstepSession::BETWEEN_ROUNDS) << i;
  c.run(45000);   // a long look at the scoreboard
  for (int i = 0; i < 3; i++)
  {
    EXPECT_EQ(c.machine(i).s->state(), LockstepSession::BETWEEN_ROUNDS) << i;
  }
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  // And the host can still start the next round.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000));
  ASSERT_TRUE(c.startRound(2, 60));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 20000));
  expectIdentical(c, {0, 1, 2}, 60);
}

// -- the input stream -------------------------------------------------------------------------------

struct LossCase
{
  const char* name;
  const char* impair;
  uint32_t atLeastTicks;
};

class LockstepLoss : public ::testing::TestWithParam<LossCase>
{
};

TEST_P(LockstepLoss, EveryMachineStepsTheSameInputsInOrder)
{
  const LossCase& lc = GetParam();
  ClusterOptions o = withDelay(80, lc.impair);
  o.localPlayers[1] = 2;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(11, 0));
  const uint32_t n = lc.atLeastTicks;
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(n); }, 240000)) << "machines stalled for good: "
      << c.machine(0).log.size() << "/" << c.machine(1).log.size() << "/" << c.machine(2).log.size();
  expectIdentical(c, {0, 1, 2}, n);
  expectModel(c.machine(0), n, ownersOf(c.machine(0)));
  // The host picked D from the measured RTTs (160 ms plus jitter): well above the test default.
  EXPECT_GE(c.machine(0).round.delay, 6) << c.machine(0).s->delayNote();
  EXPECT_EQ(c.machine(1).round.delay, c.machine(0).round.delay);
}

INSTANTIATE_TEST_SUITE_P(Impairment, LockstepLoss,
  ::testing::Values(
    LossCase{"clean", "", 300},
    LossCase{"loss5", "loss=5,dup=2,reorder=3,jit=10", 300},
    LossCase{"loss20", "loss=20,dup=3,reorder=5,jit=15", 300}),
  [](const ::testing::TestParamInfo<LossCase>& info) { return std::string(info.param.name); });

TEST(LockstepSession, ManySeedsOfHeavyLossAllAgree)
{
  // Twelve different loss patterns of 25% loss, duplication and reordering, four machines
  // with a different number of players each: the logs are always the same.
  for (uint64_t seed = 1; seed <= 12; seed++)
  {
    ClusterOptions o = withDelay(60, "loss=25,dup=5,reorder=8,jit=20");
    o.seedBase = seed * 1000;
    o.localPlayers[0] = 1;
    o.localPlayers[1] = 2;
    o.localPlayers[2] = 1;
    o.localPlayers[3] = 1;
    o.lobby.mapHeight = 19;
    Cluster c(4, o);
    std::string error;
    ASSERT_TRUE(c.startRound(seed, 0, &error)) << "seed " << seed << ": " << error;
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 300000)) << "seed " << seed;
    expectIdentical(c, {0, 1, 2, 3}, 150);
    expectModel(c.machine(0), 150, ownersOf(c.machine(0)));
  }
}

TEST(LockstepSession, ALossyLinkStillDeliversTheLastTicksOfARound)
{
  // The host finishes the round as soon as it has stepped the last tick, but a peer may not
  // hold the last few yet: the host keeps repeating them to it (between rounds) until it
  // acknowledges them, and will not start the next round before.
  for (uint64_t seed = 1; seed <= 6; seed++)
  {
    ClusterOptions o = withDelay(50, "loss=35,dup=5,reorder=10,jit=15");
    o.seedBase = seed * 777;
    Cluster c(3, o);
    ASSERT_TRUE(c.startRound(seed, 70)) << "seed " << seed;
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(70); }, 120000)) << "seed " << seed << ": "
        << c.machine(0).log.size() << "/" << c.machine(1).log.size() << "/" << c.machine(2).log.size();
    expectIdentical(c, {0, 1, 2}, 70);
    ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 60000)) << "seed " << seed;
  }
}

TEST(LockstepSession, LateRepeatsOfARoundCannotReachTheNextOne)
{
  // Half the packets are held back for half a second: whatever round 1 still has in flight when
  // round 2 starts arrives during round 2, and must be thrown away.
  for (uint64_t seed = 1; seed <= 4; seed++)
  {
    ClusterOptions o = withDelay(20, "loss=5,reorder=50,rdelay=500");
    o.seedBase = seed * 313;
    Cluster c(3, o);
    ASSERT_TRUE(c.startRound(1, 70)) << seed;
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(70) && c.machine(0).s->canStartRound(); }, 120000)) << seed;
    ASSERT_TRUE(c.startRound(2, 0)) << seed;
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120); }, 120000)) << seed;
    expectIdentical(c, {0, 1, 2}, 120);
    expectModel(c.machine(0), 120, ownersOf(c.machine(0)));
  }
}

TEST(LockstepSession, BurstLossIsRiddenOut)
{
  // A burst model: packets drop in runs of about 8, the thing an input delay is really sized for.
  Cluster c(3, withDelay(40, "burst=4,blen=8,jit=5"));
  ASSERT_TRUE(c.startRound(5, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 240000));
  expectIdentical(c, {0, 1, 2}, 300);
  expectModel(c.machine(0), 300, ownersOf(c.machine(0)));
}

TEST(LockstepSession, WithoutRedundancyLossStallsTheMatchForGood)
{
  // The negative control of D7. The same 5% loss that the clean-redundancy case above rides
  // out, with every tick sent once and never repeated: some tick is lost and nobody ever
  // sends it again, so the match freezes far short of 300 ticks.
  ClusterOptions o = withDelay(80, "loss=5,dup=2,reorder=3,jit=10");
  o.session.redundancy = false;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(11, 0));
  const bool done = c.runUntil([&] { return c.allStepped(300); }, 120000);
  EXPECT_FALSE(done) << "it should have stalled";
  EXPECT_LT(c.machine(0).log.size(), 300u);
  // ... and it really is frozen: a minute later nothing has moved.
  const size_t before = c.machine(1).log.size();
  c.run(60000);
  EXPECT_EQ(c.machine(1).log.size(), before);
}

TEST(LockstepSession, WithoutRedundancyAndWithoutLossItStillWorks)
{
  // The control of the control: the stall above is the loss, not the switch.
  ClusterOptions o = withDelay(40);
  o.session.redundancy = false;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(11, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  expectIdentical(c, {0, 1, 2}, 200);
}

TEST(LockstepSession, FirstTicksAreEmptyAndInputArrivesExactlyDTicksLate)
{
  Cluster c(2);
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40); }, 20000));
  const Machine& h = c.machine(0);
  const uint32_t d = (uint32_t) h.round.delay;
  EXPECT_EQ(d, D_DEFAULT - 1 == 2 ? d : d);   // whatever the host chose; the model uses it
  for (uint32_t t = 1; t <= d; t++)
  {
    for (int s = 0; s < 6; s++) EXPECT_EQ(h.log[t - 1].slot[s].bits, 0) << t;
  }
  // The first real input is the sample taken at the first step, for tick D + 1.
  EXPECT_EQ(h.log[d].slot[0].bits, sampleByte(0, d + 1, 0));   // the host's
  EXPECT_EQ(h.log[d].slot[1].bits, sampleByte(1, d + 1, 1));   // peer 1's
  expectModel(h, 40, ownersOf(h));
}

TEST(LockstepSession, AnOldPacketFromAFinishedRoundDoesNotLeakIntoTheNext)
{
  Cluster c(3, withDelay(60, "loss=10,dup=10,reorder=10,jit=20"));
  ASSERT_TRUE(c.startRound(1, 80));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(80); }, 120000));
  expectIdentical(c, {0, 1, 2}, 80);
  // The host can start only when the peers have the last tick of this round.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 20000));
  ASSERT_TRUE(c.startRound(2, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120); }, 120000));
  expectIdentical(c, {0, 1, 2}, 120);
  expectModel(c.machine(0), 120, ownersOf(c.machine(0)));
  EXPECT_EQ(c.machine(1).s->round(), 2);
}

TEST(LockstepSession, TheHostCannotStartTheNextRoundUntilEveryPeerHasTheLastTick)
{
  Cluster c(3, withDelay(100));
  ASSERT_TRUE(c.startRound(1, 50));
  // The moment the host has stepped the last tick, the peers (100 ms away each way) cannot
  // yet have told it they hold that tick.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  std::string error;
  EXPECT_FALSE(c.machine(0).s->canStartRound());
  EXPECT_FALSE(c.machine(0).s->startRound(9, &error));
  EXPECT_NE(error.find("waiting"), std::string::npos) << error;
  // A moment later they have, and it can.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000));
  EXPECT_TRUE(c.machine(0).s->startRound(9, &error)) << error;
}

// -- stalls ---------------------------------------------------------------------------------------

TEST(LockstepSession, ABlackoutStallsEveryoneAndTheMatchResumesIdentical)
{
  Cluster c(3, withDelay(30));
  ASSERT_TRUE(c.startRound(7, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 30000));
  const size_t before = c.machine(1).log.size();
  c.impairAll("lat=30,loss=100");   // 2.5 s with nothing getting through
  c.run(2500);
  const size_t during = c.machine(1).log.size();
  // At most the ticks already confirmed (D of them) could still be stepped.
  EXPECT_LE(during, before + (size_t) c.machine(1).round.delay + 1);
  c.impairAll("lat=30");
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(during + 100); }, 30000));
  expectIdentical(c, {0, 1, 2}, (uint32_t) during + 100);
  expectModel(c.machine(0), (uint32_t) during + 100, ownersOf(c.machine(0)));
  // Nobody was dropped by the blackout (it was shorter than the timeout).
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
}

TEST(LockstepSession, APausedPeerStallsTheOthersAndTheHostSaysWhoItWaitsFor)
{
  Cluster c(3, withDelay(20));
  ASSERT_TRUE(c.startRound(7, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(50); }, 30000));
  c.machine(1).paused = true;   // a window being dragged: pumping goes on, stepping does not
  c.run(1500);
  std::string hostText = c.machine(0).s->waitingText();
  std::string otherText = c.machine(2).s->waitingText();
  EXPECT_NE(hostText.find("peer1"), std::string::npos) << hostText;
  EXPECT_NE(otherText.find("peer1"), std::string::npos) << otherText;
  EXPECT_EQ(c.machine(1).s->waitingText(), "");   // it holds ticks; it is the one holding everybody up
  // Nobody ran ahead: the host is at most D ticks past the paused peer.
  EXPECT_LE(c.machine(0).log.size(), c.machine(1).log.size() + (size_t) c.machine(0).round.delay + 1);
  c.machine(1).paused = false;
  const size_t at = c.machine(0).log.size();
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped((uint32_t) at + 60); }, 30000));
  expectIdentical(c, {0, 1, 2}, (uint32_t) at + 60);
}

TEST(LockstepSession, NobodyEverHoldsMoreThanDUnsteppedTicks)
{
  // By construction a machine produces its record for t only by stepping t - D, and the host
  // confirms t only with every record, so the confirmed-but-unstepped backlog is at most D.
  // Watch it for a run with loss, a blackout and a paused peer.
  Cluster c(3, withDelay(50, "loss=10,jit=15"));
  ASSERT_TRUE(c.startRound(7, 0));
  uint32_t worst = 0;
  for (int ms = 0; ms < 40000; ms++)
  {
    c.run(1);
    if (ms == 8000) c.machine(2).paused = true;
    if (ms == 11000) c.machine(2).paused = false;
    for (int i = 0; i < 3; i++) worst = std::max(worst, c.machine(i).s->unsteppedTicks());
  }
  const uint32_t d = (uint32_t) c.machine(0).round.delay;
  EXPECT_LE(worst, d);
  EXPECT_GT(c.machine(0).log.size(), 300u);
}

TEST(LockstepSession, CatchUpAsksForTwoExtraTicksOnlyWhenFarBehind)
{
  // The rule of the brief, as a function: more than D + 2 confirmed ticks not yet stepped.
  EXPECT_EQ(LockstepSession::catchUpTicks(0, 3), 0);
  EXPECT_EQ(LockstepSession::catchUpTicks(3, 3), 0);
  EXPECT_EQ(LockstepSession::catchUpTicks(5, 3), 0);
  EXPECT_EQ(LockstepSession::catchUpTicks(6, 3), 2);
  EXPECT_EQ(LockstepSession::catchUpTicks(40, 3), 2);
  EXPECT_EQ(LockstepSession::catchUpTicks(9, 7), 0);
  EXPECT_EQ(LockstepSession::catchUpTicks(10, 7), 2);
}

// -- choosing D ---------------------------------------------------------------------------------------

TEST(LockstepSession, ChooseDelayFollowsTheFormula)
{
  // D = ceil((worst RTT + jitter) / 33 ms) + 1, clamped to 2..8.
  EXPECT_EQ(LockstepSession::chooseDelay(0, 0, 33), 2);       // 0 + 1 = 1, clamped up
  EXPECT_EQ(LockstepSession::chooseDelay(10, 0, 33), 2);      // ceil(0.3) + 1
  EXPECT_EQ(LockstepSession::chooseDelay(33, 0, 33), 2);      // exactly one tick
  EXPECT_EQ(LockstepSession::chooseDelay(34, 0, 33), 3);
  EXPECT_EQ(LockstepSession::chooseDelay(80, 0, 33), 4);      // ceil(2.4) + 1
  EXPECT_EQ(LockstepSession::chooseDelay(60, 20, 33), 4);
  EXPECT_EQ(LockstepSession::chooseDelay(200, 0, 33), 8);     // ceil(6.06) + 1 = 8
  EXPECT_EQ(LockstepSession::chooseDelay(2000, 500, 33), 8);  // clamped down
}

class DelayFromRtt : public ::testing::TestWithParam<std::pair<uint32_t, int>>
{
};

TEST_P(DelayFromRtt, TheHostPicksDFromTheMeasuredRoundTrips)
{
  // lat is each way, so RTT is twice it (plus up to a pump of slack).
  Cluster c(3, withDelay(GetParam().first));
  EXPECT_NEAR(c.machine(0).s->proposedDelay(), GetParam().second, 1) << c.machine(0).s->peerRtt(1) << " ms";
  ASSERT_TRUE(c.startRound(1, 0));
  EXPECT_NEAR(c.machine(0).round.delay, GetParam().second, 1);
  EXPECT_EQ(c.machine(1).round.delay, c.machine(0).round.delay);
  EXPECT_EQ(c.machine(2).round.delay, c.machine(0).round.delay);
}

INSTANTIATE_TEST_SUITE_P(Rtts, DelayFromRtt,
  ::testing::Values(std::make_pair(0u, 2), std::make_pair(20u, 3), std::make_pair(50u, 5), std::make_pair(100u, 8), std::make_pair(300u, 8)));

TEST(LockstepSession, TheSlowestPeerSetsD)
{
  Cluster c(3, withDelay(5));
  c.impair(2, "lat=200");   // one slow link (the delay applies to what a machine sends)
  c.run(3000);
  EXPECT_GE(c.machine(0).s->proposedDelay(), 7);
  EXPECT_GE(c.machine(0).s->peerRtt(2), 200);
  EXPECT_LE(c.machine(0).s->peerRtt(1), 30);
}

TEST(LockstepSession, ANewHostWithNoMeasurementUsesTheDefault)
{
  Cluster c(2);
  // Loopback with no latency: RTT 0..2 ms, D clamps to the minimum, not the default.
  EXPECT_EQ(c.machine(0).s->proposedDelay(), LockstepSession::MIN_DELAY);
  // A host nobody has joined has nothing to measure.
  ClusterOptions o;
  Cluster alone(1, o);
  EXPECT_EQ(alone.machine(0).s->proposedDelay(), LockstepSession::DEFAULT_DELAY);
}

TEST(LockstepSession, AForcedDelayWins)
{
  ClusterOptions o = withDelay(100);
  o.session.fixedDelay = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  EXPECT_EQ(c.machine(1).round.delay, 4);
}

// -- leaving and timing out ------------------------------------------------------------------------------

TEST(LockstepSession, APeerWhoLeavesBecomesAnAiOnTheSameTickEverywhere)
{
  ClusterOptions o = withDelay(30, "loss=5");
  o.localPlayers[2] = 2;   // the leaver has slots 2 and 3
  o.lobby.mapHeight = 19;  // five players need a tall map (the menu's rule)
  Cluster c(4, o);
  std::string error;
  ASSERT_TRUE(c.startRound(3, 0, &error)) << error;
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000));
  c.machine(2).s->leave();
  EXPECT_EQ(c.machine(2).s->state(), LockstepSession::ENDED);
  EXPECT_EQ(c.machine(2).s->endReason(), LockstepSession::END_LEFT);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() > 200 && c.machine(1).log.size() > 200 && c.machine(3).log.size() > 200; }, 60000));
  const std::vector<int> stay = {0, 1, 3};
  expectIdentical(c, stay, 200);
  for (int slot : {3, 4})
  {
    // owners: host 0, peer1 1, peer2 slots 2,3?  (slot numbering is by id order)
    (void) slot;
  }
  const Machine& h = c.machine(0);
  // Peer 2 owned slots 2 and 3 (host 0, peer 1 slot 1... see ownersOf): each gets exactly one RETURN_TO_AI.
  int returned = 0;
  for (int s = 0; s < 6; s++)
  {
    if (h.round.owner[s] == 2)
    {
      EXPECT_EQ(countEvents(h, s, ControlEvent::RETURN_TO_AI), 1) << "slot " << s;
      const int t = firstTickWithEvent(h, s, ControlEvent::RETURN_TO_AI);
      EXPECT_GT(t, 60 - 1);
      // After the event the slot carries no input.
      for (size_t i = (size_t) t; i < 200; i++) EXPECT_EQ(h.log[i].slot[s].bits, 0) << "tick " << i + 1;
      returned++;
    }
  }
  EXPECT_EQ(returned, 2);
  // The others kept their inputs flowing.
  EXPECT_NE(h.log[150].slot[0].bits, 0);
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
}

TEST(LockstepSession, ASilentPeerTimesOutAndTheMatchGoesOn)
{
  Cluster c(3, withDelay(30));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000));
  const uint64_t cutAt = c.now();
  c.impair(2, "loss=100");   // peer 2's packets stop arriving
  // Everybody stalls on it until the timeout (10 s), then the host stops waiting.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 1; }, 20000));
  const uint64_t timedOutAfter = c.now() - cutAt;
  EXPECT_GE(timedOutAfter, 9500u);
  EXPECT_LE(timedOutAfter, 11500u);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() > 300 && c.machine(1).log.size() > 300; }, 60000));
  expectIdentical(c, {0, 1}, 300);
  const Machine& h = c.machine(0);
  int slot2 = -1;
  for (int s = 0; s < 6; s++) if (h.round.owner[s] == 2) slot2 = s;
  ASSERT_GE(slot2, 0);
  EXPECT_EQ(countEvents(h, slot2, ControlEvent::RETURN_TO_AI), 1);
  // The dropped peer itself notices that the host went quiet and gives up.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(2).s->ended(); }, 20000));
  EXPECT_EQ(c.machine(2).s->endReason(), LockstepSession::END_HOST_LOST);
}

TEST(LockstepSession, ALatePacketFromADroppedPeerIsAnsweredWithAKick)
{
  Cluster c(3, withDelay(30));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(30); }, 30000));
  c.impair(2, "loss=100");
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 1; }, 20000));
  c.impair(2, "lat=30");    // the network comes back
  ASSERT_TRUE(c.runUntil([&] { return c.machine(2).s->ended(); }, 5000));
  EXPECT_EQ(c.machine(2).s->endReason(), LockstepSession::END_KICKED);
}

TEST(LockstepSession, HostLossEndsTheSessionOnThePeersAfterTheTimeout)
{
  Cluster c(3, withDelay(30));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000));
  const uint64_t at = c.now();
  c.machine(0).frozen = true;   // the host's process is gone
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended() && c.machine(2).s->ended(); }, 20000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_HOST_LOST);
  EXPECT_EQ(c.machine(2).s->endReason(), LockstepSession::END_HOST_LOST);
  EXPECT_GE(c.now() - at, 9500u);
  EXPECT_LE(c.now() - at, 11500u);
  EXPECT_FALSE(c.machine(1).s->endText().empty());
}

TEST(LockstepSession, AHostThatLeavesTellsThePeersAtOnce)
{
  Cluster c(3, withDelay(30));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(30); }, 30000));
  c.machine(0).s->leave();
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended() && c.machine(2).s->ended(); }, 1000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_HOST_CLOSED);
}

TEST(LockstepSession, HostLossInTheLobbyIsDetectedToo)
{
  Cluster c(2);
  c.machine(0).frozen = true;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended(); }, 20000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_HOST_LOST);
}

// -- joining -------------------------------------------------------------------------------------------------

namespace
{
// Sends a raw HELLO from a socket of its own and returns the host's answer (empty if none).
std::vector<uint8_t> rawHello(Cluster& c, uint16_t protocol, uint64_t build, uint64_t data, const std::vector<uint8_t>& junkAfterVersion = {})
{
  Cluster::Raw raw = c.openRaw();
  netplay::Writer w;
  netplay::Header h;
  h.type = netplay::MSG_HELLO;
  netplay::writeHeader(w, h);
  if (junkAfterVersion.empty())
  {
    netplay::HelloMsg m;
    m.protocol = protocol;
    m.buildId = build;
    m.dataHash = data;
    m.nonce = 424242;
    m.name = "raw";
    netplay::encode(w, m);
  }
  else
  {
    w.u16(protocol);
    for (uint8_t b : junkAfterVersion) w.u8(b);
  }
  vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
  c.run(300);
  uint8_t buf[VNET_MAX_DATAGRAM];
  vnet_addr from;
  const int n = vnet_recvfrom(raw.v, raw.sock, buf, sizeof(buf), &from);
  return n > 0 ? std::vector<uint8_t>(buf, buf + n) : std::vector<uint8_t>();
}

netplay::RejectMsg expectReject(const std::vector<uint8_t>& bytes)
{
  netplay::RejectMsg rj;
  EXPECT_FALSE(bytes.empty());
  netplay::Reader r(bytes.data(), bytes.size());
  netplay::Header h;
  EXPECT_TRUE(netplay::readHeader(r, h));
  EXPECT_EQ(h.type, netplay::MSG_REJECT);
  EXPECT_TRUE(netplay::decode(r, rj));
  return rj;
}
} // namespace

TEST(LockstepSession, AHelloOfAnotherProtocolVersionIsRejectedBeforeItsBodyIsRead)
{
  Cluster c(1);
  // A "version 99" HELLO whose layout we know nothing about: three bytes of junk.
  const netplay::RejectMsg rj = expectReject(rawHello(c, 99, 0, 0, {1, 2, 3}));
  EXPECT_EQ(rj.reason, netplay::REJECT_VERSION);
  EXPECT_EQ(rj.hostProtocol, netplay::PROTOCOL_VERSION);
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
}

TEST(LockstepSession, AVersionOneHelloIsRefusedWithRejectVersion)
{
  // Version 2 (brief 08) changed LOBBY, added READY: a build still speaking version 1 must be told so, with the
  // host's own protocol number, rather than join and then misread LOBBY. The v1 HELLO layout is the same as v2's, so
  // this is the exact datagram an old build sends.
  Cluster c(1);
  const netplay::RejectMsg rj = expectReject(rawHello(c, 1, 0xB111D, 0xDA7A));
  EXPECT_EQ(rj.reason, netplay::REJECT_VERSION);
  EXPECT_EQ(rj.hostProtocol, 3);
  EXPECT_NE(rj.text.find("protocol version 1"), std::string::npos) << rj.text;
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
}

TEST(LockstepSession, AMismatchedBuildIdIsRejectedEvenWithTheSameProtocolVersion)
{
  Cluster c(2);
  const netplay::RejectMsg rj = expectReject(rawHello(c, netplay::PROTOCOL_VERSION, 0xBAD, 0xDA7A));
  EXPECT_EQ(rj.reason, netplay::REJECT_BUILD);
  EXPECT_EQ(rj.hostBuildId, 0xB111Du);
  EXPECT_NE(rj.text.find("0x00000000000b111d"), std::string::npos) << rj.text;
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);   // only the real peer
}

TEST(LockstepSession, AJoiningSessionLearnsWhyItWasRejected)
{
  Cluster c(2);
  Machine& bad = c.addMachine(1, 0xBAD, 0);
  ASSERT_TRUE(c.runUntil([&] { return bad.s->ended(); }, 5000));
  EXPECT_EQ(bad.s->endReason(), LockstepSession::END_REJECTED);
  EXPECT_EQ(bad.s->rejectInfo().reason, netplay::REJECT_BUILD);
  EXPECT_EQ(bad.s->rejectInfo().hostBuildId, 0xB111Du);
  EXPECT_NE(bad.s->endText().find("build"), std::string::npos) << bad.s->endText();
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
}

TEST(LockstepSession, AMismatchedDataHashIsRejected)
{
  Cluster c(2);
  Machine& bad = c.addMachine(1, 0, 0xFEED);
  ASSERT_TRUE(c.runUntil([&] { return bad.s->ended(); }, 5000));
  EXPECT_EQ(bad.s->rejectInfo().reason, netplay::REJECT_DATA);
  EXPECT_EQ(bad.s->rejectInfo().hostDataHash, 0xDA7Au);
  EXPECT_NE(bad.s->endText().find("data"), std::string::npos) << bad.s->endText();
}

TEST(LockstepSession, ASeventhPlayerAndTooManyLocalPlayersAreRejectedAsFull)
{
  ClusterOptions o;
  o.localPlayers[0] = 2;
  Cluster c(4, o);   // 2 + 1 + 1 + 1 = 5 slots
  Machine& tooMany = c.addMachine(2);
  ASSERT_TRUE(c.runUntil([&] { return tooMany.s->ended(); }, 5000));
  EXPECT_EQ(tooMany.s->rejectInfo().reason, netplay::REJECT_FULL);
  Machine& last = c.addMachine(1);   // the sixth slot is free
  c.run(1500);
  EXPECT_EQ(last.s->state(), LockstepSession::LOBBY);
  Machine& seventh = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return seventh.s->ended(); }, 5000));
  EXPECT_EQ(seventh.s->rejectInfo().reason, netplay::REJECT_FULL);
}

TEST(LockstepSession, JoiningWhileARoundRunsIsRefusedByAHostThatDoesNotTakeLateJoiners)
{
  // Version 3 lets a joiner into a running round (tests/HotJoinTest.cpp); a host can still say no.
  ClusterOptions o;
  o.session.allowHotJoin = false;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  Machine& late = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return late.s->ended(); }, 5000));
  EXPECT_EQ(late.s->rejectInfo().reason, netplay::REJECT_IN_PROGRESS);
}

TEST(LockstepSession, AMachineThatJoinsBetweenRoundsPlaysTheNextRound)
{
  ClusterOptions o = withDelay(20, "loss=5");
  o.lobby.mapHeight = 13;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 40));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40) && c.machine(0).s->canStartRound(); }, 60000));
  // The scoreboard is up; a third machine walks in.
  Machine& late = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return late.s->state() == LockstepSession::LOBBY; }, 5000)) << late.s->endText();
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  EXPECT_TRUE(c.machine(0).s->canStartRound()) << "a machine that played nothing does not hold the next round up";
  std::string error;
  ASSERT_TRUE(c.startRound(2, 0, &error)) << error;
  EXPECT_EQ(late.round.owner[2], late.s->myId());
  EXPECT_EQ(late.round.config.numPlayers(), 3);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(80); }, 60000));
  expectIdentical(c, {0, 1, 2}, 80);
  expectModel(c.machine(0), 80, ownersOf(c.machine(0)));
}

TEST(LockstepSession, AHelloRepeatedByALossyLinkIsOnePeer)
{
  Cluster c(2, withDelay(50, "loss=40,dup=30"));
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
  EXPECT_EQ(c.machine(1).s->myId(), 1);
}

TEST(LockstepSession, ANobodyHomeHostMakesTheJoinGiveUp)
{
  Cluster c(1);
  vnet_addr nowhere;
  vnet_resolve(nullptr, "127.1.0.77:9999", &nowhere);
  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_LOOPBACK;
  cfg.loopback_network = c.network();
  cfg.ignore_env = 1;
  vnet* v = vnet_create(&cfg, nullptr);
  SessionOptions so;
  so.connectTimeoutMs = 3000;
  std::string err;
  auto s = LockstepSession::join(v, nowhere, so, &err);
  ASSERT_TRUE(s != nullptr) << err;
  for (int i = 0; i < 3500; i++)
  {
    vnet_loopback_network_step(c.network(), 1);
    s->pump();
  }
  EXPECT_EQ(s->endReason(), LockstepSession::END_CONNECT_TIMEOUT);
  s.reset();
  vnet_destroy(v);
}

// -- control events ---------------------------------------------------------------------------------------------

TEST(LockstepSession, ATakeoverOfABotRidesWithItsInputsAndOnlyTheFirstClaimWins)
{
  ClusterOptions o = withDelay(20);
  o.lobby.totalSlots = 4;   // host, peer 1, peer 2, and a bot in slot 3
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_EQ(c.machine(0).round.owner[3], netplay::NO_PEER);
  const uint32_t at = 40;
  // Peer 1 and peer 2 both take slot 3 over on the same tick; peer 2 also tries things it may not do.
  c.machine(1).wantEvents[at] = {ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER)};
  c.machine(2).wantEvents[at] = {ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER),
                                 ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER),   // the host's slot
                                 ControlEvent(1, ControlEvent::RETURN_TO_AI),       // not its own
                                 ControlEvent(9, ControlEvent::RETURN_TO_AI),       // no such slot
                                 ControlEvent(5, ControlEvent::HUMAN_TAKES_OVER),   // a slot this four-player round does not have
                                 ControlEvent(2, (ControlEvent::Kind) 7)};          // no such kind
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(140); }, 60000));
  expectIdentical(c, {0, 1, 2}, 140);
  const Machine& h = c.machine(0);
  const uint32_t d = (uint32_t) h.round.delay;
  // Exactly one event in the whole log: peer 1's takeover, on tick at + D.
  int events = 0;
  for (const TickInputs& t : h.log) events += (int) t.control.size();
  EXPECT_EQ(events, 1);
  ASSERT_EQ(h.log[at + d - 1].control.size(), 1u);
  EXPECT_EQ(h.log[at + d - 1].control[0], ControlEvent(3, ControlEvent::HUMAN_TAKES_OVER));
  // Peer 2 is told its claim was not granted (so its game can take the slot back); peer 1 is not.
  EXPECT_EQ(c.machine(2).s->takeRejectedClaims(), std::vector<int>({3, 0}));   // slot 3 lost the race, slot 0 is the host's (slot 5, 9 and kind 7 never left the machine)
  EXPECT_TRUE(c.machine(1).s->takeRejectedClaims().empty());
  EXPECT_TRUE(c.machine(2).s->takeRejectedClaims().empty()) << "each is reported once";
  // Slot 3 carries peer 1's inputs from that tick on (and nothing before); peer 2's stale
  // claim of the same slot is ignored.
  for (uint32_t t = 1; t < at + d; t++) EXPECT_EQ(h.log[t - 1].slot[3].bits, 0) << t;
  for (uint32_t t = at + d; t <= 140; t++) EXPECT_EQ(h.log[t - 1].slot[3].bits, sampleByte(3, t, 1)) << t;   // peer 1's, not peer 2's
  // The host's own slot and the others never took the bad requests.
  for (uint32_t t = d + 1; t <= 140; t++)
  {
    EXPECT_EQ(h.log[t - 1].slot[0].bits, sampleByte(0, t, 0));
    EXPECT_EQ(h.log[t - 1].slot[1].bits, sampleByte(1, t, 1));
  }
}

TEST(LockstepSession, TakeThenGiveBackInOneTickMeansGivenBack)
{
  ClusterOptions o = withDelay(20);
  o.lobby.totalSlots = 3;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(3, 0));
  // The last event for a slot in a record is the machine's intent: no event reaches the tick, and the
  // machine is told it does not own the slot (it believed it did for a moment).
  c.machine(1).wantEvents[30] = {ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER), ControlEvent(2, ControlEvent::RETURN_TO_AI)};
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(80); }, 60000));
  for (const TickInputs& t : c.machine(0).log) EXPECT_TRUE(t.control.empty());
  EXPECT_EQ(c.machine(1).s->takeRejectedClaims(), std::vector<int>({2}));
}

TEST(LockstepSession, AHumanGivingItsSlotBackOwnsItNoMore)
{
  ClusterOptions o = withDelay(20);
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(3, 0));
  const uint32_t at = 30;
  c.machine(1).wantEvents[at] = {ControlEvent(1, ControlEvent::RETURN_TO_AI)};
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(100); }, 60000));
  expectIdentical(c, {0, 1}, 100);
  const Machine& h = c.machine(0);
  const uint32_t d = (uint32_t) h.round.delay;
  EXPECT_EQ(firstTickWithEvent(h, 1, ControlEvent::RETURN_TO_AI), (int) (at + d));
  for (uint32_t t = at + d; t <= 100; t++) EXPECT_EQ(h.log[t - 1].slot[1].bits, 0) << t;
}

namespace
{
// A peer written by hand on a raw socket: a real handshake, then INPUT packets of its own
// making, to say what no LockstepSession would (it filters its own requests). It runs the
// minimum of the protocol: acknowledge what it sees, and send records ahead of the host.
class HostilePeer
{
  public:
    HostilePeer(Cluster& c) : m_c(c), m_raw(c.openRaw()) {}

    bool join()
    {
      netplay::HelloMsg h;
      h.buildId = 0xB111D;
      h.dataHash = 0xDA7A;
      h.nonce = 99;
      h.name = "mallory";
      send(netplay::MSG_HELLO, netplay::NO_PEER, 0, [&](netplay::Writer& w) { netplay::encode(w, h); });
      return m_c.runUntil([&] { pumpRaw(); return m_id != 0; }, 3000);
    }

    // Plays along for `ms`: reads, acknowledges, and sends `evil(tick)` events in each record.
    void play(uint32_t ms, const std::function<std::vector<ControlEvent>(uint32_t)>& evil)
    {
      for (uint32_t i = 0; i < ms; i++)
      {
        m_c.run(1);
        pumpRaw();
        if (m_started)
        {
          sendInput(evil);
        }
      }
    }

    uint8_t id() const { return m_id; }

    void sendGarbage()
    {
      send(netplay::MSG_INPUT, m_id, m_session, [](netplay::Writer& w) { w.u8(1); });   // too short to decode
    }

  private:
    void send(uint8_t type, uint8_t sender, uint32_t session, const std::function<void(netplay::Writer&)>& body)
    {
      netplay::Writer w;
      netplay::Header h;
      h.type = type;
      h.sender = sender;
      h.session = session;
      netplay::writeHeader(w, h);
      body(w);
      vnet_sendto(m_raw.v, m_raw.sock, w.bytes().data(), w.bytes().size(), &m_c.hostAddr());
    }

    void pumpRaw()
    {
      uint8_t buf[VNET_MAX_DATAGRAM];
      vnet_addr from;
      for (;;)
      {
        const int n = vnet_recvfrom(m_raw.v, m_raw.sock, buf, sizeof(buf), &from);
        if (n < 0) break;
        netplay::Reader r(buf, (size_t) n);
        netplay::Header h;
        if (!netplay::readHeader(r, h)) continue;
        if (h.type == netplay::MSG_WELCOME)
        {
          netplay::WelcomeMsg w;
          if (netplay::decode(r, w)) { m_id = w.peerId; m_session = h.session; }
        }
        else if (h.type == netplay::MSG_START)
        {
          netplay::StartMsg st;
          if (netplay::decode(r, st) && !m_started)
          {
            m_started = true;
            m_round = st.round;
            m_delay = st.delay;
            m_hostAck = st.delay;
            m_have = st.delay;
          }
        }
        else if (h.type == netplay::MSG_TICKS && m_started)
        {
          netplay::TicksMsg t;
          if (netplay::decode(r, t) && t.round == m_round)
          {
            m_hostAck = std::max(m_hostAck, t.ack);
            if (t.first <= m_have + 1) m_have = std::max(m_have, t.first + (uint32_t) t.ticks.size() - 1);
          }
        }
      }
    }

    void sendInput(const std::function<std::vector<ControlEvent>(uint32_t)>& evil)
    {
      netplay::InputMsg m;
      m.round = m_round;
      m.ack = m_have;
      m.first = m_hostAck + 1;
      // Records up to the furthest tick the host will accept: it confirms at most D ahead of what it holds.
      for (uint32_t t = m.first; t <= m_have + (uint32_t) m_delay && m.records.size() < 20; t++)
      {
        netplay::InputRecord rec;
        rec.mask = 0;
        rec.events = evil(t);
        m.records.push_back(rec);
      }
      send(netplay::MSG_INPUT, m_id, m_session, [&](netplay::Writer& w) { netplay::encode(w, m); });
    }

    Cluster& m_c;
    Cluster::Raw m_raw;
    uint8_t m_id = 0;
    uint32_t m_session = 0;
    bool m_started = false;
    uint16_t m_round = 0;
    uint32_t m_delay = 0, m_hostAck = 0, m_have = 0;
};
} // namespace

TEST(LockstepSession, AHostilePeerCannotFreezeTheOthersWithATickTooFullOfEvents)
{
  // A peer that flips one bot's slot between takeover and give-back 24 times in every record would
  // fill a merged tick to the protocol's limit of 24; one more event (a leaver's RETURN_TO_AI)
  // and every TICKS packet holding it would be refused by every peer, for ever, while the packets
  // still counted as signs of life. One event per slot per machine per tick, and a cap, prevent it.
  ClusterOptions o = withDelay(10);
  o.lobby.totalSlots = 5;
  o.lobby.mapHeight = 19;
  Cluster c(3, o);
  HostilePeer mallory(c);
  ASSERT_TRUE(mallory.join());
  c.run(1500);
  std::string error;
  ASSERT_TRUE(c.machine(0).s->startRound(5, &error)) << error;
  const auto evil = [](uint32_t) {
    std::vector<ControlEvent> e;
    for (int i = 0; i < 12; i++)
    {
      e.push_back(ControlEvent(4, ControlEvent::HUMAN_TAKES_OVER));
      e.push_back(ControlEvent(4, ControlEvent::RETURN_TO_AI));
    }
    return e;
  };
  mallory.play(1500, evil);
  c.machine(2).s->leave();   // a leaver: the host adds its RETURN_TO_AI to the next tick
  mallory.play(5000, evil);
  ASSERT_GE(c.machine(1).log.size(), 120u) << "the other honest peer froze";
  expectIdentical(c, {0, 1}, 120);
  for (const TickInputs& t : c.machine(1).log) EXPECT_LE(t.control.size(), 24u);
  EXPECT_EQ(countEvents(c.machine(0), 2, ControlEvent::RETURN_TO_AI), 1) << "the leaver still goes back to the AI";
}

TEST(LockstepSession, GarbageIsNotASignOfLife)
{
  // A peer that keeps sending packets that do not decode times out like a silent one.
  Cluster c(2);
  HostilePeer mallory(c);
  ASSERT_TRUE(mallory.join());
  ASSERT_EQ(c.machine(0).s->peerCount(), 2);
  for (int i = 0; i < 14000; i += 100)
  {
    mallory.sendGarbage();
    c.run(100);
  }
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
}

TEST(LockstepSession, ATakeoverInTheLastTicksOfARoundIsNotRevokedByTheNextRoundsSlots)
{
  // A claim whose tick is the round's last: the answer comes in TICKS that may be repeated after
  // the host has dealt the next round's slots; they must still say who played the slot in THIS round.
  for (uint64_t seed = 1; seed <= 4; seed++)
  {
    ClusterOptions o = withDelay(40, "loss=25,reorder=10");
    o.seedBase = seed * 91;
    o.lobby.totalSlots = 3;
    Cluster c(2, o);
    ASSERT_TRUE(c.startRound(1, 60));
    const uint32_t d = (uint32_t) c.machine(0).round.delay;
    c.machine(1).wantEvents[60 - d] = {ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER)};
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000)) << seed;
    c.run(3000);
    EXPECT_TRUE(c.machine(1).s->takeRejectedClaims().empty()) << "seed " << seed;
  }
}

TEST(LockstepSession, AHostilePeerCannotPutAnEventTheSimulationWouldRefuseIntoATick)
{
  // Slot 5 of a four-player round, slot 9, an unknown kind, a takeover of the host's slot,
  // another peer's slot given back: none may reach a tick (Simulation::step throws on the
  // first two kinds, and a TICKS packet holding one would be refused by every peer).
  ClusterOptions o = withDelay(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  HostilePeer mallory(c);
  ASSERT_TRUE(mallory.join());
  c.run(1500);   // the lobby pings
  ASSERT_TRUE(c.machine(0).s->startRound(5, nullptr));
  const auto evil = [](uint32_t t) {
    std::vector<ControlEvent> e;
    if (t % 3 == 0)
    {
      e.push_back(ControlEvent(5, ControlEvent::HUMAN_TAKES_OVER));
      e.push_back(ControlEvent(9, ControlEvent::RETURN_TO_AI));
      e.push_back(ControlEvent(2, (ControlEvent::Kind) 7));
      e.push_back(ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER));   // the host's
      e.push_back(ControlEvent(1, ControlEvent::RETURN_TO_AI));       // peer 1's
    }
    return e;
  };
  mallory.play(4000, evil);
  // The honest machines kept stepping and saw no event at all.
  ASSERT_GE(c.machine(0).log.size(), 100u);
  ASSERT_GE(c.machine(1).log.size(), 100u);
  expectIdentical(c, {0, 1}, 100);
  for (size_t t = 0; t < 100; t++)
  {
    EXPECT_TRUE(c.machine(0).log[t].control.empty()) << "tick " << t + 1;
  }
}

// -- desyncs --------------------------------------------------------------------------------------------------------

class LockstepDesync : public ::testing::TestWithParam<const char*>
{
};

TEST_P(LockstepDesync, ADifferentHashAtACheckpointStopsEverybodyAtThatTick)
{
  Cluster c(3, withDelay(40, GetParam()));
  ASSERT_TRUE(c.startRound(3, 0));
  // Peer 2's state diverges after tick 55: its checkpoints at 60 and later differ.
  c.machine(2).hashFor = [](uint32_t t) { return t <= 55 ? 0x1000u + t : 0xBAD0000u + t; };
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->desynced() && c.machine(1).s->desynced() && c.machine(2).s->desynced(); }, 60000));
  for (int i = 0; i < 3; i++)
  {
    const netplay::DesyncInfo& d = c.machine(i).s->desyncInfo();
    EXPECT_EQ(d.tick, 60u) << i;       // the first checkpoint after the perturbation
    EXPECT_EQ(d.offender, 2) << i;
    EXPECT_EQ(d.hostHash, 0x1000u + 60) << i;
    EXPECT_EQ(d.peerHash, 0xBAD0000u + 60) << i;
    EXPECT_EQ(d.round, 1) << i;
    EXPECT_FALSE(c.machine(i).s->canStep()) << i;
  }
  // Nobody steps on: a second later the counts have not moved.
  const size_t n0 = c.machine(0).log.size();
  c.run(2000);
  EXPECT_EQ(c.machine(0).log.size(), n0);
  EXPECT_LE(n0, 60u + (size_t) c.machine(0).round.delay + 10);
}

INSTANTIATE_TEST_SUITE_P(Impairment, LockstepDesync, ::testing::Values("", "loss=20,dup=3,reorder=5"));

TEST(LockstepSession, ADesyncInTheLastTicksOfARoundIsStillDetectedAndHoldsTheNextRound)
{
  // The last periodic checkpoint is at tick 60; the state diverges at 65, so the only hash that
  // differs is the one reported with the round's last tick (70), as the round finishes: the peer is
  // BETWEEN_ROUNDS before its next pump, and that report must still go out (and be repeated).
  for (const char* impair : {"", "loss=20,dup=3,reorder=5"})
  {
    Cluster c(3, withDelay(40, impair));
    ASSERT_TRUE(c.startRound(3, 70));
    c.machine(2).hashFor = [](uint32_t t) { return t < 65 ? 0x1000u + t : 0xBAD0000u + t; };
    ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->desynced(); }, 60000)) << "[" << impair << "]";
    const netplay::DesyncInfo& d = c.machine(0).s->desyncInfo();
    EXPECT_EQ(d.tick, 70u);
    EXPECT_EQ(d.offender, 2);
    EXPECT_EQ(c.machine(0).s->state(), LockstepSession::BETWEEN_ROUNDS);
    EXPECT_FALSE(c.machine(0).s->canStartRound()) << "a desynced round is not followed by another";
    ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->desynced() && c.machine(2).s->desynced(); }, 20000));
    EXPECT_EQ(c.machine(1).s->desyncInfo().tick, 70u);
  }
}

TEST(LockstepSession, TheNextRoundWaitsForTheLastTicksHashToBeCompared)
{
  // Peers 200 ms away: when the host has stepped the last tick it holds neither their hash for it
  // nor their acks; it may start the next round only when it has compared them.
  Cluster c(3, withDelay(100));
  ASSERT_TRUE(c.startRound(3, 70));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  EXPECT_FALSE(c.machine(0).s->canStartRound());
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 10000));
  EXPECT_FALSE(c.machine(0).s->desynced());
}

TEST(LockstepSession, MatchingHashesAreNotADesync)
{
  Cluster c(3, withDelay(30, "loss=5"));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(400); }, 60000));
  for (int i = 0; i < 3; i++) EXPECT_FALSE(c.machine(i).s->desynced());
  EXPECT_GE(c.machine(1).reported.size(), 12u);
}

// -- hostile and stray traffic ---------------------------------------------------------------------------------------------

TEST(LockstepSession, StrayAndForgedPacketsChangeNothing)
{
  Cluster c(3, withDelay(20));
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40); }, 30000));
  Cluster::Raw raw = c.openRaw();
  const uint32_t session = c.machine(0).s->sessionId();
  auto send = [&](uint8_t type, uint8_t sender, uint32_t sess, const std::function<void(netplay::Writer&)>& body) {
    netplay::Writer w;
    netplay::Header h;
    h.type = type;
    h.sender = sender;
    h.session = sess;
    netplay::writeHeader(w, h);
    body(w);
    vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
  };
  // An INPUT from a stranger's address claiming to be peer 1, with the right session.
  send(netplay::MSG_INPUT, 1, session, [](netplay::Writer& w) {
    netplay::InputMsg m;
    m.round = 1; m.ack = 0; m.first = 5;
    netplay::InputRecord r; r.mask = 0xFF >> 2; for (int i = 0; i < 6; i++) r.bytes[i] = 0x3F;
    m.records = {r};
    netplay::encode(w, m);
  });
  // A LEAVE from a stranger.
  send(netplay::MSG_LEAVE, 2, session, [](netplay::Writer& w) { netplay::encode(w, netplay::LeaveMsg()); });
  // Garbage, and a packet with the wrong session.
  send(netplay::MSG_TICKS, 0, session + 1, [](netplay::Writer& w) { w.u8(1); });
  { netplay::Writer w; w.u8(1); w.u8(2); vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr()); }
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120); }, 30000));
  expectIdentical(c, {0, 1, 2}, 120);
  expectModel(c.machine(0), 120, ownersOf(c.machine(0)));
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  EXPECT_GE(c.machine(0).s->stats().badPackets, 4u);
}

// -- ready flags, back to the lobby, removing a peer (protocol version 2, brief 08) ------------------------------------

namespace
{
ClusterOptions readyOptions(const std::string& impair = "")
{
  ClusterOptions o;
  o.session.requireReady = true;
  o.impair = impair;
  return o;
}

// The ready flag of machine `id` as machine `viewer` shows it.
bool readyAsSeenBy(Cluster& c, int viewer, int id)
{
  for (const netplay::LobbyEntry& e : c.machine(viewer).s->lobby().peers)
  {
    if (e.id == id) return e.ready;
  }
  return false;
}
} // namespace

TEST(LockstepLobby, StartWaitsForEveryPeerToBeReadyAndSaysWhy)
{
  Cluster c(3, readyOptions());
  LockstepSession& host = *c.machine(0).s;
  EXPECT_EQ(host.notReadyCount(), 2);
  EXPECT_FALSE(host.canStartRound());
  std::string error;
  EXPECT_FALSE(host.startRound(1, &error));
  EXPECT_NE(error.find("waiting for 2 players to be ready"), std::string::npos) << error;

  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.notReadyCount() == 1; }, 2000));
  EXPECT_FALSE(host.canStartRound());
  EXPECT_FALSE(host.startRound(1, &error));
  EXPECT_NE(error.find("waiting for 1 player to be ready"), std::string::npos) << error;
  // Everybody sees who is ready: the host always is, peer 1 now, peer 2 not.
  ASSERT_TRUE(c.runUntil([&] { return readyAsSeenBy(c, 2, 1); }, 2000));
  EXPECT_TRUE(readyAsSeenBy(c, 2, 0));
  EXPECT_FALSE(readyAsSeenBy(c, 1, 2));
  EXPECT_EQ(c.machine(2).s->notReadyCount(), 1);

  c.machine(2).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 2000));
  EXPECT_EQ(host.notReadyCount(), 0);
  ASSERT_TRUE(c.startRound(1, 0, &error)) << error;
}

TEST(LockstepLobby, UnreadyingTakesTheStartAwayAgain)
{
  Cluster c(2, readyOptions());
  LockstepSession& host = *c.machine(0).s;
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 2000));
  c.machine(1).s->setReady(false);
  ASSERT_TRUE(c.runUntil([&] { return !host.canStartRound(); }, 2000));
  EXPECT_FALSE(readyAsSeenBy(c, 1, 1) && host.canStartRound());
  ASSERT_TRUE(c.runUntil([&] { return !readyAsSeenBy(c, 1, 1); }, 2000));
}

TEST(LockstepLobby, ReadyIsRepeatedUntilTheHostShowsItEvenUnderHeavyLoss)
{
  // Eight networks that each lose 60% of everything, a different 60% each: some first answers are lost, and each must still arrive.
  for (uint64_t seed = 1; seed <= 8; seed++)
  {
    SCOPED_TRACE("seed " + std::to_string(seed));
    ClusterOptions o = readyOptions("loss=60");
    o.seedBase = seed * 1000;
    Cluster c(2, o);
    if (c.machine(1).s->state() != LockstepSession::LOBBY) continue;   // the join itself did not get through in this network
    LockstepSession& host = *c.machine(0).s;
    c.machine(1).s->setReady(true);
    ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 30000));
    EXPECT_TRUE(c.runUntil([&] { return readyAsSeenBy(c, 1, 1); }, 30000));   // and the LOBBY that says so gets through
  }
}

TEST(LockstepLobby, WithoutRequireReadyTheLobbyStartsAsBefore)
{
  // The headless runner and brief 07's tests never send READY.
  Cluster c(3);
  EXPECT_TRUE(c.machine(0).s->canStartRound());
  std::string error;
  EXPECT_TRUE(c.startRound(1, 0, &error)) << error;
}

TEST(LockstepLobby, AStaleOrReplayedReadyAnswerCannotUndoANewerOne)
{
  Cluster c(2, readyOptions());
  LockstepSession& host = *c.machine(0).s;
  // A raw peer cannot be crafted without the host's session id and the peer's address, so replay through the real
  // peer: ready, unready, ready again, quickly. The host ends up with the last answer whatever order it saw them.
  c.machine(1).s->setReady(true);
  c.run(5);
  c.machine(1).s->setReady(false);
  c.run(5);
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 3000));
  c.run(3000);
  EXPECT_TRUE(host.canStartRound());
  EXPECT_TRUE(readyAsSeenBy(c, 1, 1));
}

TEST(LockstepLobby, ReadyIsForgottenWhenARoundStarts)
{
  Cluster c(2, readyOptions());
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 2000));
  ASSERT_TRUE(c.startRound(1, 40));
  EXPECT_FALSE(c.machine(1).s->wantsReady());
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40); }, 20000));
  // Between rounds nobody has to be ready again: the next round may start at once.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000));
  std::string error;
  ASSERT_TRUE(c.startRound(2, 40, &error)) << error;
}

TEST(LockstepLobby, ReadyAnswersOutsideTheLobbyAreIgnored)
{
  Cluster c(2, readyOptions());
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 2000));
  ASSERT_TRUE(c.startRound(1, 0));
  // A running round: setReady() on the peer is remembered, nothing is sent and the session carries on.
  c.machine(1).s->setReady(true);
  c.run(2000);
  EXPECT_EQ(c.machine(0).s->state(), LockstepSession::RUNNING);
  EXPECT_EQ(c.machine(0).s->stats().badPackets, 0u);
}

TEST(LockstepLobby, TheHostCanGoBackToTheLobbyAndPeersFollow)
{
  ClusterOptions o = readyOptions();
  o.delayMs = 20;
  Cluster c(3, o);
  c.machine(1).s->setReady(true);
  c.machine(2).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 3000));
  ASSERT_TRUE(c.startRound(1, 60));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 20000));
  LockstepSession& host = *c.machine(0).s;
  ASSERT_TRUE(c.runUntil([&] { return host.state() == LockstepSession::BETWEEN_ROUNDS && host.canStartRound(); }, 5000));

  // The scoreboard phase is announced as such.
  EXPECT_EQ(c.machine(1).s->lobby().phase, netplay::PHASE_BETWEEN_ROUNDS);
  ASSERT_TRUE(host.returnToLobby());
  EXPECT_EQ(host.state(), LockstepSession::LOBBY);
  EXPECT_EQ(host.lobby().phase, netplay::PHASE_LOBBY);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->state() == LockstepSession::LOBBY && c.machine(2).s->state() == LockstepSession::LOBBY; }, 5000));
  EXPECT_EQ(c.machine(1).s->lobby().phase, netplay::PHASE_LOBBY);

  // Back in the lobby everybody has to say ready again; nobody carried it over.
  EXPECT_EQ(host.notReadyCount(), 2);
  EXPECT_FALSE(host.canStartRound());
  EXPECT_FALSE(c.machine(1).s->wantsReady());
  c.machine(1).s->setReady(true);
  c.machine(2).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 3000));

  // The lobby has its own settings again, and a second round plays on identical inputs.
  netplay::LobbySettings ls = host.lobby().settings;
  ls.totalSlots = 4;
  ls.teams = {0, 0, 1, 1};
  host.setLobbySettings(ls);
  std::string error;
  ASSERT_TRUE(c.startRound(2, 60, &error)) << error;
  EXPECT_EQ(c.machine(1).round.config.numPlayers(), 4);
  EXPECT_EQ(c.machine(1).round.config.slots[1].team, 0);
  EXPECT_EQ(c.machine(1).round.config.slots[3].team, 1);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 20000));
  expectIdentical(c, {0, 1, 2}, 60);
}

TEST(LockstepLobby, BackToTheLobbyWaitsForTheLastTickLikeTheNextRound)
{
  ClusterOptions o = readyOptions();
  o.delayMs = 100;
  Cluster c(2, o);
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 3000));
  ASSERT_TRUE(c.startRound(1, 50));
  LockstepSession& host = *c.machine(0).s;
  EXPECT_FALSE(host.returnToLobby()) << "a round is running";
  // The moment the host has stepped the last tick, a peer 100 ms away cannot yet have said it holds it.
  ASSERT_TRUE(c.runUntil([&] { return host.state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  EXPECT_FALSE(host.canStartRound());
  EXPECT_FALSE(host.returnToLobby());
  EXPECT_EQ(host.state(), LockstepSession::BETWEEN_ROUNDS);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 5000));
  EXPECT_TRUE(host.returnToLobby());
}

TEST(LockstepLobby, OnlyTheHostGoesBackAndOnlyFromTheScoreboard)
{
  Cluster c(2);
  EXPECT_FALSE(c.machine(0).s->returnToLobby()) << "already in the lobby";
  EXPECT_FALSE(c.machine(1).s->returnToLobby()) << "a peer cannot";
  c.machine(0).s->setReady(true);   // a host has no ready flag to set
  EXPECT_FALSE(c.machine(0).s->wantsReady());
}

TEST(LockstepLobby, TheHostCanRemoveAPeerAndItIsToldWhy)
{
  Cluster c(3, readyOptions());
  LockstepSession& host = *c.machine(0).s;
  ASSERT_EQ(host.peerCount(), 2);
  EXPECT_FALSE(host.kick(0)) << "the host cannot remove itself";
  EXPECT_FALSE(host.kick(5)) << "nobody there";
  EXPECT_TRUE(host.kick(1));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended(); }, 3000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_REMOVED);
  EXPECT_NE(c.machine(1).s->endText().find("removed"), std::string::npos) << c.machine(1).s->endText();
  EXPECT_EQ(host.peerCount(), 1);
  // The lobby view of the others no longer lists it, and the slots were dealt again.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(2).s->lobby().peers.size() == 2; }, 3000));
  EXPECT_EQ(c.machine(2).s->lobby().slots[1].owner, 2);
  // It is not banned: it may join again (a fresh session).
  Machine& again = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return again.s->state() == LockstepSession::LOBBY; }, 3000));
  EXPECT_EQ(host.peerCount(), 2);
}

TEST(LockstepLobby, ARemovedPeerWhoseLeavePacketsWereAllLostIsStillToldWhy)
{
  Cluster c(2, readyOptions());
  c.impair(0, "loss=100");   // the host's three LEAVEs go nowhere
  ASSERT_TRUE(c.machine(0).s->kick(1));
  c.run(50);
  c.impair(0, "");
  EXPECT_FALSE(c.machine(1).s->ended());
  // The peer is still talking (its keepalives): the host answers with the reason, not with silence.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended(); }, 3000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_REMOVED);
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
}

TEST(LockstepLobby, ARoundInProgressCanKickSinceVersionThree)
{
  // (Brief 13) Removing a peer used to be lobby-only. In a round its slots go to the AI like any leaver's (HotJoinTest).
  Cluster c(2, readyOptions());
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 3000));
  ASSERT_TRUE(c.startRound(1, 0));
  EXPECT_TRUE(c.machine(0).s->kick(1));
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
  EXPECT_FALSE(c.machine(0).s->kick(1)) << "already gone";
}

TEST(LockstepLobby, ATeamChosenForMoreSlotsThanThereAreIsClampedAndTheRoundStillStarts)
{
  Cluster c(2);
  netplay::LobbySettings ls;
  ls.totalSlots = 4;
  ls.teams = {3, 3, 2, 2};
  c.setLobby(ls);
  c.run(100);
  EXPECT_EQ(c.machine(1).s->lobby().slots.size(), 4u);
  EXPECT_EQ(c.machine(1).s->lobby().slots[0].team, 3);
  // The host takes the slot count down to two: the old teams 3 and 2 are not teams of a two-player round.
  ls.totalSlots = 2;
  c.setLobby(ls);
  c.run(100);
  ASSERT_EQ(c.machine(1).s->lobby().slots.size(), 2u);
  EXPECT_EQ(c.machine(1).s->lobby().slots[0].team, 0) << "falls back to the slot's own number";
  EXPECT_EQ(c.machine(1).s->lobby().slots[1].team, 1);
  std::string error;
  ASSERT_TRUE(c.startRound(1, 0, &error)) << error;
  EXPECT_EQ(c.machine(1).round.config.slots[0].team, 0);
}

TEST(LockstepLobby, CannotStartBecauseSaysWhy)
{
  Cluster c(2, readyOptions());
  LockstepSession& host = *c.machine(0).s;
  EXPECT_EQ(host.cannotStartBecause(), "waiting for 1 player to be ready");
  EXPECT_EQ(c.machine(1).s->cannotStartBecause(), "only the host starts a round");
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 2000));
  EXPECT_EQ(host.cannotStartBecause(), "");
  ASSERT_TRUE(c.startRound(1, 0));
  EXPECT_EQ(host.cannotStartBecause(), "a round is running");
}

// A peer written by hand on a raw socket (the way the hostile-peer tests are): it joins properly, then sends exactly the
// READY datagrams a test wants, in any order, for any round.
namespace
{
struct RawPeer
{
  Cluster& c;
  Cluster::Raw raw;
  uint8_t id = netplay::NO_PEER;
  uint32_t session = 0;
  netplay::LobbyMsg lastLobby;
  bool haveLobby = false;

  explicit RawPeer(Cluster& cluster) : c(cluster), raw(cluster.openRaw())
  {
    netplay::Writer w;
    netplay::Header h;
    h.type = netplay::MSG_HELLO;
    netplay::writeHeader(w, h);
    netplay::HelloMsg m;
    m.buildId = 0xB111D;
    m.dataHash = 0xDA7A;
    m.nonce = 99;
    m.name = "rawpeer";
    netplay::encode(w, m);
    vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
    c.run(300);
    drain();
  }

  void drain()
  {
    uint8_t buf[VNET_MAX_DATAGRAM];
    vnet_addr from;
    for (int n; (n = vnet_recvfrom(raw.v, raw.sock, buf, sizeof(buf), &from)) > 0;)
    {
      netplay::Reader r(buf, (size_t) n);
      netplay::Header h;
      if (!netplay::readHeader(r, h)) continue;
      if (h.type == netplay::MSG_WELCOME)
      {
        netplay::WelcomeMsg w;
        if (netplay::decode(r, w)) { id = w.peerId; session = h.session; }
      }
      else if (h.type == netplay::MSG_LOBBY)
      {
        netplay::LobbyMsg l;
        if (netplay::decode(r, l)) { lastLobby = l; haveLobby = true; }
      }
    }
  }

  void ready(uint16_t round, uint32_t seq, bool value)
  {
    netplay::Writer w;
    netplay::Header h;
    h.type = netplay::MSG_READY;
    h.sender = id;
    h.session = session;
    netplay::writeHeader(w, h);
    netplay::ReadyMsg m;
    m.round = round;
    m.seq = seq;
    m.ready = value;
    netplay::encode(w, m);
    vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
    c.run(50);
  }

  // The host's own list: is this peer ready (from the host's view, which is the LOBBY it sent last)?
  bool hostShowsReady()
  {
    c.run(1200);   // a LOBBY goes out every 500 ms in the lobby, every second in a round
    drain();
    for (const netplay::LobbyPeer& p : lastLobby.peers)
    {
      if (p.id == id) return p.ready;
    }
    return false;
  }
};
} // namespace

TEST(LockstepLobby, TheNewestReadyAnswerWinsWhateverOrderTheyArriveIn)
{
  Cluster c(1, readyOptions());
  RawPeer p(c);
  ASSERT_NE(p.id, netplay::NO_PEER);
  ASSERT_TRUE(p.haveLobby || (p.drain(), true));
  const uint16_t round = (uint16_t) (c.machine(0).s->round() + 1);
  p.ready(round, 5, true);
  p.ready(round, 4, false);   // older: it was overtaken in the network
  EXPECT_TRUE(p.hostShowsReady());
  p.ready(round, 5, false);   // a duplicate of the same number does not count as newer
  EXPECT_TRUE(p.hostShowsReady());
  p.ready(round, 6, false);
  EXPECT_FALSE(p.hostShowsReady());
}

TEST(LockstepLobby, AReadyForAnotherRoundIsIgnored)
{
  Cluster c(1, readyOptions());
  RawPeer p(c);
  ASSERT_NE(p.id, netplay::NO_PEER);
  const uint16_t round = (uint16_t) (c.machine(0).s->round() + 1);
  p.ready((uint16_t) (round + 1), 1, true);   // for a later round
  p.ready((uint16_t) (round + 7), 2, true);
  p.ready(0, 3, true);                          // for none
  EXPECT_FALSE(p.hostShowsReady());
  EXPECT_EQ(c.machine(0).s->notReadyCount(), 1);
  p.ready(round, 4, true);
  EXPECT_TRUE(p.hostShowsReady());
}

TEST(LockstepLobby, AReadyDuringARoundIsIgnoredEvenForTheNextRoundsNumber)
{
  Cluster c(1, readyOptions());
  RawPeer p(c);
  ASSERT_NE(p.id, netplay::NO_PEER);
  const uint16_t round = (uint16_t) (c.machine(0).s->round() + 1);
  p.ready(round, 1, true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 2000));
  std::string error;
  ASSERT_TRUE(c.machine(0).s->startRound(1, &error)) << error;
  ASSERT_EQ(c.machine(0).s->state(), LockstepSession::RUNNING);
  p.ready((uint16_t) (c.machine(0).s->round() + 1), 2, true);   // the number a lobby after this round would have
  p.ready(c.machine(0).s->round(), 3, true);
  EXPECT_FALSE(p.hostShowsReady()) << "the round started: nobody is ready for anything until the lobby is back";
}

TEST(LockstepLobby, AMachineThatJoinedBetweenRoundsCannotSayReadyUntilTheHostIsBackInTheLobby)
{
  ClusterOptions o = readyOptions();
  o.delayMs = 20;
  Cluster c(2, o);
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 3000));
  ASSERT_TRUE(c.startRound(1, 40));
  LockstepSession& host = *c.machine(0).s;
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40) && host.canStartRound(); }, 60000));
  Machine& late = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return late.s->state() == LockstepSession::LOBBY; }, 5000));
  ASSERT_TRUE(c.runUntil([&] { return late.s->lobby().phase == netplay::PHASE_BETWEEN_ROUNDS; }, 3000));
  late.s->setReady(true);
  EXPECT_FALSE(late.s->wantsReady()) << "the host's lobby is not open";
  ASSERT_TRUE(host.returnToLobby());
  ASSERT_TRUE(c.runUntil([&] { return late.s->lobby().phase == netplay::PHASE_LOBBY; }, 3000));
  late.s->setReady(true);
  EXPECT_TRUE(late.s->wantsReady());
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return host.canStartRound(); }, 3000));
}

// A host written by hand on a raw socket, and a real peer session that joins it. With it a test can put any datagram into the
// peer that the host's own address would send: the peer only believes that address.
namespace
{
struct FakeHost
{
  vnet_loopback_network* net;
  vnet* hv;
  vnet* pv;
  int sock;
  vnet_addr peerAddr;
  std::unique_ptr<LockstepSession> peer;
  uint32_t session = 0x5E55;

  FakeHost()
  {
    net = vnet_loopback_network_create();
    auto make = [&]() {
      vnet_config cfg;
      vnet_config_init(&cfg);
      cfg.backend = VNET_BACKEND_LOOPBACK;
      cfg.loopback_network = net;
      cfg.ignore_env = 1;
      int err = 0;
      return vnet_create(&cfg, &err);
    };
    hv = make();
    pv = make();
    sock = vnet_open(hv, 0);
    vnet_addr local;
    vnet_local_addr(hv, sock, &local);
    char text[64];
    std::snprintf(text, sizeof(text), "127.1.0.1:%u", (unsigned) local.port);
    vnet_addr hostAddr;
    vnet_resolve(pv, text, &hostAddr);
    SessionOptions so;
    so.buildId = 0xB111D;
    so.dataHash = 0xDA7A;
    so.name = "peer";
    so.randomSeed = 5;
    std::string error;
    peer = LockstepSession::join(pv, hostAddr, so, &error);
  }
  ~FakeHost()
  {
    peer.reset();
    vnet_destroy(pv);
    vnet_destroy(hv);
    vnet_loopback_network_destroy(net);
  }
  void run(int ms)
  {
    for (int i = 0; i < ms; i++)
    {
      vnet_loopback_network_step(net, 1);
      peer->pump();
    }
  }
  template <typename Msg> void send(uint8_t type, const Msg& m)
  {
    netplay::Writer w;
    netplay::Header h;
    h.type = type;
    h.sender = netplay::HOST_ID;
    h.session = session;
    netplay::writeHeader(w, h);
    netplay::encode(w, m);
    vnet_sendto(hv, sock, w.bytes().data(), w.bytes().size(), &peerAddr);
  }
  // Answers the peer's HELLO and starts round 1 for it.
  bool welcomeAndStart()
  {
    uint8_t buf[VNET_MAX_DATAGRAM];
    uint32_t nonce = 0;
    for (int i = 0; i < 2000 && nonce == 0; i++)
    {
      run(1);
      const int n = vnet_recvfrom(hv, sock, buf, sizeof(buf), &peerAddr);
      netplay::Reader r(buf, n > 0 ? (size_t) n : 0);
      netplay::Header h;
      netplay::HelloMsg hello;
      if (n > 0 && netplay::readHeader(r, h) && h.type == netplay::MSG_HELLO && netplay::decode(r, hello)) nonce = hello.nonce;
    }
    if (nonce == 0) return false;
    netplay::WelcomeMsg w;
    w.nonce = nonce;
    w.peerId = 1;
    w.slotMask = 2;
    send(netplay::MSG_WELCOME, w);
    run(50);
    netplay::StartMsg s;
    s.round = 1;
    s.seed = 7;
    s.mapWidth = 13;
    s.mapHeight = 11;
    s.slots = {{0, 0, false}, {1, 1, false}};
    s.delay = 2;
    send(netplay::MSG_START, s);
    run(50);
    return peer->state() == LockstepSession::RUNNING;
  }
  void lobby(uint8_t phase, uint16_t round)
  {
    netplay::LobbyMsg l;
    l.phase = phase;
    l.round = round;
    l.slots = {{0, 0}, {1, 1}};
    netplay::LobbyPeer a, b;
    a.id = 0; a.slotMask = 1; a.name = "host"; a.ready = true;
    b.id = 1; b.slotMask = 2; b.name = "peer";
    l.peers = {a, b};
    send(netplay::MSG_LOBBY, l);
    run(50);
  }
};
}

TEST(LockstepLobby, APeerOnTheScoreboardFollowsOnlyALobbyAnnouncedForTheNextRound)
{
  FakeHost f;
  RoundStart rs;
  ASSERT_TRUE(f.welcomeAndStart());
  ASSERT_TRUE(f.peer->takeRoundStart(rs));
  f.peer->roundFinished();
  ASSERT_EQ(f.peer->state(), LockstepSession::BETWEEN_ROUNDS);   // round 1 is over here
  // A lobby packet of another round (an old one that arrived late, or a hostile one) must not pull it off the scoreboard.
  f.lobby(netplay::PHASE_LOBBY, 1);
  f.lobby(netplay::PHASE_LOBBY, 5);
  f.lobby(netplay::PHASE_LOBBY, 0);
  EXPECT_EQ(f.peer->state(), LockstepSession::BETWEEN_ROUNDS);
  f.lobby(netplay::PHASE_BETWEEN_ROUNDS, 2);   // the right round, but the host is not in its lobby
  EXPECT_EQ(f.peer->state(), LockstepSession::BETWEEN_ROUNDS);
  f.lobby(netplay::PHASE_LOBBY, 2);            // the lobby after round 1
  EXPECT_EQ(f.peer->state(), LockstepSession::LOBBY);
}
