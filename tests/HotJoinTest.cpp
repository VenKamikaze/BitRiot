// Disconnects and hot-join (brief 13), at the session level: N machines in one process on vnet's loopback network and
// virtual time, with the fake game of tests/support/SessionHarness.h on top of each. A machine that joins a round in
// progress receives the host's record, "fast-forwards" it (the harness steps without a clock while catchingUp()), says
// READY_AT, and is given a bot at a tick the host chose; a machine that reconnects with its token gets its old slots back;
// a spectator watches. The assertions compare what every machine stepped, tick for tick, from tick 1: the late joiner's
// log is the whole round, so "identical" includes the part it replayed.

#include <gtest/gtest.h>

#include "support/SessionHarness.h"

using namespace netharness;
using netplay::LockstepSession;

namespace
{
ClusterOptions delayed(uint32_t ms, const std::string& impair = "")
{
  ClusterOptions o;
  o.delayMs = ms;
  o.impair = impair;
  o.lobby.mapHeight = 19;
  return o;
}

// The first n ticks of every machine in `which` are the same, control events included.
void expectSameLogs(Cluster& c, const std::vector<int>& which, uint32_t n)
{
  const Machine& ref = c.machine(which[0]);
  ASSERT_GE(ref.log.size(), n);
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

int firstEvent(const Machine& m, int slot, ControlEvent::Kind kind, int after = 0)
{
  for (size_t i = (size_t) after; i < m.log.size(); i++)
  {
    for (const ControlEvent& e : m.log[i].control)
    {
      if (e.slot == slot && e.kind == kind) return (int) i + 1;
    }
  }
  return -1;
}

// The longest gap, in virtual milliseconds, between two consecutive steps of a machine within a tick range.
uint64_t longestGap(const Machine& m, uint32_t fromTick, uint32_t toTick)
{
  uint64_t worst = 0;
  for (uint32_t t = fromTick; t < toTick && t < m.stepTimes.size(); t++)
  {
    worst = std::max(worst, m.stepTimes[t] - m.stepTimes[t - 1]);
  }
  return worst;
}

bool joined(Machine& m) { return m.s->joinStage() == LockstepSession::JOIN_DONE; }
} // namespace

// -- a late joiner -------------------------------------------------------------------------------------------

TEST(HotJoin, ALateJoinerCatchesUpTakesOverABotAndEveryoneAgrees)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;   // the host, one peer, two bots
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(400); }, 60000));

  Machine& j = c.addMachine(1);
  const size_t joinerIndex = (size_t) c.size() - 1;
  ASSERT_TRUE(c.runUntil([&] { return j.s->state() == LockstepSession::RUNNING; }, 5000));
  EXPECT_EQ(j.s->joinStage(), LockstepSession::JOIN_CATCHING_UP);
  EXPECT_TRUE(j.s->catchingUp());
  EXPECT_EQ(j.round.localSlotMask, 0) << "a late joiner starts with no slot";
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_FALSE(j.s->catchingUp());
  EXPECT_NE(j.s->token(), 0u);
  EXPECT_EQ(j.s->grantedSlots(), 0b0100) << "the lowest bot (slot 2) is dealt";
  EXPECT_EQ(j.s->myId(), 2);
  const uint32_t ts = j.s->takeoverTick();
  EXPECT_GT(ts, 400u);

  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 240); }, 60000));
  const uint32_t n = ts + 240;
  expectSameLogs(c, {0, 1, (int) joinerIndex}, n);

  // The takeover is in the final inputs of exactly the tick the host named, on every machine.
  for (int i = 0; i < 3; i++)
  {
    EXPECT_EQ(firstEvent(c.machine(i), 2, ControlEvent::HUMAN_TAKES_OVER), (int) ts) << "machine " << i;
  }
  // From then on slot 2 carries the joiner's inputs, D ticks after they were sampled.
  const uint32_t d = (uint32_t) c.machine(0).round.delay;
  for (uint32_t t = ts + d + 4; t <= n; t++)
  {
    ASSERT_EQ(c.machine(0).log[t - 1].slot[2].bits, sampleByte(2, t, j.s->myId())) << "tick " << t;
  }
  // The live match did not wait for the joiner while it caught up: no step of the host took longer than a couple of ticks.
  EXPECT_LE(longestGap(c.machine(0), 380, ts + 100), 100u);
  EXPECT_LE(longestGap(c.machine(1), 380, ts + 100), 100u);
  // And it is a peer like the others now: the host waits for it (and says so if it stalls).
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
}

TEST(HotJoin, ALateJoinerThatTakesTwoSlotsGetsTwoBots)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 5;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  Machine& j = c.addMachine(2);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->grantedSlots(), 0b01100);
  const uint32_t ts = j.s->takeoverTick();
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 100); }, 60000));
  expectSameLogs(c, {0, 1, 2}, ts + 100);
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), (int) ts);
  EXPECT_EQ(firstEvent(c.machine(0), 3, ControlEvent::HUMAN_TAKES_OVER), (int) ts);
}

struct JoinLossCase
{
  const char* name;
  const char* impair;
};

class HotJoinLoss : public ::testing::TestWithParam<JoinLossCase>
{
};

TEST_P(HotJoinLoss, TheRecordReachesAJoinerWhateverTheLinkLoses)
{
  const JoinLossCase& lc = GetParam();
  ClusterOptions o = delayed(30, lc.impair);
  o.lobby.totalSlots = 4;
  o.seedBase = 4242;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(3, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(600); }, 240000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 120000)) << "the joiner never got its slot; stage " << (int) j.s->joinStage();
  const uint32_t ts = j.s->takeoverTick();
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 150); }, 240000));
  expectSameLogs(c, {0, 1, 2}, ts + 150);
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), (int) ts);
  EXPECT_FALSE(c.machine(0).s->desynced());
}

INSTANTIATE_TEST_SUITE_P(Impairment, HotJoinLoss,
  ::testing::Values(JoinLossCase{"clean", ""},
                    JoinLossCase{"loss5", "loss=5,dup=2,reorder=3,jit=10"},
                    JoinLossCase{"loss25", "loss=25,dup=4,reorder=6,jit=15"}),
  [](const ::testing::TestParamInfo<JoinLossCase>& info) { return std::string(info.param.name); });

TEST(HotJoin, AJoinerThatIsSlowToCatchUpNeverStallsTheLiveMatch)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(2500); }, 240000));
  Machine& j = c.addMachine(1);
  j.fastForwardPerMs = 1;   // 1000 ticks a second: a long way to go
  const size_t hostTicks0 = c.machine(0).log.size();
  c.run(1500);
  EXPECT_TRUE(j.s->catchingUp());
  EXPECT_LT(j.log.size(), 2000u);
  // A second and a half, thirty ticks a second: the live machines kept their pace the whole time.
  EXPECT_GE(c.machine(0).log.size() - hostTicks0, 1500u / 33 - 4);
  EXPECT_EQ(c.machine(0).s->waitingMask() & (1u << j.s->myId()), 0) << "the host is not waiting for the joiner";
  EXPECT_LE(longestGap(c.machine(0), (uint32_t) hostTicks0, (uint32_t) c.machine(0).log.size()), 66u);
  EXPECT_LE(longestGap(c.machine(1), (uint32_t) hostTicks0, (uint32_t) c.machine(1).log.size()), 66u);
  // It does get there.
  j.fastForwardPerMs = 200;
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 30000));
  expectSameLogs(c, {0, 1, 2}, 2500);
}

TEST(HotJoin, AFrozenJoinerIsDroppedByTheTimeoutAndNobodyWaited)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return j.s->state() == LockstepSession::RUNNING; }, 5000));
  j.frozen = true;
  const size_t before = c.machine(0).log.size();
  c.run(12000);
  EXPECT_GE(c.machine(0).log.size() - before, 12000u / 33 - 10);
  EXPECT_EQ(c.machine(0).s->peerCount(), 1) << "the silent joiner timed out";
}

// -- the takeover is a normal control event ------------------------------------------------------------------

TEST(HotJoin, NobodyElseCanClaimTheBotDealtToAJoinerBeforeItsTakeoverApplies)
{
  // Two bots (slots 2 and 3). The joiner is dealt slot 2. Peer 1 asks for slot 2 in the same window: the host refuses the claim
  // (the slot is reserved), peer 1's claim is rejected and told so, and the joiner has the slot.
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return j.s->joinStage() == LockstepSession::JOIN_WAITING || joined(j); }, 20000));
  // As soon as the joiner has said it is ready, peer 1 claims slot 2 for a tick shortly before the takeover.
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  const uint32_t ts = j.s->takeoverTick();
  Machine& p1 = c.machine(1);
  const uint32_t at = ts - (uint32_t) p1.round.delay;   // its record for tick ts, made while stepping ts - D
  if (p1.s->nextStepTick() <= at)
  {
    p1.wantEvents[at] = {ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER)};
    ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 100); }, 60000));
    // Peer 1 comes first in id order, but the slot is the joiner's: the event in tick ts is the joiner's, once.
    EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), (int) ts);
    int n = 0;
    for (const TickInputs& t : c.machine(0).log)
      for (const ControlEvent& e : t.control)
        if (e.slot == 2 && e.kind == ControlEvent::HUMAN_TAKES_OVER) n++;
    EXPECT_EQ(n, 1);
    EXPECT_EQ(c.machine(0).log[ts + 20].slot[2].bits, sampleByte(2, ts + 21, j.s->myId()));
  }
  else
  {
    GTEST_SKIP() << "the takeover was scheduled before peer 1 could claim (virtual time too quick)";
  }
}

TEST(HotJoin, WithNoBotFreeALateJoinerWatchesAndSendsNoInputs)
{
  ClusterOptions o = delayed(10);
  Cluster c(2, o);   // two humans, two slots: no bot
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(250); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->grantedSlots(), 0);
  EXPECT_TRUE(j.s->spectating());
  EXPECT_EQ(j.s->takeoverReason(), netplay::TAKEOVER_NO_FREE_BOT);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(700); }, 60000));
  expectSameLogs(c, {0, 1, 2}, 700);
  EXPECT_EQ(j.s->stats().inputRecordsSent, 0u) << "a spectator sends no inputs";
  EXPECT_EQ(c.machine(0).s->waitingMask() & (1u << j.s->myId()), 0);
  // The host does not wait for it: a frozen spectator costs the match nothing.
  const size_t before = c.machine(0).log.size();
  j.frozen = true;
  c.run(3000);
  EXPECT_GE(c.machine(0).log.size() - before, 3000u / 33 - 5);
  EXPECT_EQ(c.machine(0).s->waitingMask() & (1u << j.s->myId()), 0);
}

TEST(HotJoin, ASpectatorByChoiceIsNeverDealtABotEvenWhenOneIsFree)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& j = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.spectate = true; });
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->grantedSlots(), 0);
  EXPECT_EQ(j.s->takeoverReason(), netplay::TAKEOVER_SPECTATOR);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(600); }, 60000));
  expectSameLogs(c, {0, 1, 2}, 600);
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), -1);
  EXPECT_EQ(firstEvent(c.machine(0), 3, ControlEvent::HUMAN_TAKES_OVER), -1);
  EXPECT_EQ(j.s->stats().inputRecordsSent, 0u);
}

TEST(HotJoin, ASpectatorInTheLobbyIsNotDealtASlotNorWaitedForNorAskedToBeReady)
{
  ClusterOptions o = delayed(5);
  o.session.requireReady = true;
  Cluster c(2, o);
  Machine& s = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.spectate = true; });
  ASSERT_TRUE(c.runUntil([&] { return s.s->state() == LockstepSession::LOBBY; }, 3000));
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 3000)) << "the spectator was asked to be ready";
  ASSERT_TRUE(c.startRound(1, 0));
  EXPECT_EQ(c.machine(0).round.config.numPlayers(), 2) << "no slot for the spectator";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  expectSameLogs(c, {0, 1, 2}, 200);
  s.frozen = true;
  const size_t before = c.machine(0).log.size();
  c.run(3000);
  EXPECT_GE(c.machine(0).log.size() - before, 3000u / 33 - 5);
}

TEST(HotJoin, AJoinerTooFarAwayForTheRoundsDelayWatchesInsteadOfStallingEveryone)
{
  // D was chosen for the players the round started with (a round trip of about 20 ms: D = 2). A machine 300 ms away would make
  // the host wait for it on every tick; the host does not give it a slot, and says why.
  ClusterOptions o = delayed(5);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  const int d = c.machine(0).round.delay;
  ASSERT_LE(d, 3) << c.machine(0).s->delayNote();
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1);
  c.impair(c.size() - 1, "lat=150");   // 150 ms each way for what it sends
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 30000));
  EXPECT_EQ(j.s->grantedSlots(), 0) << "no slot for a connection the delay cannot carry";
  EXPECT_EQ(j.s->takeoverReason(), netplay::TAKEOVER_TOO_SLOW);
  const size_t before = c.machine(0).log.size();
  c.run(3000);
  EXPECT_GE(c.machine(0).log.size() - before, 3000u / 33 - 5) << "the live match kept its pace";
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), -1);
  expectSameLogs(c, {0, 1, 2}, 400);
}

// -- a hash that disagrees -----------------------------------------------------------------------------------

TEST(HotJoin, ALateJoinerWithTheWrongStateIsStoppedAtItsFirstCheckpointAndTheMatchGoesOn)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1);
  j.hashFor = [](uint32_t t) { return 0xBAD0000ull + t; };   // its simulation is not the same as everyone's
  ASSERT_TRUE(c.runUntil([&] { return j.s->desynced(); }, 20000)) << "the joiner was never told";
  EXPECT_EQ(j.s->grantedSlots(), 0) << "it never got a slot";
  EXPECT_GT(j.s->desyncInfo().tick, 0u);
  EXPECT_LE(j.s->desyncInfo().tick, 360u) << "stopped at the first checkpoints, not later";
  // The live match is untouched: no desync, no stall, nobody was dropped but the joiner.
  const size_t before = c.machine(0).log.size();
  c.run(3000);
  EXPECT_FALSE(c.machine(0).s->desynced());
  EXPECT_FALSE(c.machine(1).s->desynced());
  EXPECT_GE(c.machine(0).log.size() - before, 3000u / 33 - 5);
  expectSameLogs(c, {0, 1}, (uint32_t) before + 50);
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), -1);
}

TEST(HotJoin, AJoinerIsOnlyGivenASlotOnceItsStateAgreesWithTheHosts)
{
  // A joiner that reports nothing about its state never gets a slot (and is dropped after the timeout), whatever it says
  // about being ready.
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1);
  j.reportHashes = false;
  c.run(9000);
  EXPECT_FALSE(joined(j));
  EXPECT_EQ(j.s->grantedSlots(), 0);
  ASSERT_TRUE(c.runUntil([&] { return j.s->ended(); }, 5000));
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
  EXPECT_FALSE(c.machine(0).s->desynced());
}

TEST(HotJoin, AJoinerBeforeTheFirstCheckpointIsVerifiedAtTheFirstLiveOne)
{
  ClusterOptions o = delayed(5);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(6); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_GE(j.s->takeoverTick(), 30u) << "it waited for a checkpoint to agree first";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(j.s->takeoverTick() + 60); }, 60000));
  expectSameLogs(c, {0, 1, 2}, j.s->takeoverTick() + 60);
}

// -- rounds ------------------------------------------------------------------------------------------------

TEST(HotJoin, AMachineThatArrivesOnTheScoreboardJoinsTheNextRoundLikeAnyoneElse)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 3;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 90));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(90); }, 60000));
  ASSERT_EQ(c.machine(0).s->state(), LockstepSession::BETWEEN_ROUNDS);
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return j.s->state() == LockstepSession::LOBBY; }, 3000));
  EXPECT_EQ(j.s->joinStage(), LockstepSession::JOIN_NONE);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 10000));
  ASSERT_TRUE(c.startRound(2, 0));
  EXPECT_EQ(j.s->joinStage(), LockstepSession::JOIN_NONE) << "a normal member of round 2, not a late joiner";
  EXPECT_NE(j.round.localSlotMask, 0) << "dealt a slot by START";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120); }, 60000));
  expectSameLogs(c, {0, 1, 2}, 120);
}

TEST(HotJoin, ATakeoverMessageThatArrivesAfterTheJoinerPassedItsTickIsBackfilledAndTheMatchResumes)
{
  // TAKEOVER_AT is lost on the way (the joiner drops what arrives in the first 600 ms, a test knob): it steps on to the tick the
  // host waits for before it knows it was given a slot, so the records of the ticks it already passed are neutral ones. Without
  // them the host waits for a record nobody will ever make.
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.testDropTakeoverForMs = 600; });
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  const uint32_t ts = j.s->takeoverTick();
  EXPECT_GT(j.s->nextStepTick(), ts - (uint32_t) j.round.delay) << "it had stepped past the tick of its first record";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 150); }, 60000)) << "the match never resumed";
  expectSameLogs(c, {0, 1, 2}, ts + 150);
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), (int) ts);
}

TEST(HotJoin, AJoinerThatSaysItIsReadyFarBehindTheLiveTickIsNotGivenASlot)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.testReadyAtLag = 200; });
  c.run(4000);
  EXPECT_FALSE(joined(j)) << "a joiner 200 ticks behind could not make its first record in time";
  EXPECT_EQ(j.s->grantedSlots(), 0);
}

TEST(HotJoin, ABotTheHostSaysIsNotWorthTakingIsNotDealt)
{
  // The game's host narrows the choice to bots that are alive (NetMatch's filter); the session just asks.
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  c.machine(0).s->setSlotFilter([](int slot) { return slot != 2; });   // slot 2 is dead
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->grantedSlots(), 0b1000) << "slot 3, the only live bot";
}

TEST(HotJoin, ARoundPastTheHistoryTheHostKeepsCannotBeJoinedAnyMore)
{
  ClusterOptions o = delayed(5);
  o.session.historyLimit = 250;
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  Machine& early = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(early); }, 20000)) << "within the limit it is fine";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(400); }, 60000));
  Machine& late = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return late.s->ended(); }, 5000));
  EXPECT_EQ(late.s->rejectInfo().reason, netplay::REJECT_IN_PROGRESS);
  EXPECT_NE(late.s->rejectInfo().text.find("too long"), std::string::npos) << late.s->rejectInfo().text;
}

TEST(HotJoin, ANewRoundBeginsForAJoinerStillCatchingUpOnTheOldOne)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 240));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  Machine& j = c.addMachine(1);
  j.fastForwardPerMs = 0;   // it does not manage to step at all
  c.machine(0).roundTicks = 240;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS
                                      && c.machine(1).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  EXPECT_TRUE(j.s->catchingUp());
  // The host does not wait for it to start the next round.
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 20000));
  j.fastForwardPerMs = 200;
  ASSERT_TRUE(c.startRound(2, 0));
  EXPECT_EQ(j.round.round, 2);
  EXPECT_EQ(j.s->joinStage(), LockstepSession::JOIN_NONE);
  EXPECT_NE(j.round.localSlotMask, 0);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  expectSameLogs(c, {0, 1, 2}, 150);
}

TEST(HotJoin, AHostThatDoesNotTakeLateJoinersAnswersInProgress)
{
  ClusterOptions o = delayed(5);
  o.session.allowHotJoin = false;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  Machine& late = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.allowHotJoin = true; });
  ASSERT_TRUE(c.runUntil([&] { return late.s->ended(); }, 5000));
  EXPECT_EQ(late.s->rejectInfo().reason, netplay::REJECT_IN_PROGRESS);
}

// -- reconnecting ---------------------------------------------------------------------------------------------

TEST(Reconnect, APeerThatComesBackWithItsTokenFromAnotherAddressGetsItsSlotBackWhileTheStaleOneIsStillRegistered)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(3, o);   // the host (slot 0), peer 1 (slot 1), peer 2 (slot 2), one bot (slot 3)
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  const uint32_t token = c.machine(1).s->token();
  ASSERT_NE(token, 0u);
  c.machine(1).frozen = true;   // the process dies: nothing is sent, nothing is told
  c.run(500);                   // far inside the host's timeout: the stale address is still registered
  ASSERT_EQ(c.machine(0).s->peerCount(), 2);

  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; so.name = "peer1"; });
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->myId(), 1) << "the same peer id";
  EXPECT_EQ(back.round.localSlotMask, 0) << "its old slot is in START's owners but a bot now: the slots come from TAKEOVER_AT only";
  EXPECT_EQ(back.s->token(), token) << "the same token";
  EXPECT_EQ(back.s->offeredSlots(), 0b0010);
  EXPECT_EQ(back.s->grantedSlots(), 0b0010) << "its old slot, not the free bot";
  EXPECT_EQ(c.machine(0).s->peerCount(), 2) << "one peer, not two";
  const int backIndex = c.size() - 1;
  const uint32_t ts = back.s->takeoverTick();
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 200, {0, 2, backIndex}); }, 60000));
  expectSameLogs(c, {0, 2, backIndex}, ts + 200);
  // Slot 1 went back to the AI when the host saw the restart, and was handed to the new incarnation at the agreed tick.
  const int back_to_ai = firstEvent(c.machine(0), 1, ControlEvent::RETURN_TO_AI, 290);
  EXPECT_GT(back_to_ai, 0);
  EXPECT_LT(back_to_ai, (int) ts);
  EXPECT_EQ(firstEvent(c.machine(0), 1, ControlEvent::HUMAN_TAKES_OVER, 290), (int) ts);
  for (uint32_t t = ts + 8; t <= ts + 200; t++)
  {
    ASSERT_EQ(c.machine(0).log[t - 1].slot[1].bits, sampleByte(1, t, 1)) << "tick " << t;
  }
  EXPECT_FALSE(c.machine(0).s->desynced());
}

TEST(Reconnect, TheOldSlotIsPreferredOverALowerFreeBot)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(3, o);   // host (0), peer 1 (1), peer 2 (2), a bot (3)
  ASSERT_TRUE(c.startRound(1, 0));
  c.machine(1).wantEvents[100] = {ControlEvent(1, ControlEvent::RETURN_TO_AI)};   // slot 1 is a bot now: free bots are 1 and 3
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(250); }, 60000));
  ASSERT_GT(firstEvent(c.machine(0), 1, ControlEvent::RETURN_TO_AI), 0);
  const uint32_t token = c.machine(2).s->token();
  c.machine(2).frozen = true;
  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->myId(), 2);
  EXPECT_EQ(back.s->grantedSlots(), 0b0100) << "its own slot 2, not the lowest free bot (slot 1)";
}

TEST(Reconnect, ASlotThatWasTakenInTheMeantimeIsNotOfferedBack)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  const uint32_t token = c.machine(1).s->token();
  c.machine(1).frozen = true;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 1; }, 15000));   // timed out: its slot is a bot
  ASSERT_TRUE(c.runUntil([&] { return firstEvent(c.machine(0), 1, ControlEvent::RETURN_TO_AI) > 0; }, 3000));
  // Peer 2 takes slot 1 over while its owner is away.
  const uint32_t at = c.machine(2).s->nextStepTick() + 3;
  c.machine(2).wantEvents[at] = {ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)};
  ASSERT_TRUE(c.runUntil([&] { return firstEvent(c.machine(0), 1, ControlEvent::HUMAN_TAKES_OVER) > 0; }, 5000));

  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->myId(), 1);
  EXPECT_EQ(back.s->offeredSlots(), 0b0010);
  EXPECT_EQ(back.s->grantedSlots(), 0b1000) << "slot 1 is someone else's now: it gets the free bot";
}

TEST(Reconnect, ATokenTheHostDoesNotKnowIsJustANewPlayer)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& j = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.reconnectToken = 0xDEADBEEF; });
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->myId(), 2);
  EXPECT_NE(j.s->token(), 0xDEADBEEFu);
  EXPECT_EQ(j.s->offeredSlots(), 0);
  EXPECT_EQ(j.s->grantedSlots(), 0b0100);
}

TEST(Reconnect, AMachineThatRestartsInTheLobbyKeepsItsPeerIdAndTokenWhateverItsAddress)
{
  Cluster c(3, delayed(5));
  const uint32_t token = c.machine(2).s->token();
  c.machine(2).frozen = true;
  Machine& again = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return again.s->state() == LockstepSession::LOBBY; }, 5000));
  EXPECT_EQ(again.s->myId(), 2);
  EXPECT_EQ(again.s->token(), token);
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  c.machine(0).roundTicks = 0;
  ASSERT_TRUE(c.machine(0).s->startRound(1, nullptr));   // (Cluster::startRound would wait for the frozen old machine too)
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() >= 100 && again.log.size() >= 100 && c.machine(1).log.size() >= 100; }, 60000));
  expectSameLogs(c, {0, 1, 3}, 100);
}

TEST(Reconnect, APeerWhoWasDroppedButIsStillTalkingIsToldAndMayThenReconnect)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 3;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  const uint32_t token = c.machine(1).s->token();
  c.machine(1).paused = true;       // it stops stepping...
  c.impair(1, "loss=100");          // ...and nothing it sends gets through
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 0; }, 15000));
  c.impair(1, "");
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended(); }, 5000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_KICKED);
  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->myId(), 1);
  EXPECT_EQ(back.s->grantedSlots(), 0b0010);
}

// -- removing and leaving mid-round ------------------------------------------------------------------------------

TEST(HotJoin, TheHostCanRemoveAPeerInTheMiddleOfARoundAndItsSlotGoesToTheAi)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 3;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  const uint32_t token = c.machine(1).s->token();
  EXPECT_TRUE(c.machine(0).s->kick(1));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(1).s->ended(); }, 3000));
  EXPECT_EQ(c.machine(1).s->endReason(), LockstepSession::END_REMOVED);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(400); }, 60000));
  expectSameLogs(c, {0, 2}, 400);
  EXPECT_GT(firstEvent(c.machine(0), 1, ControlEvent::RETURN_TO_AI), 0);
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
  // Its token is dead: coming back with it is being a new player, with nothing offered.
  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->offeredSlots(), 0);
}

// -- a lost WELCOME, and a peer that never reports ------------------------------------------------------------------------

namespace
{
std::vector<std::pair<uint8_t, int>> rawAnswers(Cluster::Raw& raw)
{
  std::vector<std::pair<uint8_t, int>> out;
  uint8_t buf[VNET_MAX_DATAGRAM];
  vnet_addr from;
  for (int n; (n = vnet_recvfrom(raw.v, raw.sock, buf, sizeof(buf), &from)) > 0;)
  {
    netplay::Reader r(buf, (size_t) n);
    netplay::Header h;
    if (!netplay::readHeader(r, h)) continue;
    int detail = -1;
    if (h.type == netplay::MSG_LEAVE)
    {
      netplay::LeaveMsg l;
      if (netplay::decode(r, l)) detail = l.reason;
    }
    out.push_back(std::make_pair(h.type, detail));
  }
  return out;
}

void rawHelloWithNonce(Cluster& c, Cluster::Raw& raw, uint32_t nonce)
{
  netplay::Writer w;
  netplay::Header h;
  h.type = netplay::MSG_HELLO;
  netplay::writeHeader(w, h);
  netplay::HelloMsg m;
  m.buildId = 0xB111D;
  m.dataHash = 0xDA7A;
  m.nonce = nonce;
  m.name = "raw";
  netplay::encode(w, m);
  vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
  c.run(100);
}
} // namespace

TEST(LockstepLobby, ARemovedMachineThatNeverGotItsWelcomeIsTurnedAwayWhenItRepeatsItsHelloButANewNonceGetsIn)
{
  // The re-review's nit on 08: a machine still CONNECTING whose WELCOME was lost does not know it has joined. The host removes
  // it; its next HELLO (same nonce) must not simply join it again. A restarted game (a new nonce) is not a repeat and gets in.
  Cluster c(1);
  Cluster::Raw raw = c.openRaw();
  rawHelloWithNonce(c, raw, 777);
  rawAnswers(raw);   // the WELCOME is "lost"
  ASSERT_EQ(c.machine(0).s->peerCount(), 1);
  ASSERT_TRUE(c.machine(0).s->kick(1));
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
  rawAnswers(raw);   // so are the LEAVEs
  rawHelloWithNonce(c, raw, 777);
  bool removed = false;
  for (const auto& a : rawAnswers(raw))
  {
    if (a.first == netplay::MSG_LEAVE && a.second == netplay::LEAVE_REMOVED) removed = true;
    EXPECT_NE(a.first, netplay::MSG_WELCOME) << "it was let in again";
  }
  EXPECT_TRUE(removed);
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
  rawHelloWithNonce(c, raw, 778);
  bool welcomed = false;
  for (const auto& a : rawAnswers(raw)) welcomed = welcomed || a.first == netplay::MSG_WELCOME;
  EXPECT_TRUE(welcomed);
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
}

TEST(LockstepLobby, AJoiningSessionStillConnectingEndsWhenTheHostRemovedIt)
{
  Cluster c(1);
  // The WELCOME to the peer is lost on the way (the host's sends are dropped for a moment); then the host removes it.
  c.impair(0, "loss=100");
  Machine& p = c.addMachine(1);
  c.run(600);
  ASSERT_EQ(p.s->state(), LockstepSession::CONNECTING);
  ASSERT_EQ(c.machine(0).s->peerCount(), 1);
  ASSERT_TRUE(c.machine(0).s->kick(1));
  c.impair(0, "");
  ASSERT_TRUE(c.runUntil([&] { return p.s->ended(); }, 3000));
  EXPECT_EQ(p.s->endReason(), LockstepSession::END_REMOVED);
}


// -- the review fix round ---------------------------------------------------------------------------------------------

TEST(HotJoin, ASpectatorOrWaitingJoinerWhoClaimsABotIsToldItWasNotGranted)
{
  // Without this the local layer binds a keyboard set to a bot the simulation never gives it (and the set is gone when the real slot
  // is dealt).
  ClusterOptions o = delayed(10);
  Cluster c(2, o);   // no bot
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  ASSERT_EQ(j.s->grantedSlots(), 0);
  const uint32_t at = j.s->nextStepTick() + 2;
  j.wantEvents[at] = {ControlEvent(1, ControlEvent::HUMAN_TAKES_OVER)};
  ASSERT_TRUE(c.runUntil([&] { return j.s->nextStepTick() > at; }, 5000));
  const std::vector<int> rejected = j.s->takeRejectedClaims();
  ASSERT_EQ(rejected.size(), 1u);
  EXPECT_EQ(rejected[0], 1);
}

TEST(HotJoin, ALateJoinerStillCatchingUpFollowsTheHostBackToTheLobbyAndTheReadyGateOpens)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  o.session.requireReady = true;
  Cluster c(2, o);
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000));
  ASSERT_TRUE(c.startRound(1, 300));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  Machine& j = c.addMachine(1);
  j.fastForwardPerMs = 0;   // it never gets anywhere
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS
                                      && c.machine(1).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 20000));
  ASSERT_TRUE(c.machine(0).s->returnToLobby());
  ASSERT_TRUE(c.runUntil([&] { return j.s->state() == LockstepSession::LOBBY; }, 5000)) << "stuck in the old round";
  EXPECT_EQ(j.s->joinStage(), LockstepSession::JOIN_NONE);
  c.machine(1).s->setReady(true);
  j.s->setReady(true);
  EXPECT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000)) << "Start stays blocked: " << c.machine(0).s->cannotStartBecause();
}

TEST(HotJoin, ALateJoinerThatNeverFinishesCatchingUpIsNotDroppedForMissingTheLastChecksum)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 200));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(100); }, 60000));
  Machine& j = c.addMachine(1);
  j.fastForwardPerMs = 0;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  c.run(12000);   // past finalHashWaitMs
  EXPECT_EQ(c.machine(0).s->peerCount(), 2) << "it is not waited for, so it is not dropped for what it could not report";
  EXPECT_FALSE(j.s->ended());
}

TEST(Reconnect, TwoLiveMachinesWithTheSameTokenAreTwoPlayersAndNeitherEvictsTheOther)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);   // the host and peer 1, both running
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  const uint32_t token = c.machine(1).s->token();
  Machine& twin = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  ASSERT_TRUE(c.runUntil([&] { return joined(twin); }, 20000));
  EXPECT_NE(twin.s->myId(), 1) << "a second player, not the first one again";
  EXPECT_NE(twin.s->token(), token);
  EXPECT_EQ(twin.s->offeredSlots(), 0);
  EXPECT_FALSE(c.machine(1).s->ended());
  c.run(2000);
  EXPECT_FALSE(c.machine(1).s->ended());
  EXPECT_EQ(c.machine(0).s->peerCount(), 2 + 0 + 1 - 1 + 0) << "peer 1 and the twin";
}

TEST(Reconnect, ATokenFromADeadMachineIsHonouredOnceItHasBeenQuietForASecond)
{
  // The crash-restart counterpart of the test above: the old process is silent, the new one reclaims.
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(3, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  const uint32_t token = c.machine(2).s->token();
  c.machine(2).frozen = true;
  Machine& back = c.addMachine(1, 0, 0, [&](netplay::SessionOptions& so) { so.reconnectToken = token; });
  const uint64_t t0 = c.now();
  ASSERT_TRUE(c.runUntil([&] { return joined(back); }, 20000));
  EXPECT_EQ(back.s->myId(), 2);
  EXPECT_EQ(back.s->offeredSlots(), 0b0100);
  EXPECT_GE(c.now() - t0, 2500u) << "held back until the old one has been quiet for 3 s (a throttled browser tab pumps about once a second)";
}

TEST(HotJoin, ASpectatorWhoseStateIsWrongIsStoppedAlone)
{
  ClusterOptions o = delayed(10);
  Cluster c(2, o);
  Machine& s = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.spectate = true; });
  s.hashFor = [](uint32_t t) { return 0xBAD0000ull + t; };
  ASSERT_TRUE(c.runUntil([&] { return s.s->state() == LockstepSession::LOBBY; }, 3000));
  c.machine(0).roundTicks = 0;
  ASSERT_TRUE(c.machine(0).s->startRound(1, nullptr));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() >= 400 && c.machine(1).log.size() >= 400; }, 60000));
  EXPECT_FALSE(c.machine(0).s->desynced()) << "a spectator decides nothing";
  EXPECT_FALSE(c.machine(1).s->desynced());
  EXPECT_TRUE(s.s->desynced()) << "it was told";
  EXPECT_EQ(c.machine(0).s->peerCount(), 1);
}

TEST(HotJoin, ABotReservedForAJoinerThatVanishedBeforeItsTickIsFreeAgain)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 3;   // the host, peer 1, one bot
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(200); }, 60000));
  Machine& a = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(a); }, 20000));   // the host dealt it the bot, for a tick some way ahead...
  EXPECT_EQ(a.s->grantedSlots(), 0b0100);
  a.frozen = true;                                             // ...and it is gone before that tick
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 1; }, 20000));
  EXPECT_EQ(firstEvent(c.machine(0), 2, ControlEvent::HUMAN_TAKES_OVER), -1) << "the takeover never applied";
  Machine& b = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(b); }, 20000));
  EXPECT_EQ(b.s->grantedSlots(), 0b0100) << "the bot reserved for the vanished joiner is free again";
}

TEST(HotJoin, TheTakeoverTickIsFarEnoughAheadThatTheLiveMatchDoesNotStallAtIt)
{
  ClusterOptions o = delayed(40);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(300); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  const uint32_t ts = j.s->takeoverTick();
  EXPECT_GE(ts, j.s->catchUpHead() + (uint32_t) j.round.delay + 2) << "the takeover is scheduled a delay and a round trip past the head";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(ts + 100); }, 60000));
  EXPECT_LE(longestGap(c.machine(0), ts - 20, ts + 60), 70u) << "the host waited for the joiner at its takeover tick";
  EXPECT_LE(longestGap(c.machine(1), ts - 20, ts + 60), 70u);
}

TEST(HotJoin, TheRecordIsStreamedInAWindowNotAllAtOnce)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(4000); }, 400000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 60000));
  const uint64_t inFlight = c.machine(0).s->stats().maxCatchUpInFlight;
  EXPECT_GT(inFlight, 300u) << "a window was streamed";
  EXPECT_LE(inFlight, 1100u) << "no more than the window (1,000 ticks and one datagram) beyond what the joiner acknowledged";
}

TEST(LockstepLobby, ASpectatorDoesNotUseUpASlotOfTheSixWhenAnotherMachineAsksToJoin)
{
  Cluster c(4);      // four players
  Machine& s = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.spectate = true; });
  ASSERT_TRUE(c.runUntil([&] { return s.s->state() == LockstepSession::LOBBY; }, 3000));
  Machine& two = c.addMachine(2);   // 4 + 2 = six: fits, unless the spectator is counted
  c.run(1500);
  EXPECT_EQ(two.s->state(), LockstepSession::LOBBY) << "the spectator was counted among the humans";
  Machine& seventh = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return seventh.s->ended(); }, 5000));
  EXPECT_EQ(seventh.s->rejectInfo().reason, netplay::REJECT_FULL);
}

TEST(HotJoin, ALateJoinerThatTheSlotsRanOutForWatchesTheNextRoundToo)
{
  ClusterOptions o = delayed(10);
  o.localPlayers[0] = 2;
  Cluster c(5, o);   // six players, no bot
  ASSERT_TRUE(c.startRound(1, 120));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  EXPECT_EQ(j.s->grantedSlots(), 0);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 30000));
  ASSERT_TRUE(c.startRound(2, 0));
  EXPECT_EQ(j.round.localSlotMask, 0) << "no slot left for it in round 2 either";
  j.frozen = true;
  const size_t before = c.machine(0).log.size();
  c.run(2000);
  EXPECT_GE(c.machine(0).log.size() - before, 2000u / 33 - 5) << "it is a spectator: not waited for";
}

TEST(LockstepLobby, AMachineRemovedInTheMiddleOfARoundWhoseWelcomeWasLostIsTurnedAwayToo)
{
  Cluster c(1);
  ASSERT_TRUE(c.startRound(1, 0));
  c.run(300);
  Cluster::Raw raw = c.openRaw();
  rawHelloWithNonce(c, raw, 4242);
  rawAnswers(raw);   // the WELCOME_MIDMATCH is "lost"
  ASSERT_EQ(c.machine(0).s->peerCount(), 1);
  ASSERT_TRUE(c.machine(0).s->kick(1));
  rawAnswers(raw);
  rawHelloWithNonce(c, raw, 4242);
  bool removed = false;
  for (const auto& a : rawAnswers(raw))
  {
    if (a.first == netplay::MSG_LEAVE && a.second == netplay::LEAVE_REMOVED) removed = true;
    EXPECT_NE(a.first, netplay::MSG_WELCOME_MIDMATCH) << "it was let back in mid-round";
  }
  EXPECT_TRUE(removed);
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
}

TEST(HotJoin, ADemotedLateJoinerIsDealtASlotAgainWhenOneIsFree)
{
  // Six humans, a seventh joins late with no bot: it watches. Round 2 has no seat for it either (it is told: no slot in START, no
  // records, a "Watching" state). A player leaves between rounds: in round 3 it is dealt that seat. It stayed a spectator for the
  // rest of the session before this fix.
  ClusterOptions o = delayed(10);
  o.localPlayers[0] = 2;
  Cluster c(5, o);
  ASSERT_TRUE(c.startRound(1, 120));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(60); }, 60000));
  Machine& j = c.addMachine(1);
  ASSERT_TRUE(c.runUntil([&] { return joined(j); }, 20000));
  ASSERT_EQ(j.s->grantedSlots(), 0);
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120, {0, 1, 2, 3, 4}) && c.machine(0).s->canStartRound(); }, 30000));
  ASSERT_TRUE(c.startRound(2, 120));
  EXPECT_EQ(j.round.localSlotMask, 0) << "no seat in round 2";
  EXPECT_TRUE(j.s->spectating()) << "and it knows it watches (the banner)";
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(120); }, 60000));
  EXPECT_EQ(j.s->stats().inputRecordsSent, 0u) << "a watcher sends no records";
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 30000));
  c.machine(4).s->leave();   // a seat frees up
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->peerCount() == 4 && c.machine(0).s->canStartRound(); }, 10000));
  ASSERT_TRUE(c.startRound(3, 0));
  EXPECT_NE(j.round.localSlotMask, 0) << "round 3 has a seat for it";
  EXPECT_FALSE(j.s->spectating());
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(150); }, 60000));
  expectSameLogs(c, {0, 1, 2, 3, 5}, 150);
}

TEST(HotJoin, AByChoiceSpectatorFromTheLobbyFollowsTheHostBackToTheLobbyWhileItIsStillInTheRound)
{
  ClusterOptions o = delayed(10);
  o.session.requireReady = true;
  Cluster c(2, o);
  Machine& s = c.addMachine(1, 0, 0, [](netplay::SessionOptions& so) { so.spectate = true; });
  ASSERT_TRUE(c.runUntil([&] { return s.s->state() == LockstepSession::LOBBY; }, 3000));
  c.machine(1).s->setReady(true);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 5000));
  c.machine(0).roundTicks = 0;
  ASSERT_TRUE(c.machine(0).s->startRound(1, nullptr));
  s.fastForwardPerMs = 0;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() >= 100 && c.machine(1).log.size() >= 100; }, 60000));
  // End the round for the players only; the spectator has not stepped it (it is "still in the round").
  c.machine(0).roundTicks = 100;
  c.machine(1).roundTicks = 100;
  s.paused = true;
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS
                                      && c.machine(1).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  ASSERT_EQ(s.s->state(), LockstepSession::RUNNING);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 20000));
  ASSERT_TRUE(c.machine(0).s->returnToLobby());
  EXPECT_TRUE(c.runUntil([&] { return s.s->state() == LockstepSession::LOBBY; }, 5000)) << "the spectator stayed in the old round";
}

TEST(HotJoin, ALateJoinerThatSaidReadyButWasNeverVerifiedIsNotDroppedInTheLobbyLater)
{
  ClusterOptions o = delayed(10);
  o.lobby.totalSlots = 4;
  Cluster c(2, o);
  ASSERT_TRUE(c.startRound(1, 200));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(40); }, 60000));
  Machine& j = c.addMachine(1);
  j.reportHashes = false;   // caught up and ready, but its state is never compared
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->state() == LockstepSession::BETWEEN_ROUNDS; }, 60000));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 30000));
  ASSERT_TRUE(c.machine(0).s->returnToLobby());
  c.run(14000);   // longer than the never-verified timer, which belongs to the round
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  EXPECT_FALSE(j.s->ended());
}

TEST(LockstepSession, APeerThatNeverReportsTheLastChecksumIsDroppedAndTheNextRoundCanStart)
{
  // 07's review, R3: canStartRound() waited for every peer's final-tick hash with no limit. The round is 75 ticks long (the last
  // tick is not a multiple of 30), the peer acknowledges everything and says nothing about its state.
  ClusterOptions o = delayed(10);
  Cluster c(3, o);
  c.machine(2).reportHashes = false;
  ASSERT_TRUE(c.startRound(1, 75));
  ASSERT_TRUE(c.runUntil([&] { return c.allStepped(75); }, 60000));
  c.run(5000);
  EXPECT_FALSE(c.machine(0).s->canStartRound()) << "still waiting for the hash";
  EXPECT_EQ(c.machine(0).s->peerCount(), 2);
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).s->canStartRound(); }, 10000));
  EXPECT_EQ(c.machine(0).s->peerCount(), 1) << "dropped";
  ASSERT_TRUE(c.runUntil([&] { return c.machine(2).s->ended(); }, 5000));
  EXPECT_EQ(c.machine(2).s->endReason(), LockstepSession::END_KICKED);
  ASSERT_TRUE(c.startRound(2, 0));
  ASSERT_TRUE(c.runUntil([&] { return c.machine(0).log.size() >= 60 && c.machine(1).log.size() >= 60; }, 60000));
  expectSameLogs(c, {0, 1}, 60);
}

TEST(LockstepSession, AVersionTwoHelloIsRefusedWithRejectVersion)
{
  // Version 3 changed HELLO (a flags byte) and added three messages: a build still speaking version 2 must be told so.
  Cluster c(1);
  Cluster::Raw raw = c.openRaw();
  netplay::Writer w;
  netplay::Header h;
  h.type = netplay::MSG_HELLO;
  netplay::writeHeader(w, h);
  w.u16(2);
  w.u64(0xB111D);
  w.u64(0xDA7A);
  w.u8(1);
  w.u32(5);
  w.u32(0);
  w.str("old");   // the version-2 layout: no flags byte
  vnet_sendto(raw.v, raw.sock, w.bytes().data(), w.bytes().size(), &c.hostAddr());
  c.run(300);
  uint8_t buf[VNET_MAX_DATAGRAM];
  vnet_addr from;
  const int n = vnet_recvfrom(raw.v, raw.sock, buf, sizeof(buf), &from);
  ASSERT_GT(n, 0);
  netplay::Reader r(buf, (size_t) n);
  netplay::Header rh;
  ASSERT_TRUE(netplay::readHeader(r, rh));
  ASSERT_EQ(rh.type, netplay::MSG_REJECT);
  netplay::RejectMsg rj;
  ASSERT_TRUE(netplay::decode(r, rj));
  EXPECT_EQ(rj.reason, netplay::REJECT_VERSION);
  EXPECT_EQ(rj.hostProtocol, 3);
  EXPECT_NE(rj.text.find("protocol version 2"), std::string::npos) << rj.text;
  EXPECT_EQ(c.machine(0).s->peerCount(), 0);
}

// -- NetMatch: the fast-forward keeps no state dumps ------------------------------------------------------------------------

#include "MatchRecord.h"
#include "NetMatch.h"
#include "support/HeadlessGame.h"

namespace
{
class NetMatchFastForward : public bitriot_test::HeadlessGameTest
{
};
}

TEST_F(NetMatchFastForward, StepsWithoutDevicesKeepsNoDumpsButStillRecordsAndReportsTheCheckpoints)
{
  // A host alone: the simulation and a session in one process. fastForward() is what a late joiner does with the record it
  // received; here it just has to step, record, and not pay for a state dump at every checkpoint (they are what makes a long
  // fast-forward slow, and only the last ones are of any use).
  netharness::ClusterOptions o;
  o.lobby.totalSlots = 2;
  netharness::Cluster c(1, o);
  LockstepSession& s = *c.machine(0).s;
  std::string error;
  ASSERT_TRUE(s.startRound(7, &error)) << error;
  netplay::RoundStart rs;
  ASSERT_TRUE(s.takeRoundStart(rs));
  netplay::NetMatch match(s, sim());
  match.beginRound(rs);
  uint32_t stepped = 0;
  while (stepped < 400)
  {
    s.pump();
    const uint32_t n = match.fastForward(1000, 0);
    stepped += n;
    ASSERT_TRUE(n > 0 || stepped >= 400);
  }
  EXPECT_EQ(sim().tick(), stepped);
  EXPECT_EQ(match.record().ticks.size(), stepped) << "the record has every tick";
  EXPECT_GE(match.record().checkpoints.size(), stepped / 30u) << "and its checkpoints";
  EXPECT_TRUE(match.keptDumpTicks().empty()) << "no dump was made on the way";
  EXPECT_EQ(match.lastCheckpointTick(), (stepped / 30) * 30) << "a hash is still taken (and reported) at every checkpoint";
  // The ordinary step keeps them (the evidence of a desync).
  s.pump();
  ASSERT_TRUE(s.canStep());
  match.step(TickInputs(), 0);
  while (match.keptDumpTicks().empty() && match.active())
  {
    s.pump();
    if (s.canStep()) match.step(TickInputs(), 0);
  }
  EXPECT_FALSE(match.keptDumpTicks().empty());
}
