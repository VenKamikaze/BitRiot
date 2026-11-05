// The lobby model (brief 08, src/net/LobbyModel): what the online lobby, the error pages and the in-game
// overlay say, as plain data. No window, no RmlUi, no sockets: these tests are in both test executables.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <unistd.h>

#include "LobbyModel.h"

using namespace netplay;

namespace
{
// Is `s` well-formed UTF-8 (no stray continuation, no overlong form, no surrogate, nothing past U+10FFFF)?
bool validUtf8(const std::string& s)
{
  for (size_t i = 0; i < s.size();)
  {
    const unsigned char c = (unsigned char) s[i];
    size_t n = 0;
    uint32_t cp = 0;
    if (c < 0x80) { n = 1; cp = c; }
    else if ((c & 0xE0) == 0xC0) { n = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; cp = c & 0x07; }
    else return false;
    if (i + n > s.size()) return false;
    for (size_t k = 1; k < n; k++)
    {
      if (((unsigned char) s[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | ((unsigned char) s[i + k] & 0x3F);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)) return false;
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += n;
  }
  return true;
}

LobbyEntry peer(uint8_t id, const std::string& name, uint8_t mask, int rtt, bool ready)
{
  LobbyEntry e;
  e.id = id;
  e.name = name;
  e.slotMask = mask;
  e.rttMs = rtt;
  e.ready = ready;
  return e;
}

// The host (2 players), Bob (1, ready, 40 ms), Carol (2, not ready, 90 ms), and a computer player: 6 slots.
LobbyView mixedView()
{
  LobbyView v;
  v.phase = PHASE_LOBBY;
  v.round = 1;
  v.settings.mapWidth = 17;
  v.settings.mapHeight = 19;
  v.settings.blockPercentage = 55;
  v.settings.npcSpawner = true;
  const uint8_t owners[] = {0, 0, 1, 2, 2, NO_PEER};
  for (int i = 0; i < 6; i++)
  {
    SlotInfo s;
    s.owner = owners[i];
    s.team = (uint8_t) (i % 3);
    v.slots.push_back(s);
  }
  v.peers.push_back(peer(0, "Alice", 0b000011, -1, true));
  v.peers.push_back(peer(1, "Bob", 0b000100, 40, true));
  v.peers.push_back(peer(2, "Carol", 0b011000, 90, false));
  return v;
}
} // namespace

// -- text ---------------------------------------------------------------------------------------

TEST(LobbyModelText, SanitizeKeepsGoodUtf8AndReplacesEverythingElse)
{
  EXPECT_EQ(sanitizeUtf8("plain"), "plain");
  EXPECT_EQ(sanitizeUtf8("Z\xc3\xb6" "e \xe2\x9c\x93 \xf0\x9f\x98\x80"), "Z\xc3\xb6" "e \xe2\x9c\x93 \xf0\x9f\x98\x80");
  EXPECT_EQ(sanitizeUtf8("a\x80" "b"), "a?b");                 // a stray continuation byte
  EXPECT_EQ(sanitizeUtf8("\xc3"), "?");                         // truncated at the end
  EXPECT_EQ(sanitizeUtf8("\xc3(x"), "?(x");                     // a lead byte followed by ASCII
  EXPECT_EQ(sanitizeUtf8("\xc0\xaf"), "??");                    // an overlong '/'
  EXPECT_EQ(sanitizeUtf8("\xe0\x80\xaf"), "???");               // an overlong three-byte form
  EXPECT_EQ(sanitizeUtf8("\xed\xa0\x80"), "???");               // a surrogate
  EXPECT_EQ(sanitizeUtf8("\xf4\x90\x80\x80"), "????");          // past U+10FFFF
  EXPECT_EQ(sanitizeUtf8("\xff\xfe"), "??");
  EXPECT_EQ(sanitizeUtf8("a\tb\n\x1b[31mc\x7f"), "a?b??[31mc?"); // control characters never reach the page
  EXPECT_EQ(sanitizeUtf8(""), "");
}

TEST(LobbyModelText, SanitizedTextIsAlwaysWellFormed)
{
  // Every two-byte and three-byte combination of the bytes that matter, and a few longer runs.
  const unsigned char interesting[] = {0x00, 0x1F, 0x41, 0x7F, 0x80, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF};
  for (unsigned char a : interesting)
    for (unsigned char b : interesting)
      for (unsigned char c : interesting)
      {
        const std::string raw = {(char) a, (char) b, (char) c};
        ASSERT_TRUE(validUtf8(sanitizeUtf8(raw))) << (int) a << " " << (int) b << " " << (int) c;
      }
}

TEST(LobbyModelText, ClipCutsBetweenCharactersAndMarksTheCut)
{
  EXPECT_EQ(clipUtf8("abcdef", 6), "abcdef");
  EXPECT_EQ(clipUtf8("abcdefg", 6), "abc...");
  EXPECT_EQ(utf8Length(clipUtf8("abcdefghijklmnopqrstuvwxyz", 10)), 10u);
  // Two-byte characters are never split: 8 characters in 16 bytes, cut to 6 characters (3 kept and the dots).
  const std::string greek = "\xce\xb1\xce\xb2\xce\xb3\xce\xb4\xce\xb5\xce\xb6\xce\xb7\xce\xb8";
  EXPECT_EQ(clipUtf8(greek, 6), "\xce\xb1\xce\xb2\xce\xb3...");
  EXPECT_TRUE(validUtf8(clipUtf8(greek, 5)));
  EXPECT_EQ(clipUtf8(greek, 8), greek);
  // Invalid bytes are replaced before they are counted.
  EXPECT_EQ(clipUtf8("\xff\xff\xff\xff\xff\xff\xff\xff", 5), "??...");
  EXPECT_EQ(clipUtf8("abcdef", 3), "...");
  EXPECT_EQ(clipUtf8("abcdef", 0), "");
}

TEST(LobbyModelText, ByteClipEndsBetweenCharacters)
{
  const std::string s = "ab\xe2\x9c\x93" "cd";   // 2 + 3 + 2 bytes
  EXPECT_EQ(clipUtf8Bytes(s, 7), s);
  EXPECT_EQ(clipUtf8Bytes(s, 6), "ab\xe2\x9c\x93" "c");
  EXPECT_EQ(clipUtf8Bytes(s, 4), "ab");     // would end inside the check mark
  EXPECT_EQ(clipUtf8Bytes(s, 1), "a");
  EXPECT_EQ(clipUtf8Bytes(s, 0), "");
}

TEST(LobbyModelText, PlayerNamesAreCleanedForTheWire)
{
  EXPECT_EQ(cleanPlayerName("  Venom  "), "Venom");
  EXPECT_EQ(cleanPlayerName("a\tb\nc"), "abc");
  const std::string name = cleanPlayerName("\xce\xb1\xce\xb2\xce\xb3\xce\xb4\xce\xb5\xce\xb6\xce\xb7\xce\xb8\xce\xb9\xce\xba\xce\xbb\xce\xbc\xce\xbd");
  EXPECT_LE(name.size(), MAX_NAME);
  EXPECT_TRUE(validUtf8(name));
  EXPECT_EQ(name.size(), 24u);
  EXPECT_EQ(cleanPlayerName(""), "");
}

// -- the lobby table ----------------------------------------------------------------------------------------

TEST(LobbyModelRows, MixedSlotsMapToHostRemoteAiAndOpenRows)
{
  LobbyView v = mixedView();
  v.slots.resize(5);   // five seats: the sixth is open
  v.slots[4].owner = NO_PEER;
  v.peers[2].slotMask = 0b001000;   // Carol has one seat
  LobbyContext ctx;
  ctx.myId = 0;
  ctx.isHost = true;
  const std::vector<LobbyRow> rows = buildLobbyRows(v, ctx);
  ASSERT_EQ(rows.size(), 6u);

  EXPECT_EQ(rows[0].kind, SlotKind::HOST);
  EXPECT_EQ(rows[0].name, "Alice 1/2");   // a machine with two players names which one
  EXPECT_TRUE(rows[0].mine);
  EXPECT_FALSE(rows[0].showReady);
  EXPECT_EQ(rows[0].pingMs, -1);
  EXPECT_FALSE(rows[0].kickable);

  EXPECT_EQ(rows[1].kind, SlotKind::HOST);
  EXPECT_EQ(rows[1].name, "Alice 2/2");

  EXPECT_EQ(rows[2].kind, SlotKind::REMOTE);
  EXPECT_EQ(rows[2].name, "Bob");
  EXPECT_FALSE(rows[2].mine);
  EXPECT_TRUE(rows[2].showReady);
  EXPECT_TRUE(rows[2].ready);
  EXPECT_EQ(rows[2].pingMs, 40);
  EXPECT_TRUE(rows[2].kickable);
  EXPECT_EQ(rows[2].kickId, 1);
  EXPECT_TRUE(rows[2].teamEditable);

  EXPECT_EQ(rows[3].kind, SlotKind::REMOTE);
  EXPECT_EQ(rows[3].name, "Carol");
  EXPECT_FALSE(rows[3].ready);
  EXPECT_EQ(rows[3].pingMs, 90);

  EXPECT_EQ(rows[4].kind, SlotKind::AI);
  EXPECT_EQ(rows[4].name, "Computer");
  EXPECT_FALSE(rows[4].showReady);
  EXPECT_EQ(rows[4].owner, NO_PEER);
  EXPECT_EQ(rows[4].team, v.slots[4].team);

  EXPECT_EQ(rows[5].kind, SlotKind::OPEN);
  EXPECT_EQ(rows[5].name, "open");
  EXPECT_EQ(rows[5].team, -1);
  EXPECT_FALSE(rows[5].teamEditable);
  EXPECT_EQ(rows[5].number, "6");
}

TEST(LobbyModelRows, ASecondSeatOfARemoteMachineHasNoKickButtonOfItsOwn)
{
  const LobbyView v = mixedView();
  LobbyContext ctx;
  ctx.isHost = true;
  const std::vector<LobbyRow> rows = buildLobbyRows(v, ctx);
  EXPECT_EQ(rows[3].name, "Carol 1/2");
  EXPECT_EQ(rows[4].name, "Carol 2/2");
  EXPECT_TRUE(rows[3].kickable);
  EXPECT_FALSE(rows[4].kickable);
  EXPECT_EQ(rows[5].kind, SlotKind::AI);
}

TEST(LobbyModelRows, APeerSeesTheSameTableButOwnsNoButtons)
{
  const LobbyView v = mixedView();
  LobbyContext ctx;
  ctx.myId = 2;
  ctx.isHost = false;
  const std::vector<LobbyRow> rows = buildLobbyRows(v, ctx);
  EXPECT_EQ(rows[0].kind, SlotKind::HOST);   // the host's machine, as a peer sees it
  EXPECT_FALSE(rows[0].mine);
  EXPECT_TRUE(rows[3].mine);
  EXPECT_TRUE(rows[4].mine);
  EXPECT_FALSE(rows[2].mine);
  for (const LobbyRow& r : rows)
  {
    EXPECT_FALSE(r.teamEditable) << r.slot;
    EXPECT_FALSE(r.kickable) << r.slot;
  }
  EXPECT_EQ(rows[2].pingMs, 40);
}

TEST(LobbyModelRows, TeamsAreOnlyEditableInTheLobbyPhase)
{
  const LobbyView v = mixedView();
  LobbyContext ctx;
  ctx.isHost = true;
  ctx.inLobby = false;
  for (const LobbyRow& r : buildLobbyRows(v, ctx))
  {
    EXPECT_FALSE(r.teamEditable) << r.slot;
    EXPECT_FALSE(r.kickable) << r.slot;
  }
}

TEST(LobbyModelRows, TwoToSixSeatsAlwaysGiveSixRowsAndTheRestAreOpen)
{
  for (int n = 2; n <= 6; n++)
  {
    LobbyView v;
    for (int i = 0; i < n; i++)
    {
      SlotInfo s;
      s.owner = i == 0 ? 0 : NO_PEER;
      s.team = (uint8_t) i;
      v.slots.push_back(s);
    }
    v.peers.push_back(peer(0, "H", 1, -1, true));
    LobbyContext ctx;
    ctx.isHost = true;
    const LobbyScreen sc = buildLobbyScreen(v, ctx);
    ASSERT_EQ(sc.rows.size(), 6u) << n;
    EXPECT_EQ(sc.shownRows, n);
    for (int i = 0; i < 6; i++)
    {
      EXPECT_EQ(sc.rows[i].kind == SlotKind::OPEN, i >= n) << n << " / " << i;
    }
  }
}

TEST(LobbyModelRows, LongAndOddNamesAreClippedAndStayWellFormed)
{
  LobbyView v = mixedView();
  v.peers[1].name = std::string(24, 'W');
  v.peers[2].name = "\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93";
  v.peers[0].name = "\xff\xfe\x80Bad\x1b[";
  LobbyContext ctx;
  ctx.isHost = true;
  ctx.nameChars = 14;
  for (const LobbyRow& r : buildLobbyRows(v, ctx))
  {
    EXPECT_LE(utf8Length(r.name), 14u) << r.slot << " " << r.name;
    EXPECT_TRUE(validUtf8(r.name)) << r.slot;
  }
  const std::vector<LobbyRow> rows = buildLobbyRows(v, ctx);
  EXPECT_EQ(rows[2].name, "WWWWWWWWWWW...");
  EXPECT_EQ(rows[0].name.find('\xff'), std::string::npos);
  // A suffix is counted in the budget: a long name with "1/2" still fits.
  EXPECT_LE(utf8Length(rows[0].name), 14u);
}

TEST(LobbyModelScreen, TheHostSeesWhoIsBlockingTheStart)
{
  const LobbyView v = mixedView();
  LobbyContext ctx;
  ctx.isHost = true;
  ctx.canStart = false;
  const LobbyScreen sc = buildLobbyScreen(v, ctx);
  EXPECT_FALSE(sc.canStart);
  EXPECT_EQ(sc.status, "Waiting for Carol to be ready.");
  EXPECT_EQ(sc.humanPeers, 2);
  EXPECT_EQ(sc.readyPeers, 1);
  EXPECT_EQ(sc.startLabel, "Start match");
  EXPECT_EQ(sc.title, "Hosting a network game");
  EXPECT_EQ(sc.settings, "Map 17 x 19, blocks 55%, enemies on, 6 players");

  LobbyView two = v;
  two.peers[1].ready = false;
  const LobbyScreen sc2 = buildLobbyScreen(two, ctx);
  EXPECT_EQ(sc2.status, "Waiting for Bob and Carol to be ready.");

  LobbyView three = v;
  three.peers.push_back(peer(3, "Dave", 0, -1, false));
  three.peers[1].ready = false;
  EXPECT_EQ(buildLobbyScreen(three, ctx).status, "Waiting for Bob, Carol and Dave to be ready.");
}

TEST(LobbyModelScreen, TheHostCanStartWhenTheSessionSaysSo)
{
  LobbyView v = mixedView();
  v.peers[2].ready = true;
  LobbyContext ctx;
  ctx.isHost = true;
  ctx.canStart = true;
  LobbyScreen sc = buildLobbyScreen(v, ctx);
  EXPECT_TRUE(sc.canStart);
  EXPECT_EQ(sc.status, "Everyone is ready.");

  // Alone: allowed, and the page says what that means.
  LobbyView alone;
  alone.slots = {SlotInfo(), SlotInfo()};
  alone.peers.push_back(peer(0, "Alice", 1, -1, true));
  sc = buildLobbyScreen(alone, ctx);
  EXPECT_TRUE(sc.canStart);
  EXPECT_NE(sc.status.find("computer players"), std::string::npos) << sc.status;

  // The session may refuse for another reason (peers still catching up).
  ctx.canStart = false;
  ctx.startBlocked = "waiting for the other players to finish the last round";
  sc = buildLobbyScreen(alone, ctx);
  EXPECT_FALSE(sc.canStart);
  EXPECT_EQ(sc.status, ctx.startBlocked);
}

TEST(LobbyModelScreen, APeerToggleReadyAndIsToldWhatIsHappening)
{
  const LobbyView v = mixedView();
  LobbyContext ctx;
  ctx.myId = 2;
  LobbyScreen sc = buildLobbyScreen(v, ctx);
  EXPECT_TRUE(sc.canToggleReady);
  EXPECT_EQ(sc.startLabel, "Ready");
  EXPECT_FALSE(sc.canStart);
  EXPECT_EQ(sc.title, "Network game");
  ctx.myReady = true;
  sc = buildLobbyScreen(v, ctx);
  EXPECT_EQ(sc.startLabel, "Not ready");
  EXPECT_NE(sc.status.find("You are ready"), std::string::npos);
  // A machine that joined between rounds has nothing to press.
  ctx.inLobby = false;
  sc = buildLobbyScreen(v, ctx);
  EXPECT_FALSE(sc.canToggleReady);
  EXPECT_EQ(sc.startLabel, "Waiting for the host");
}

// -- why a session ended -------------------------------------------------------------------------------------

namespace
{
std::string allText(const EndMessage& m)
{
  std::string s = m.title;
  for (const std::string& l : m.lines) s += "\n" + l;
  return s;
}
}

TEST(LobbyModelEnd, EveryRefusalSaysWhatDiffersAndWhoseIsNewerWhenKnowable)
{
  Identity mine;
  mine.protocol = 2;
  mine.buildId = 0x1111111111111111ull;
  mine.dataHash = 0x2222222222222222ull;
  RejectInfo rj;
  rj.hostProtocol = 3;
  rj.hostBuildId = 0xAAAAAAAAAAAAAAAAull;
  rj.hostDataHash = 0xBBBBBBBBBBBBBBBBull;

  rj.reason = REJECT_VERSION;
  EndMessage m = describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "h:1");
  EXPECT_EQ(m.title, "Different game version");
  EXPECT_NE(allText(m).find("protocol 3"), std::string::npos);
  EXPECT_NE(allText(m).find("newer than yours"), std::string::npos);
  EXPECT_NE(allText(m).find("0xaaaaaaaaaaaaaaaa"), std::string::npos);
  EXPECT_NE(allText(m).find("0x1111111111111111"), std::string::npos);
  rj.hostProtocol = 1;
  EXPECT_NE(allText(describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "")).find("older than yours"), std::string::npos);

  rj.reason = REJECT_BUILD;
  rj.hostProtocol = 2;
  m = describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "");
  EXPECT_EQ(m.title, "Different game build");
  EXPECT_NE(allText(m).find("0xaaaaaaaaaaaaaaaa"), std::string::npos);
  EXPECT_NE(allText(m).find("0x1111111111111111"), std::string::npos);
  EXPECT_NE(allText(m).find("cannot be told"), std::string::npos) << "same protocol: say that newer is unknowable";
  rj.hostProtocol = 5;
  EXPECT_NE(allText(describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "")).find("newer than yours"), std::string::npos);

  rj.reason = REJECT_DATA;
  m = describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "");
  EXPECT_EQ(m.title, "Different game data");
  EXPECT_NE(allText(m).find("0xbbbbbbbbbbbbbbbb"), std::string::npos);
  EXPECT_NE(allText(m).find("0x2222222222222222"), std::string::npos);

  rj.reason = REJECT_FULL;
  EXPECT_EQ(describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "").title, "The game is full");
  rj.reason = REJECT_IN_PROGRESS;
  m = describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "");
  EXPECT_EQ(m.title, "A match is already in progress");
  EXPECT_NE(allText(m).find("between rounds"), std::string::npos);
  rj.reason = REJECT_BAD_REQUEST;
  rj.text = "asked for 7 players";
  m = describeEnd(LockstepSession::END_REJECTED, rj, "x", mine, "");
  EXPECT_NE(allText(m).find("asked for 7 players"), std::string::npos);
}

TEST(LobbyModelEnd, HostNotAnsweringHostLeftAndTheOtherEndings)
{
  Identity mine;
  RejectInfo none;
  EndMessage m = describeEnd(LockstepSession::END_CONNECT_TIMEOUT, none, "", mine, "192.168.1.20:7777");
  EXPECT_EQ(m.title, "The host did not answer");
  EXPECT_NE(allText(m).find("192.168.1.20:7777"), std::string::npos);
  EXPECT_NE(allText(m).find("forwarded"), std::string::npos);

  EXPECT_EQ(describeEnd(LockstepSession::END_HOST_LOST, none, "", mine, "").title, "Lost the connection to the host");
  EXPECT_EQ(describeEnd(LockstepSession::END_HOST_CLOSED, none, "the host ended the session", mine, "").title, "The host left");
  m = describeEnd(LockstepSession::END_HOST_CLOSED, none, "the host stopped the match: the game state diverged", mine, "");
  EXPECT_EQ(m.title, "The host stopped the match");
  EXPECT_NE(allText(m).find("desync"), std::string::npos);
  EXPECT_EQ(describeEnd(LockstepSession::END_KICKED, none, "", mine, "").title, "You were dropped");
  EXPECT_EQ(describeEnd(LockstepSession::END_REMOVED, none, "", mine, "").title, "You were removed");
  m = describeEnd(LockstepSession::END_LEFT, none, "", mine, "");
  EXPECT_FALSE(m.isError);
  EXPECT_EQ(describeEnd(LockstepSession::END_DESYNC, none, "", mine, "").title, "Stopped after a desync");
  EXPECT_EQ(describeEnd(LockstepSession::END_NETWORK, none, "socket closed", mine, "").lines.at(0), "socket closed");
  EXPECT_EQ(describeEnd(LockstepSession::END_BAD_HOST, none, "bad delay", mine, "").lines.at(0), "bad delay");
  // A hostile host's reject text is shown, but cleaned.
  RejectInfo evil;
  evil.reason = REJECT_BAD_REQUEST;
  evil.text = "\x1b[2Jhi\xff";
  m = describeEnd(LockstepSession::END_REJECTED, evil, "", mine, "");
  EXPECT_TRUE(validUtf8(allText(m)));
  EXPECT_EQ(allText(m).find('\x1b'), std::string::npos);
  EXPECT_EQ(hex64(0x6767612d056ba8afull), "0x6767612d056ba8af");
}

// -- addresses and saved settings ---------------------------------------------------------------------------------

TEST(LobbyModelAddress, HostPortParsing)
{
  std::string host, error, norm;
  int port = 0;
  ASSERT_TRUE(parseHostPort("192.168.1.20:7777", 7777, &host, &port, &error, &norm)) << error;
  EXPECT_EQ(host, "192.168.1.20");
  EXPECT_EQ(port, 7777);
  EXPECT_EQ(norm, "192.168.1.20:7777");
  ASSERT_TRUE(parseHostPort("  example.org  ", 9000, &host, &port, &error, &norm)) << error;
  EXPECT_EQ(port, 9000);
  EXPECT_EQ(norm, "example.org:9000");
  ASSERT_TRUE(parseHostPort("my-host_1.lan:65535", 7777, &host, &port, &error, &norm));
  EXPECT_EQ(port, 65535);
  ASSERT_TRUE(parseHostPort("[::1]:7778", 7777, &host, &port, &error, &norm)) << error;
  EXPECT_EQ(host, "::1");
  EXPECT_EQ(norm, "[::1]:7778");
  ASSERT_TRUE(parseHostPort("[fe80::1%eth0]", 7777, &host, &port, &error, &norm)) << error;
  EXPECT_EQ(norm, "[fe80::1%eth0]:7777");
  ASSERT_TRUE(parseHostPort("localhost", 7777, &host, &port, &error, &norm));
  EXPECT_EQ(norm, "localhost:7777");

  const char* const bad[] = {"", "   ", ":7777", "host:", "host:0", "host:65536", "host:abc", "host:-1", "ho st:7777", "host:77 77",
                             "a:b:c", "::1", "[::1", "[::1]x", "host/path:7777", "http://host:7777", "h\xc3\xb6st:7777", "[]:1"};
  for (const char* b : bad)
  {
    EXPECT_FALSE(parseHostPort(b, 7777, &host, &port, &error)) << "'" << b << "'";
    EXPECT_FALSE(error.empty()) << b;
  }
}

TEST(LobbyModelAddress, PortParsing)
{
  int port = 0;
  std::string error;
  EXPECT_TRUE(parsePort("7777", &port, &error));
  EXPECT_EQ(port, 7777);
  EXPECT_TRUE(parsePort(" 1 ", &port, &error));
  EXPECT_EQ(port, 1);
  for (const char* b : {"", "0", "65536", "99999", "123456", "12a", "-5", "7 7", "٧٧"})
  {
    EXPECT_FALSE(parsePort(b, &port, &error)) << b;
  }
}

namespace
{
std::string scratch(const std::string& leaf)
{
  const char* dir = std::getenv("TMPDIR");
  return std::string(dir ? dir : "/tmp") + "/bitriot-lobbymodel-" + std::to_string((long) getpid()) + "-" + leaf;
}
}

TEST(LobbyModelPrefs, RoundTripThroughAFile)
{
  NetPrefs p;
  p.name = "Venom \xc3\xb6";
  p.joinAddress = "example.org:7001";
  p.hostPort = 7123;
  p.localPlayers = 3;
  const std::string path = scratch("prefs.txt");
  ASSERT_TRUE(saveNetPrefs(path, p));
  NetPrefs back;
  ASSERT_TRUE(loadNetPrefs(path, back));
  EXPECT_EQ(back, p);
  std::remove(path.c_str());
  std::ifstream tmp((path + ".tmp").c_str());
  EXPECT_FALSE(tmp.good()) << "the temporary file is renamed away";
}

TEST(LobbyModelPrefs, AMissingFileIsNotAnError)
{
  NetPrefs p;
  p.name = "kept";
  EXPECT_FALSE(loadNetPrefs(scratch("does-not-exist"), p));
  EXPECT_EQ(p.name, "kept");
  EXPECT_EQ(p.hostPort, 7777);
}

TEST(LobbyModelPrefs, ACorruptFileIsReadAsFarAsItMakesSense)
{
  NetPrefs p;
  EXPECT_TRUE(parseNetPrefs("junk without equals\nname=Zed\n=novalue\nport=99999\nplayers=9\njoin=a b c\nunknown=1\nport=7002\n", p));
  EXPECT_EQ(p.name, "Zed");
  EXPECT_EQ(p.hostPort, 7002);        // the bad value was skipped, the later good one taken
  EXPECT_EQ(p.localPlayers, 1);       // 9 is not a player count
  EXPECT_EQ(p.joinAddress, "");       // a bad address is dropped
  NetPrefs q;
  EXPECT_TRUE(parseNetPrefs(std::string("name=\xff\xfe\x01" "x\n\0\0\0\n", 12), q));
  EXPECT_EQ(q.name, "??x");           // cleaned, never raw bytes
  NetPrefs r;
  EXPECT_TRUE(parseNetPrefs(std::string(100000, 'x'), r));
  EXPECT_EQ(r, NetPrefs());
  // A file that cannot be written reports it.
  EXPECT_FALSE(saveNetPrefs("/nonexistent-dir/prefs", NetPrefs()));
}

// -- during a round ---------------------------------------------------------------------------------------------

TEST(LobbyModelOverlay, ALeavingPlayerIsAnnouncedAndNothingElse)
{
  EXPECT_EQ(eventToast(ControlEvent(2, ControlEvent::RETURN_TO_AI), "Bob"), "Bob left - now AI");
  EXPECT_EQ(eventToast(ControlEvent(2, ControlEvent::LEAVE), "Bob"), "Bob left - now AI");
  EXPECT_EQ(eventToast(ControlEvent(2, ControlEvent::RETURN_TO_AI), ""), "A player left - now AI");
  EXPECT_EQ(eventToast(ControlEvent(2, ControlEvent::HUMAN_TAKES_OVER), "Bob"), "");
  EXPECT_LE(utf8Length(eventToast(ControlEvent(0, ControlEvent::LEAVE), std::string(60, 'x'))), 20u + 14u);
}

TEST(LobbyModelOverlay, TheStallTextWaitsForHalfASecondOfStalling)
{
  StallTimer t(500);
  EXPECT_EQ(t.update("", 1000), "");
  EXPECT_EQ(t.update("waiting for Bob", 1000), "");     // just started
  EXPECT_EQ(t.update("waiting for Bob", 1400), "");
  EXPECT_EQ(t.update("waiting for Bob", 1500), "waiting for Bob");
  EXPECT_EQ(t.update("waiting for Bob, Carol", 1600), "waiting for Bob, Carol");   // the names change: still one stall
  EXPECT_EQ(t.update("", 1700), "");                      // released
  EXPECT_EQ(t.update("waiting for Bob", 1800), "");       // a new stall starts its half second again
  EXPECT_EQ(t.update("waiting for Bob", 2299), "");
  EXPECT_EQ(t.update("waiting for Bob", 2300), "waiting for Bob");
  // A clock that wraps does not show it early or never.
  StallTimer w(500);
  w.update("x", 0xFFFFFF00u);
  EXPECT_EQ(w.update("x", 0xFFFFFF00u + 100), "");
  EXPECT_EQ(w.update("x", 0xFFFFFF00u + 500), "x");
}
