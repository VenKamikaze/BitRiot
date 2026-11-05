// NetProtocol (brief 07): every message encodes and decodes back to itself, a datagram
// that is cut anywhere or carries garbage is refused without reading past its end, and
// the wire layout in doco/netplay/protocol.md is the one the code writes (pinned bytes).

#include <gtest/gtest.h>

#include <functional>
#include <random>

#include "NetProtocol.h"

using namespace netplay;

namespace
{
std::vector<uint8_t> packet(uint8_t type, uint8_t sender, uint32_t session, const std::function<void(Writer&)>& body)
{
  Writer w;
  Header h;
  h.type = type;
  h.sender = sender;
  h.session = session;
  writeHeader(w, h);
  body(w);
  return w.bytes();
}

// Decodes the body of a datagram that starts with a header.
template <typename Msg>
bool decodeBody(const std::vector<uint8_t>& bytes, Msg& out)
{
  Reader r(bytes.data(), bytes.size());
  Header h;
  if (!readHeader(r, h)) return false;
  return decode(r, out);
}

InputRecord record(uint8_t mask, std::initializer_list<uint8_t> bytes, std::vector<ControlEvent> events = {})
{
  InputRecord r;
  r.mask = mask;
  int k = 0;
  auto it = bytes.begin();
  for (int s = 0; s < MAX_SLOTS && it != bytes.end(); s++)
  {
    if (mask & (1 << s)) { r.bytes[s] = *it++; k++; }
  }
  r.events = events;
  return r;
}
} // namespace

TEST(NetProtocol, HeaderLayoutIsPinned)
{
  Writer w;
  Header h;
  h.type = MSG_TICKS;
  h.sender = 3;
  h.session = 0x11223344;
  writeHeader(w, h);
  ASSERT_EQ(w.bytes().size(), HEADER_SIZE);
  const uint8_t expected[] = {'B', 'N', 7, 3, 0x44, 0x33, 0x22, 0x11};
  EXPECT_EQ(std::vector<uint8_t>(expected, expected + 8), w.bytes());
}

TEST(NetProtocol, HeaderWithWrongMagicOrTooShortIsRefused)
{
  std::vector<uint8_t> good = packet(MSG_PING, 0, 1, [](Writer&) {});
  Reader ok(good.data(), good.size());
  Header h;
  EXPECT_TRUE(readHeader(ok, h));
  EXPECT_EQ(h.type, MSG_PING);

  std::vector<uint8_t> bad = good;
  bad[0] = 'X';
  Reader r1(bad.data(), bad.size());
  EXPECT_FALSE(readHeader(r1, h));
  for (size_t n = 0; n < HEADER_SIZE; n++)
  {
    Reader r(good.data(), n);
    EXPECT_FALSE(readHeader(r, h)) << "length " << n;
  }
}

TEST(NetProtocol, ReaderNeverReadsPastTheEnd)
{
  const uint8_t data[3] = {1, 2, 3};
  Reader r(data, 3);
  EXPECT_EQ(r.u16(), 0x0201);
  EXPECT_TRUE(r.ok());
  EXPECT_EQ(r.u32(), 0u);   // only one byte left
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.u8(), 0);     // and it stays failed
  EXPECT_FALSE(r.ok());
}

TEST(NetProtocol, ScalarsAreLittleEndian)
{
  Writer w;
  w.u16(0x1234);
  w.u32(0xA1B2C3D4);
  w.u64(0x0102030405060708ull);
  const uint8_t expected[] = {0x34, 0x12, 0xD4, 0xC3, 0xB2, 0xA1, 8, 7, 6, 5, 4, 3, 2, 1};
  EXPECT_EQ(std::vector<uint8_t>(expected, expected + sizeof(expected)), w.bytes());
}

TEST(NetProtocol, StringsAreBoundedAndControlCharactersReplaced)
{
  Writer w;
  w.str(std::string(40, 'x'));    // cut to MAX_NAME
  w.str("ok\x1b[31m");
  Reader r(w.bytes().data(), w.bytes().size());
  EXPECT_EQ(r.str(), std::string(MAX_NAME, 'x'));
  EXPECT_EQ(r.str(), "ok?[31m");
  EXPECT_TRUE(r.done());

  // A length that runs past the end, and one over the bound.
  const uint8_t over[] = {30, 'a', 'b'};
  Reader r2(over, 3);
  r2.str();
  EXPECT_FALSE(r2.ok());
  Writer w3;
  w3.u8(100);
  for (int i = 0; i < 100; i++) w3.u8('a');
  Reader r3(w3.bytes().data(), w3.bytes().size());
  r3.str();
  EXPECT_FALSE(r3.ok());
}

TEST(NetProtocol, HelloRoundTripsAndTheVersionComesFirst)
{
  HelloMsg m;
  m.protocol = 1;
  m.buildId = 0x6767612d056ba8afull;
  m.dataHash = 0x0ca2017527be6327ull;
  m.localPlayers = 2;
  m.nonce = 0xDEADBEEF;
  m.token = 77;
  m.name = "Venom";
  auto bytes = packet(MSG_HELLO, NO_PEER, 0, [&](Writer& w) { encode(w, m); });
  HelloMsg back;
  ASSERT_TRUE(decodeBody(bytes, back));
  EXPECT_EQ(back.protocol, m.protocol);
  EXPECT_EQ(back.buildId, m.buildId);
  EXPECT_EQ(back.dataHash, m.dataHash);
  EXPECT_EQ(back.localPlayers, 2);
  EXPECT_EQ(back.nonce, m.nonce);
  EXPECT_EQ(back.token, 77u);
  EXPECT_EQ(back.name, "Venom");

  // Any HELLO, whatever the rest looks like, gives its version.
  uint16_t version = 0;
  EXPECT_TRUE(decodeHelloVersion(bytes.data() + HEADER_SIZE, bytes.size() - HEADER_SIZE, version));
  EXPECT_EQ(version, 1);
  const uint8_t future[] = {9, 0, 1, 2, 3};
  EXPECT_TRUE(decodeHelloVersion(future, sizeof(future), version));
  EXPECT_EQ(version, 9);
  EXPECT_FALSE(decodeHelloVersion(future, 1, version));
}

TEST(NetProtocol, WelcomeAndRejectRoundTrip)
{
  WelcomeMsg w;
  w.nonce = 5;
  w.peerId = 2;
  w.token = 99;
  w.slotMask = 0x0C;
  auto wb = packet(MSG_WELCOME, 0, 42, [&](Writer& o) { encode(o, w); });
  WelcomeMsg wback;
  ASSERT_TRUE(decodeBody(wb, wback));
  EXPECT_EQ(wback.peerId, 2);
  EXPECT_EQ(wback.slotMask, 0x0C);
  EXPECT_EQ(wback.token, 99u);
  // A WELCOME that names the host's id, or an id past the table, is nonsense.
  w.peerId = 0;
  wb = packet(MSG_WELCOME, 0, 42, [&](Writer& o) { encode(o, w); });
  EXPECT_FALSE(decodeBody(wb, wback));
  w.peerId = MAX_PEERS;
  wb = packet(MSG_WELCOME, 0, 42, [&](Writer& o) { encode(o, w); });
  EXPECT_FALSE(decodeBody(wb, wback));

  RejectMsg rj;
  rj.nonce = 6;
  rj.reason = REJECT_BUILD;
  rj.hostBuildId = 1;
  rj.hostDataHash = 2;
  rj.text = "different game build";
  auto rb = packet(MSG_REJECT, 0, 42, [&](Writer& o) { encode(o, rj); });
  RejectMsg rback;
  ASSERT_TRUE(decodeBody(rb, rback));
  EXPECT_EQ(rback.reason, REJECT_BUILD);
  EXPECT_EQ(rback.hostBuildId, 1u);
  EXPECT_EQ(rback.text, "different game build");
}

TEST(NetProtocol, LobbyAndStartRoundTrip)
{
  LobbyMsg l;
  l.phase = PHASE_BETWEEN_ROUNDS;
  l.round = 3;
  l.mapWidth = 25;
  l.mapHeight = 19;
  l.blockPercentage = 40;
  l.npcSpawner = false;
  l.slots = {{0, 0}, {1, 1}, {NO_PEER, 2}};
  LobbyPeer p0;
  p0.id = 0; p0.slotMask = 1; p0.rttMs = 0; p0.name = "host";
  LobbyPeer p1;
  p1.id = 1; p1.slotMask = 2; p1.rttMs = 123; p1.name = "guest";
  l.peers = {p0, p1};
  auto lb = packet(MSG_LOBBY, 0, 9, [&](Writer& w) { encode(w, l); });
  LobbyMsg lback;
  ASSERT_TRUE(decodeBody(lb, lback));
  EXPECT_EQ(lback.phase, PHASE_BETWEEN_ROUNDS);
  EXPECT_EQ(lback.round, 3);
  EXPECT_EQ(lback.mapWidth, 25);
  EXPECT_FALSE(lback.npcSpawner);
  ASSERT_EQ(lback.slots.size(), 3u);
  EXPECT_EQ(lback.slots[2].owner, NO_PEER);
  ASSERT_EQ(lback.peers.size(), 2u);
  EXPECT_EQ(lback.peers[1].rttMs, 123);
  EXPECT_EQ(lback.peers[1].name, "guest");

  StartMsg s;
  s.round = 2;
  s.seed = 0x0123456789ABCDEFull;
  s.mapWidth = 17;
  s.mapHeight = 13;
  s.blockPercentage = 55;
  s.npcSpawner = true;
  s.slots = {{0, 0, false}, {1, 1, false}, {NO_PEER, 2, true}};
  s.delay = 5;
  auto sb = packet(MSG_START, 0, 9, [&](Writer& w) { encode(w, s); });
  StartMsg sback;
  ASSERT_TRUE(decodeBody(sb, sback));
  EXPECT_EQ(sback.seed, s.seed);
  EXPECT_EQ(sback.delay, 5);
  ASSERT_EQ(sback.slots.size(), 3u);
  EXPECT_TRUE(sback.slots[2].ai);
  EXPECT_FALSE(sback.slots[1].ai);
  EXPECT_EQ(sback.slots[1].owner, 1);
}

TEST(NetProtocol, InputRoundTripsWithMasksAndEvents)
{
  InputMsg m;
  m.round = 4;
  m.ack = 100;
  m.first = 101;
  m.records.push_back(record(0b000101, {0x11, 0x22}));
  m.records.push_back(record(0, {}));
  m.records.push_back(record(0b100000, {0xC1}, {ControlEvent(5, ControlEvent::HUMAN_TAKES_OVER), ControlEvent(2, ControlEvent::RETURN_TO_AI)}));
  auto bytes = packet(MSG_INPUT, 1, 9, [&](Writer& w) { encode(w, m); });
  InputMsg back;
  ASSERT_TRUE(decodeBody(bytes, back));
  EXPECT_EQ(back.round, 4);
  EXPECT_EQ(back.ack, 100u);
  EXPECT_EQ(back.first, 101u);
  ASSERT_EQ(back.records.size(), 3u);
  for (size_t i = 0; i < 3; i++)
  {
    EXPECT_TRUE(back.records[i] == m.records[i]) << i;
  }
  EXPECT_EQ(back.records[2].events.size(), 2u);
  EXPECT_EQ(back.records[2].events[1], ControlEvent(2, ControlEvent::RETURN_TO_AI));

  // The size the sender budgets with is the size written.
  size_t expected = INPUT_FIXED_SIZE;
  for (const InputRecord& r : m.records) expected += encodedSize(r);
  EXPECT_EQ(bytes.size(), expected);
}

TEST(NetProtocol, TicksRoundTripAndTheSizeIsAsBudgeted)
{
  TicksMsg m;
  m.round = 1;
  m.ack = 7;
  m.first = 8;
  m.waitingMask = 0b10;
  m.owners[0] = 0; m.owners[1] = 2; m.owners[2] = NO_PEER; m.owners[5] = 5;
  for (int i = 0; i < 20; i++)
  {
    TickInputs t;
    for (int s = 0; s < 6; s++) t.slot[s] = PlayerInput((uint8_t) (i * 6 + s));
    if (i == 3) t.control.push_back(ControlEvent(1, ControlEvent::LEAVE));
    m.ticks.push_back(t);
  }
  auto bytes = packet(MSG_TICKS, 0, 9, [&](Writer& w) { encode(w, m); });
  TicksMsg back;
  ASSERT_TRUE(decodeBody(bytes, back));
  ASSERT_EQ(back.ticks.size(), 20u);
  EXPECT_EQ(back.waitingMask, 0b10);
  for (int s = 0; s < 6; s++) EXPECT_EQ(back.owners[s], m.owners[s]) << s;
  for (size_t i = 0; i < 20; i++)
  {
    for (int s = 0; s < 6; s++) EXPECT_EQ(back.ticks[i].slot[s], m.ticks[i].slot[s]);
    EXPECT_EQ(back.ticks[i].control, m.ticks[i].control);
  }
  size_t expected = TICKS_FIXED_SIZE;
  for (const TickInputs& t : m.ticks) expected += encodedSize(t);
  EXPECT_EQ(bytes.size(), expected);
  EXPECT_EQ(TICKS_FIXED_SIZE, HEADER_SIZE + 12 + 6);   // + the owner of each slot
}

TEST(NetProtocol, OnePacketRepeatsSecondsOfTicks)
{
  // How many ticks one datagram repeats: this is the redundancy window of D7. At 7 bytes
  // a tick, a 1100-byte packet carries 150 ticks, five seconds of play.
  const size_t perTick = encodedSize(TickInputs());
  EXPECT_EQ(perTick, 7u);
  EXPECT_GE((MAX_PACKET - TICKS_FIXED_SIZE) / perTick, 140u);
  EXPECT_GE(MAX_TICKS_PER_PACKET, (MAX_PACKET - TICKS_FIXED_SIZE) / perTick);   // the decoder accepts a full packet
}

TEST(NetProtocol, ChecksumDesyncPingKeepaliveLeaveRoundTrip)
{
  ChecksumMsg c;
  c.round = 2;
  for (uint32_t i = 1; i <= 10; i++) c.entries.push_back({i * 30, 0x1111u * i});
  auto cb = packet(MSG_CHECKSUM, 1, 9, [&](Writer& w) { encode(w, c); });
  ChecksumMsg cback;
  ASSERT_TRUE(decodeBody(cb, cback));
  // Ten entries: the newest eight are sent.
  ASSERT_EQ(cback.entries.size(), MAX_CHECKSUMS);
  EXPECT_EQ(cback.entries.front().tick, 90u);
  EXPECT_EQ(cback.entries.back().tick, 300u);

  DesyncMsg d;
  d.round = 2; d.tick = 150; d.hostHash = 0xAA; d.peerHash = 0xBB; d.offender = 2;
  auto db = packet(MSG_DESYNC, 0, 9, [&](Writer& w) { encode(w, d); });
  DesyncMsg dback;
  ASSERT_TRUE(decodeBody(db, dback));
  EXPECT_EQ(dback.tick, 150u);
  EXPECT_EQ(dback.peerHash, 0xBBu);
  EXPECT_EQ(dback.offender, 2);

  PingMsg p;
  p.seq = 12; p.sentMs = 0xFFFFFFF0;
  auto pb = packet(MSG_PING, 0, 9, [&](Writer& w) { encode(w, p); });
  PingMsg pback;
  ASSERT_TRUE(decodeBody(pb, pback));
  EXPECT_EQ(pback.sentMs, 0xFFFFFFF0u);

  KeepaliveMsg k;
  k.round = 3; k.ack = 55;
  auto kb = packet(MSG_KEEPALIVE, 1, 9, [&](Writer& w) { encode(w, k); });
  KeepaliveMsg kback;
  ASSERT_TRUE(decodeBody(kb, kback));
  EXPECT_EQ(kback.ack, 55u);

  LeaveMsg l;
  l.reason = LEAVE_HOST_CLOSED;
  auto lb = packet(MSG_LEAVE, 0, 9, [&](Writer& w) { encode(w, l); });
  LeaveMsg lback;
  ASSERT_TRUE(decodeBody(lb, lback));
  EXPECT_EQ(lback.reason, LEAVE_HOST_CLOSED);
}

// Every message, cut at every length, is refused; with one byte appended, refused too.
TEST(NetProtocol, EveryTruncationAndTrailingByteIsRefused)
{
  std::vector<std::vector<uint8_t>> all;
  { HelloMsg m; m.name = "n"; all.push_back(packet(MSG_HELLO, NO_PEER, 0, [&](Writer& w) { encode(w, m); })); }
  { WelcomeMsg m; m.peerId = 1; all.push_back(packet(MSG_WELCOME, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { RejectMsg m; m.text = "x"; all.push_back(packet(MSG_REJECT, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { LobbyMsg m; m.slots = {{0, 0}}; LobbyPeer p; p.name = "a"; m.peers = {p}; all.push_back(packet(MSG_LOBBY, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { StartMsg m; m.slots = {{0, 0, false}, {NO_PEER, 1, true}}; all.push_back(packet(MSG_START, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { InputMsg m; m.records = {record(1, {3}, {ControlEvent(0, ControlEvent::LEAVE)})}; all.push_back(packet(MSG_INPUT, 1, 1, [&](Writer& w) { encode(w, m); })); }
  { TicksMsg m; TickInputs t; t.control.push_back(ControlEvent(1, ControlEvent::RETURN_TO_AI)); m.ticks = {t, TickInputs()}; all.push_back(packet(MSG_TICKS, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { ChecksumMsg m; m.entries = {{30, 1}, {60, 2}}; all.push_back(packet(MSG_CHECKSUM, 1, 1, [&](Writer& w) { encode(w, m); })); }
  { DesyncMsg m; all.push_back(packet(MSG_DESYNC, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { PingMsg m; all.push_back(packet(MSG_PING, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { KeepaliveMsg m; all.push_back(packet(MSG_KEEPALIVE, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { LeaveMsg m; all.push_back(packet(MSG_LEAVE, 0, 1, [&](Writer& w) { encode(w, m); })); }

  auto decodeAny = [](const std::vector<uint8_t>& b) -> bool {
    Reader r(b.data(), b.size());
    Header h;
    if (!readHeader(r, h)) return false;
    switch (h.type)
    {
      case MSG_HELLO: { HelloMsg m; return decode(r, m); }
      case MSG_WELCOME: { WelcomeMsg m; return decode(r, m); }
      case MSG_REJECT: { RejectMsg m; return decode(r, m); }
      case MSG_LOBBY: { LobbyMsg m; return decode(r, m); }
      case MSG_START: { StartMsg m; return decode(r, m); }
      case MSG_INPUT: { InputMsg m; return decode(r, m); }
      case MSG_TICKS: { TicksMsg m; return decode(r, m); }
      case MSG_CHECKSUM: { ChecksumMsg m; return decode(r, m); }
      case MSG_DESYNC: { DesyncMsg m; return decode(r, m); }
      case MSG_PING: case MSG_PONG: { PingMsg m; return decode(r, m); }
      case MSG_KEEPALIVE: { KeepaliveMsg m; return decode(r, m); }
      case MSG_LEAVE: { LeaveMsg m; return decode(r, m); }
    }
    return false;
  };

  for (const auto& full : all)
  {
    ASSERT_TRUE(decodeAny(full)) << messageName(full[2]);
    for (size_t n = 0; n < full.size(); n++)
    {
      std::vector<uint8_t> cut(full.begin(), full.begin() + n);
      EXPECT_FALSE(decodeAny(cut)) << messageName(full[2]) << " cut to " << n;
    }
    std::vector<uint8_t> longer = full;
    longer.push_back(0);
    EXPECT_FALSE(decodeAny(longer)) << messageName(full[2]) << " with a trailing byte";
  }
}

TEST(NetProtocol, CountsThatDoNotFitAreRefusedBeforeAnythingIsAllocated)
{
  // An INPUT that claims 255 records and holds none.
  auto bytes = packet(MSG_INPUT, 1, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(1); w.u8(255); });
  InputMsg m;
  EXPECT_FALSE(decodeBody(bytes, m));
  // First tick 0 does not exist (ticks start at 1).
  bytes = packet(MSG_INPUT, 1, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(0); w.u8(0); });
  EXPECT_FALSE(decodeBody(bytes, m));
  // A tick number that would wrap.
  bytes = packet(MSG_INPUT, 1, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(0xFFFFFFFF); w.u8(1); w.u8(0); w.u8(0); });
  EXPECT_FALSE(decodeBody(bytes, m));
  // More events in a tick than a round can hold.
  bytes = packet(MSG_INPUT, 1, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(1); w.u8(1); w.u8(0); w.u8(200); });
  EXPECT_FALSE(decodeBody(bytes, m));
  // A mask naming slots past the sixth.
  bytes = packet(MSG_INPUT, 1, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(1); w.u8(1); w.u8(0xFF); });
  EXPECT_FALSE(decodeBody(bytes, m));
  TicksMsg t;
  bytes = packet(MSG_TICKS, 0, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(1); w.u8(255); w.u8(0); for (int i = 0; i < 6; i++) w.u8(NO_PEER); });
  EXPECT_FALSE(decodeBody(bytes, t));
  // An owner that is no peer id.
  bytes = packet(MSG_TICKS, 0, 1, [](Writer& w) { w.u16(1); w.u32(0); w.u32(1); w.u8(0); w.u8(0); for (int i = 0; i < 6; i++) w.u8(77); });
  EXPECT_FALSE(decodeBody(bytes, t));
  LobbyMsg l;
  bytes = packet(MSG_LOBBY, 0, 1, [](Writer& w) { w.u8(0); w.u16(0); w.u8(17); w.u8(13); w.u8(55); w.u8(1); w.u8(7); });
  EXPECT_FALSE(decodeBody(bytes, l));   // seven slots
  bytes = packet(MSG_LOBBY, 0, 1, [](Writer& w) { w.u8(9); w.u16(0); w.u8(17); w.u8(13); w.u8(55); w.u8(1); w.u8(0); w.u8(0); });
  EXPECT_FALSE(decodeBody(bytes, l));   // an unknown phase
}

TEST(NetProtocol, EventValidityMatchesWhatTheSimulationAccepts)
{
  EXPECT_TRUE(validEvent(ControlEvent(0, ControlEvent::HUMAN_TAKES_OVER), 2));
  EXPECT_TRUE(validEvent(ControlEvent(1, ControlEvent::LEAVE), 2));
  EXPECT_FALSE(validEvent(ControlEvent(2, ControlEvent::RETURN_TO_AI), 2));   // past the roster
  EXPECT_FALSE(validEvent(ControlEvent(-1, ControlEvent::RETURN_TO_AI), 2));
  ControlEvent unknown(0, (ControlEvent::Kind) 9);
  EXPECT_FALSE(validEvent(unknown, 6));
}

// Random bytes, and valid packets with random bytes flipped: no decoder crashes, reads out
// of bounds (the sanitizers would say) or loops.
TEST(NetProtocol, FuzzedDatagramsNeverCrashADecoder)
{
  std::mt19937 rng(12345);
  std::vector<std::vector<uint8_t>> seeds;
  { InputMsg m; m.records = {record(3, {1, 2}, {ControlEvent(0, ControlEvent::LEAVE)})}; seeds.push_back(packet(MSG_INPUT, 1, 1, [&](Writer& w) { encode(w, m); })); }
  { TicksMsg m; m.ticks = {TickInputs(), TickInputs()}; seeds.push_back(packet(MSG_TICKS, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { LobbyMsg m; m.slots = {{0, 0}}; LobbyPeer p; p.name = "a"; m.peers = {p}; seeds.push_back(packet(MSG_LOBBY, 0, 1, [&](Writer& w) { encode(w, m); })); }
  { HelloMsg m; m.name = "abc"; seeds.push_back(packet(MSG_HELLO, NO_PEER, 0, [&](Writer& w) { encode(w, m); })); }
  { StartMsg m; m.slots = {{0, 0, false}}; seeds.push_back(packet(MSG_START, 0, 1, [&](Writer& w) { encode(w, m); })); }

  auto tryAll = [](const std::vector<uint8_t>& b) {
    for (int type = 1; type <= 13; type++)
    {
      Reader r(b.data(), b.size());
      Header h;
      if (!readHeader(r, h)) continue;
      HelloMsg a; WelcomeMsg b2; RejectMsg c; LobbyMsg d; StartMsg e; InputMsg f; TicksMsg g; ChecksumMsg h2; DesyncMsg i; PingMsg j; KeepaliveMsg k; LeaveMsg l;
      Reader r1 = r; decode(r1, a);
      Reader r2 = r; decode(r2, b2);
      Reader r3 = r; decode(r3, c);
      Reader r4 = r; decode(r4, d);
      Reader r5 = r; decode(r5, e);
      Reader r6 = r; decode(r6, f);
      Reader r7 = r; decode(r7, g);
      Reader r8 = r; decode(r8, h2);
      Reader r9 = r; decode(r9, i);
      Reader r10 = r; decode(r10, j);
      Reader r11 = r; decode(r11, k);
      Reader r12 = r; decode(r12, l);
      break;
    }
  };
  for (int round = 0; round < 3000; round++)
  {
    std::vector<uint8_t> b;
    if (round % 2 == 0)
    {
      const size_t n = rng() % 200;
      for (size_t i = 0; i < n; i++) b.push_back((uint8_t) rng());
      if (n > 2) { b[0] = 'B'; b[1] = 'N'; }
    }
    else
    {
      b = seeds[rng() % seeds.size()];
      const int flips = 1 + (int) (rng() % 4);
      for (int f = 0; f < flips; f++) b[rng() % b.size()] = (uint8_t) rng();
      if (rng() % 3 == 0) b.resize(rng() % (b.size() + 1));
    }
    tryAll(b);
  }
  SUCCEED();
}
