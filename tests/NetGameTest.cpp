// NetGame (brief 08): the game's side of an online session, without a window. A host and a peer
// in one process on real UDP over 127.0.0.1 (ephemeral ports) walk the lobby through the same calls the
// menus make: ready, the host's settings and teams, removing a peer, and the player's own settings coming
// back when a session ends. Also the host-name lookup that must not block a frame.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "GameSettings.h"
#include "NetGame.h"
#include "Simulation.h"

using netplay::LockstepSession;

namespace
{
typedef std::chrono::steady_clock Clock;

// Pumps every game until `done()` or `limitMs` of real time has passed.
bool pumpUntil(const std::vector<NetGame*>& games, const std::function<bool()>& done, int limitMs = 5000)
{
  const auto end = Clock::now() + std::chrono::milliseconds(limitMs);
  while (Clock::now() < end)
  {
    for (NetGame* g : games) g->pump();
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return done();
}

class NetGameTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
      GameSettingsHolder::save();
      // The singleton carries whatever an earlier test left: start from the game's own defaults.
      std::GameSettings* gs = std::GameSettings::getInstance();
      gs->setMapWidth(17);
      gs->setMapHeight(13);
      gs->setBlockSpawnPercentage(55);
      gs->setNpcSpawnerEnabled(true);
    }
    void TearDown() override
    {
      m_peer.reset();
      m_host.reset();
      GameSettingsHolder::restore();
    }

    // Keeps the singleton's settings from leaking between tests.
    struct GameSettingsHolder
    {
      static std::GameSettings::Snapshot& saved() { static std::GameSettings::Snapshot s; return s; }
      static void save() { saved() = std::GameSettings::getInstance()->snapshot(); }
      static void restore() { std::GameSettings::getInstance()->restore(saved()); }
    };

    NetGame* makeHost(int totalSlots = 0, int localPlayers = 1)
    {
      NetGame::Params p;
      p.host = true;
      p.port = 0;
      p.name = "Alice";
      p.localPlayers = localPlayers;
      p.totalSlots = totalSlots;
      p.requireReady = true;
      std::string error;
      m_host = NetGame::create(p, m_sim, &error);
      EXPECT_TRUE(m_host) << error;
      return m_host.get();
    }

    NetGame* makePeer(const std::string& name = "Bob", int localPlayers = 1)
    {
      NetGame::Params p;
      p.host = false;
      p.join = "127.0.0.1:" + std::to_string(m_host->session().localPort());
      p.name = name;
      p.localPlayers = localPlayers;
      p.requireReady = true;
      vnet_addr addr;
      EXPECT_EQ(vnet_resolve_host(p.join.c_str(), VNET_FAMILY_ANY, &addr), 0);
      std::string error;
      m_peer = NetGame::createResolved(p, m_sim, addr, &error);
      EXPECT_TRUE(m_peer) << error;
      return m_peer.get();
    }

    bool connected()
    {
      return pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_peer->session().state() == LockstepSession::LOBBY && m_host->session().peerCount() == 1; });
    }

    Simulation m_sim;
    std::unique_ptr<NetGame> m_host;
    std::unique_ptr<NetGame> m_peer;
};
} // namespace

// -- the lookup --------------------------------------------------------------------------------------------

TEST(NetResolve, ALookupThatBlocksDoesNotBlockTheCaller)
{
  std::mutex mutex;
  std::condition_variable cv;
  bool release = false;
  auto resolver = [&](const std::string&, vnet_addr* out) -> int {
    std::unique_lock<std::mutex> lock(mutex);
    // A dead name server: waits until the test lets go (or gives up after 3 s, so a broken NetResolve fails instead of hanging).
    cv.wait_for(lock, std::chrono::seconds(3), [&] { return release; });
    std::memset(out, 0, sizeof(*out));
    return 0;
  };
  const auto start = Clock::now();
  std::unique_ptr<NetResolve> r(new NetResolve("example.invalid:7777", resolver));
  const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
  EXPECT_LT(took, 1000) << "constructing a NetResolve waited for the lookup";
  EXPECT_FALSE(r->done());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(r->done()) << "still looking";
  {
    std::lock_guard<std::mutex> lock(mutex);
    release = true;
  }
  cv.notify_all();
  const auto end = Clock::now() + std::chrono::seconds(2);
  while (!r->done() && Clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  EXPECT_TRUE(r->done());
  EXPECT_EQ(r->result(), 0);
  EXPECT_EQ(r->hostPort(), "example.invalid:7777");
}

TEST(NetResolve, AFailureIsReportedAndAnAbandonedLookupIsHarmless)
{
  NetResolve failing("nowhere:1", [](const std::string&, vnet_addr*) { return VNET_EIO; });
  const auto end = Clock::now() + std::chrono::seconds(2);
  while (!failing.done() && Clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT_TRUE(failing.done());
  EXPECT_EQ(failing.result(), VNET_EIO);

  // Dropped while the lookup still runs: the worker finishes unseen (nothing here may crash or touch freed memory).
  std::atomic<bool> finished(false);
  {
    NetResolve slow("slow:1", [&](const std::string&, vnet_addr*) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); finished = true; return 0; });
  }
  const auto end2 = Clock::now() + std::chrono::seconds(2);
  while (!finished && Clock::now() < end2) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT_TRUE(finished);
}

TEST(NetResolve, TheRealResolverFindsALiteralAddress)
{
  NetResolve r("127.0.0.1:7777");
  const auto end = Clock::now() + std::chrono::seconds(5);
  while (!r.done() && Clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  ASSERT_TRUE(r.done());
  EXPECT_EQ(r.result(), 0);
  char text[64];
  vnet_addr_to_string(&r.address(), text, sizeof(text));
  EXPECT_EQ(std::string(text), "127.0.0.1:7777");
}

// -- the lobby ---------------------------------------------------------------------------------------------

TEST_F(NetGameTest, ReadyGatesTheHostsStartAndTheScreenSaysWho)
{
  makeHost(4);
  makePeer("Bob");
  ASSERT_TRUE(connected());

  netplay::LobbyScreen sc = m_host->lobbyScreen();
  ASSERT_EQ(sc.rows.size(), 6u);
  EXPECT_EQ(sc.rows[0].kind, netplay::SlotKind::HOST);
  EXPECT_EQ(sc.rows[1].kind, netplay::SlotKind::REMOTE);
  EXPECT_EQ(sc.rows[1].name, "Bob");
  EXPECT_EQ(sc.rows[2].kind, netplay::SlotKind::AI);
  EXPECT_EQ(sc.rows[4].kind, netplay::SlotKind::OPEN);
  EXPECT_FALSE(sc.canStart);
  EXPECT_EQ(sc.status, "Waiting for Bob to be ready.");
  std::string error;
  EXPECT_FALSE(m_host->startRound(&error));
  EXPECT_NE(error.find("ready"), std::string::npos) << error;

  // The peer's screen: its own row is marked, it can press Ready.
  sc = m_peer->lobbyScreen();
  EXPECT_TRUE(sc.canToggleReady);
  EXPECT_TRUE(sc.rows[1].mine);
  EXPECT_EQ(sc.startLabel, "Ready");

  m_peer->session().setReady(true);
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_host->lobbyScreen().canStart; }));
  EXPECT_EQ(m_host->lobbyScreen().status, "Everyone is ready.");
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_peer->lobbyScreen().rows[1].ready; }));
  EXPECT_EQ(m_peer->lobbyScreen().startLabel, "Not ready");
}

TEST_F(NetGameTest, TheHostsSettingsAndTeamsReachThePeer)
{
  makeHost(0);
  makePeer();
  ASSERT_TRUE(connected());
  const int width = m_host->session().lobby().settings.mapWidth;

  m_host->cycleSetting(NetGame::SET_MAP_WIDTH);
  m_host->cycleSetting(NetGame::SET_BLOCKS);
  m_host->cycleSetting(NetGame::SET_SPAWNER);
  m_host->cycleSetting(NetGame::SET_SLOTS);   // auto -> 2
  m_host->cycleSetting(NetGame::SET_SLOTS);   // -> 3
  m_host->cycleTeam(1);
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_peer->session().lobby().slots.size() == 3 && m_peer->session().lobby().settings.mapWidth != width; }));
  const netplay::LobbyView& v = m_peer->session().lobby();
  EXPECT_EQ(v.settings.mapWidth, width + 2);
  EXPECT_EQ(v.settings.blockPercentage, m_host->session().lobby().settings.blockPercentage);
  EXPECT_FALSE(v.settings.npcSpawner) << "the host's menu said on; one press of the button turned it off";
  ASSERT_EQ(v.slots.size(), 3u);
  EXPECT_EQ(v.slots[1].team, 2);   // team 1 -> the next: 2 (of 0..2)
  EXPECT_EQ(v.slots[0].team, 0);
  EXPECT_EQ(m_host->lobbyScreen().settingButtons[4], "Players 3");

  // Fewer slots: a team that no longer exists falls back to the slot's own number, so the round can still start.
  m_host->cycleSetting(NetGame::SET_SLOTS);   // 4
  m_host->cycleTeam(3);                        // slot 3 -> team 0
  m_host->cycleTeam(2);                        // slot 2 -> team 3
  m_host->cycleSetting(NetGame::SET_SLOTS);   // 5
  m_host->cycleSetting(NetGame::SET_SLOTS);   // 6
  m_host->cycleSetting(NetGame::SET_SLOTS);   // auto (0): back to the two humans
  m_host->cycleSetting(NetGame::SET_SLOTS);   // 2
  for (const netplay::SlotInfo& s : m_host->session().lobby().slots)
  {
    EXPECT_LT((int) s.team, (int) m_host->session().lobby().slots.size()) << "a team beyond the slots";
  }
  m_peer->session().setReady(true);
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_host->session().canStartRound(); }));
}

TEST_F(NetGameTest, TheTeamButtonWrapsAtTheNumberOfSeatsNotAtSix)
{
  makeHost(2);
  ASSERT_EQ(m_host->session().lobby().slots.size(), 2u);
  EXPECT_EQ(m_host->session().lobby().slots[1].team, 1);
  m_host->cycleTeam(1);
  EXPECT_EQ(m_host->session().lobby().slots[1].team, 0) << "team 1 of 2 seats goes to team 0";
  m_host->cycleTeam(1);
  EXPECT_EQ(m_host->session().lobby().slots[1].team, 1) << "and round again: it never lands on a team this match does not have";
  m_host->cycleTeam(0);
  EXPECT_EQ(m_host->session().lobby().slots[0].team, 1);
}

TEST_F(NetGameTest, FiveOrSixSeatsRaiseTheMapForTheirPortraits)
{
  makeHost(0);
  for (int i = 0; i < 5; i++) m_host->cycleSetting(NetGame::SET_SLOTS);   // 2,3,4,5,6
  EXPECT_EQ(m_host->session().lobby().slots.size(), 6u);
  const int needed = std::GameSettings::getInstance()->requiredMapHeightFor(6);
  EXPECT_GE(m_host->session().lobby().settings.mapHeight, needed);
  // Stepping the height starts from what the lobby shows, not from the lower number that was raised.
  m_host->cycleSetting(NetGame::SET_MAP_HEIGHT);
  EXPECT_GE(m_host->session().lobby().settings.mapHeight, needed);
}

TEST_F(NetGameTest, TheSettingButtonsWrapLikeTheGameOptionsScreen)
{
  typedef std::GameSettings GS;
  const int minWidth = GS::MIN_MAP_WIDTH, maxWidth = GS::MAX_MAP_WIDTH, minBlocks = GS::MIN_BLOCK_PCT, maxBlocks = GS::MAX_BLOCK_PCT;
  makeHost(0);
  const int widthSteps = (maxWidth - minWidth) / GS::MAP_SIZE_STEP + 1;
  for (int i = 0; i < widthSteps; i++) m_host->cycleSetting(NetGame::SET_MAP_WIDTH);
  const int startWidth = m_host->session().lobby().settings.mapWidth;
  for (int i = 0; i < widthSteps; i++) m_host->cycleSetting(NetGame::SET_MAP_WIDTH);
  EXPECT_EQ(m_host->session().lobby().settings.mapWidth, startWidth) << "a full cycle comes back";
  int seen = m_host->session().lobby().settings.mapWidth;
  bool wrapped = false;
  for (int i = 0; i < widthSteps; i++)
  {
    m_host->cycleSetting(NetGame::SET_MAP_WIDTH);
    const int now = m_host->session().lobby().settings.mapWidth;
    EXPECT_GE(now, minWidth);
    EXPECT_LE(now, maxWidth);
    if (now < seen) wrapped = true;
    seen = now;
  }
  EXPECT_TRUE(wrapped || startWidth == minWidth);
  for (int i = 0; i < 20; i++)
  {
    m_host->cycleSetting(NetGame::SET_BLOCKS);
    const int b = m_host->session().lobby().settings.blockPercentage;
    EXPECT_GE(b, minBlocks);
    EXPECT_LE(b, maxBlocks);
  }
}

TEST_F(NetGameTest, RemovingAPeerEndsItsSessionWithTheRightWordsAndGivesItsSettingsBack)
{
  std::GameSettings* gs = std::GameSettings::getInstance();
  gs->setBlockSpawnPercentage(35);
  const int mapWidth = gs->getMapWidth();
  makeHost(4);
  makePeer("Bob");
  ASSERT_TRUE(connected());
  // A match overwrites the player's own settings (Simulation::start applies the host's MatchConfig).
  gs->setBlockSpawnPercentage(70);
  gs->setMapWidth(mapWidth + 4);

  ASSERT_TRUE(m_host->kick(m_peer->session().myId()));
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_peer->session().ended(); }));
  EXPECT_EQ(m_peer->session().endReason(), LockstepSession::END_REMOVED);
  const netplay::EndMessage m = m_peer->endMessage();
  EXPECT_EQ(m.title, "You were removed");
  // The peer's pump gave the player's own settings back the moment the session ended: nothing else called restore.
  EXPECT_EQ(gs->getBlockSpawnPercentage(), 35);
  EXPECT_EQ(gs->getMapWidth(), mapWidth);
}

TEST_F(NetGameTest, WhenTheHostLeavesThePeerEndsWithItsWordsAndItsSettingsAreBack)
{
  std::GameSettings* gs = std::GameSettings::getInstance();
  gs->setBlockSpawnPercentage(40);
  makeHost(4);
  makePeer();
  ASSERT_TRUE(connected());
  gs->setBlockSpawnPercentage(65);   // what a match did to them
  m_host->session().leave();
  ASSERT_TRUE(pumpUntil({m_host.get(), m_peer.get()}, [&] { return m_peer->session().ended(); }));
  EXPECT_EQ(m_peer->endMessage().title, "The host left");
  EXPECT_EQ(gs->getBlockSpawnPercentage(), 40);
}

TEST_F(NetGameTest, AJoinThatNobodyAnswersStaysConnectingAndLeavingEndsItWithoutAnError)
{
  makeHost(4);
  const uint16_t port = m_host->session().localPort();
  m_host.reset();   // nobody listens on that port now
  NetGame::Params p;
  p.host = false;
  p.join = "127.0.0.1:" + std::to_string(port);
  p.name = "Bob";
  vnet_addr addr;
  ASSERT_EQ(vnet_resolve_host(p.join.c_str(), VNET_FAMILY_ANY, &addr), 0);
  std::string error;
  m_peer = NetGame::createResolved(p, m_sim, addr, &error);
  ASSERT_TRUE(m_peer) << error;
  pumpUntil({m_peer.get()}, [] { return false; }, 300);
  EXPECT_EQ(m_peer->session().state(), LockstepSession::CONNECTING) << "ten seconds have not passed: still asking";
  // Cancel: the session ends the way the page's Cancel button ends it.
  m_peer->session().leave();
  const netplay::EndMessage m = m_peer->endMessage();
  EXPECT_FALSE(m.isError);
}

TEST_F(NetGameTest, AHostWithTwoLocalPlayersAndAPeerWithTwoFillFourSeats)
{
  makeHost(0, 2);
  makePeer("Bob", 2);
  ASSERT_TRUE(connected());
  const netplay::LobbyScreen sc = m_host->lobbyScreen();
  EXPECT_EQ(sc.rows[0].name, "Alice 1/2 (you)");
  EXPECT_EQ(sc.rows[1].name, "Alice 2/2 (you)");
  EXPECT_EQ(sc.rows[2].name, "Bob 1/2");
  EXPECT_EQ(sc.rows[3].name, "Bob 2/2");
  EXPECT_TRUE(sc.rows[2].kickable);
  EXPECT_FALSE(sc.rows[3].kickable);
  EXPECT_EQ(sc.rows[4].kind, netplay::SlotKind::OPEN);
}

// Brief 12: a joiner over a relay that is slow to answer. The session was made while the relay context was still CONNECTING; the host's
// time to answer (connectTimeoutMs) must start when the room is entered, not when the join was asked for, or a slow relay (a WebTransport
// attempt that falls back to the WebSocket takes seconds) would be reported as "the host did not answer".
TEST(NetGameRelay, TheHostsTimeToAnswerStartsWhenTheRelayIsReadyNotWhenTheJoinWasAsked)
{
  // A UDP socket that never answers: the relay context stays CONNECTING until its own timeout (1.5 s), then FAILED.
  const int hole = socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(hole, 0);
  sockaddr_in a;
  std::memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = 0;
  ASSERT_EQ(bind(hole, (sockaddr*) &a, sizeof(a)), 0);
  socklen_t len = sizeof(a);
  ASSERT_EQ(getsockname(hole, (sockaddr*) &a, &len), 0);
  const std::string relayAddr = "127.0.0.1:" + std::to_string(ntohs(a.sin_port));

  vnet_config cfg;
  vnet_config_init(&cfg);
  cfg.backend = VNET_BACKEND_RELAY_AUTO;
  cfg.relay_udp = relayAddr.c_str();
  cfg.room = "ABCDEF";
  cfg.relay_timeout_ms = 1500;
  cfg.ignore_env = 1;
  int err = 0;
  vnet* v = vnet_create(&cfg, &err);
  ASSERT_NE(v, nullptr) << vnet_strerror(err);
  vnet_addr host;
  ASSERT_EQ(vnet_resolve(v, "room-host:7777", &host), 0);
  netplay::SessionOptions so;
  so.connectTimeoutMs = 300;   // far shorter than the relay's 1.5 s
  std::string error;
  std::unique_ptr<LockstepSession> s = LockstepSession::join(v, host, so, &error);
  ASSERT_NE(s, nullptr) << error;

  ASSERT_TRUE(pumpUntil({}, [&] { s->pump(); return s->ended(); }, 4000));
  EXPECT_EQ(s->endReason(), LockstepSession::END_NETWORK) << "the relay failed first; the host was never given a chance to be slow: " << s->endText();
  EXPECT_NE(s->endText().find("timed out"), std::string::npos) << s->endText();
  s.reset();
  vnet_destroy(v);
  close(hole);
}

// Brief 12 review: the game's own sessions raise the D limit on a relay and tolerate sparse pumps (commenting either out used to pass).
TEST(NetGameRelay, ARelayGameMayChooseADOfUpToTwelveAndADirectOneEight)
{
  NetGame::Params direct;
  direct.host = true;
  EXPECT_EQ(NetGame::sessionOptionsFor(direct).maxDelay, 8);
  NetGame::Params relay;
  relay.host = true;
  relay.relay = true;
  EXPECT_EQ(NetGame::sessionOptionsFor(relay).maxDelay, 12);
  relay.host = false;
  EXPECT_EQ(NetGame::sessionOptionsFor(relay).maxDelay, 12);
  EXPECT_TRUE(NetGame::sessionOptionsFor(direct).tolerateSparsePump);
  EXPECT_TRUE(NetGame::sessionOptionsFor(relay).tolerateSparsePump);
  relay.name = "Bob";
  EXPECT_EQ(NetGame::sessionOptionsFor(relay).name, "Bob");
}
