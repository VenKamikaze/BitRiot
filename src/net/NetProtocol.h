/*
 * NetProtocol.h
 *
 * The wire format of Bit Riot's lockstep netplay (brief 07): every message, its
 * encoder and its decoder, and nothing else. No sockets, no SDL, no Simulation: the
 * LockstepSession (LockstepSession.h) sends and receives these through vnet, and
 * the tests encode, decode and mangle them directly. The full specification, with
 * the versioning rules, is doco/netplay/protocol.md.
 *
 * Every datagram is at most VNET_MAX_DATAGRAM (1100) bytes, little-endian, fixed
 * width, and starts with the same 8-byte header (Header): two magic bytes, the
 * message type, the sender's peer id and the session id. The header never changes
 * between protocol versions; everything after it may. HELLO is the one message a
 * host must be able to read from a peer of another version, so its first field is the
 * protocol version.
 *
 * Decoders are strict and total: a datagram that is truncated, has a count that does
 * not fit, trailing bytes, or a value outside its range makes decode() return false
 * and never reads or allocates past what the datagram holds (the fuzz test feeds
 * them random bytes). A decoded message is well-formed, not trusted: whether a peer
 * may do what it asks is the session's business.
 */

#ifndef _NETPROTOCOL_H
#define _NETPROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "TickInputs.h"

namespace netplay
{
  // Bumped when anything after the header changes. A host refuses a HELLO of another
  // version (REJECT_VERSION) before reading the rest of it.
  const uint16_t PROTOCOL_VERSION = 3;

  // vnet's VNET_MAX_DATAGRAM, repeated here so this header needs no vnet.h.
  const size_t MAX_PACKET = 1100;
  const size_t HEADER_SIZE = 8;
  const uint8_t MAGIC0 = 'B';
  const uint8_t MAGIC1 = 'N';

  // Peer ids: the host is 0, its peers 1..MAX_PEERS-1, NO_PEER is "none yet / nobody".
  const uint8_t HOST_ID = 0;
  const uint8_t NO_PEER = 0xFF;
  const int MAX_PEERS = 6;   // humans on different machines, the host included
  const int MAX_SLOTS = 6;   // the same cap as local play (README Q4)
  const size_t MAX_NAME = 24;
  // Ticks in one INPUT / TICKS datagram, whatever fits: the size budget decides, this
  // only bounds what a decoder accepts.
  const size_t MAX_TICKS_PER_PACKET = 255;
  const size_t MAX_CHECKSUMS = 8;

  enum MsgType : uint8_t
  {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_REJECT = 3,
    MSG_LOBBY = 4,
    MSG_START = 5,
    MSG_INPUT = 6,
    MSG_TICKS = 7,
    MSG_CHECKSUM = 8,
    MSG_DESYNC = 9,
    MSG_PING = 10,
    MSG_PONG = 11,
    MSG_KEEPALIVE = 12,
    MSG_LEAVE = 13,
    MSG_READY = 14,
    MSG_WELCOME_MIDMATCH = 15,   // version 3 (brief 13): the answer to a HELLO while a round runs
    MSG_READY_AT = 16,           // version 3: a late joiner has caught up
    MSG_TAKEOVER_AT = 17         // version 3: the host's answer to READY_AT
  };

  enum RejectReason : uint8_t
  {
    REJECT_VERSION = 1,      // another protocol version
    REJECT_BUILD = 2,        // another build id: different simulation code
    REJECT_DATA = 3,         // another data hash: different data/ files
    REJECT_FULL = 4,         // no free player slot for the players asked for
    REJECT_IN_PROGRESS = 5,  // a round is running and this host does not take late joiners (SessionOptions::allowHotJoin), or the match
                             // is stopped by a desync. Since version 3 a host normally lets a joiner in mid-round (WELCOME_MIDMATCH).
    REJECT_BAD_REQUEST = 6   // a HELLO that makes no sense (no players, a bad name)
  };

  enum LeaveReason : uint8_t
  {
    LEAVE_QUIT = 0,          // the player quit (ESC)
    LEAVE_HOST_CLOSED = 1,   // the host ended the session
    LEAVE_DESYNC = 2,        // the sender stopped because of a desync
    LEAVE_KICKED = 3,        // host -> peer: you were silent too long and were dropped
    LEAVE_REMOVED = 4        // host -> peer: the host removed you from the lobby (version 2)
  };
  const uint8_t LEAVE_REASON_MAX = LEAVE_REMOVED;

  // The common start of every datagram.
  struct Header
  {
    uint8_t type = 0;
    uint8_t sender = NO_PEER;
    uint32_t session = 0;    // chosen by the host; 0 in a HELLO (none yet)
  };

  // -- the messages ----------------------------------------------------------------

  // peer -> host, repeated until WELCOME or REJECT. `protocol` is first on purpose.
  struct HelloMsg
  {
    uint16_t protocol = PROTOCOL_VERSION;
    uint64_t buildId = 0;
    uint64_t dataHash = 0;
    uint8_t localPlayers = 1;  // how many player slots this machine wants (1..MAX_SLOTS)
    uint32_t nonce = 0;        // picked by the peer: matches the answer, and makes a repeat idempotent
    uint32_t token = 0;        // a reconnect token from an earlier WELCOME (brief 13), 0 = new
    uint8_t flags = 0;         // version 3: HELLO_SPECTATE
    std::string name;
  };
  // HelloMsg::flags. Any other bit makes the HELLO undecodable.
  const uint8_t HELLO_SPECTATE = 1;   // "do not give me a player slot": watch only (never dealt a slot, never waited for)

  // host -> peer
  struct WelcomeMsg
  {
    uint32_t nonce = 0;
    uint8_t peerId = 0;
    uint32_t token = 0;        // identifies this peer to the host across reconnects (brief 13)
    uint8_t slotMask = 0;      // the slots assigned for now; START and LOBBY are authoritative
    uint8_t maxSlots = MAX_SLOTS;
  };

  // host -> peer (version 3), in answer to a HELLO while a round runs, instead of WELCOME: the same identity, plus where the
  // round is. Repeated (with a fresh `head`) while the joiner catches up. `offerMask` is the slots this peer is being offered
  // back because they were its own before it dropped (a reconnect with its token) and are still bots; 0 for a new joiner.
  struct WelcomeMidMatchMsg
  {
    uint32_t nonce = 0;
    uint8_t peerId = 0;
    uint32_t token = 0;
    uint16_t round = 0;        // the running round
    uint32_t head = 0;         // the newest tick the host has confirmed
    uint8_t offerMask = 0;
  };

  // joiner -> host (version 3), repeated until TAKEOVER_AT: "I have stepped through `tick` and hold what is confirmed;
  // schedule me". The host also wants the joiner's checksums to agree with its own before it gives it a slot.
  struct ReadyAtMsg
  {
    uint16_t round = 0;
    uint32_t tick = 0;
  };

  // host -> joiner (version 3): from `tick` on the machine plays `slotMask` (0 = it watches), and the host waits for its inputs
  // from `tick`. The HUMAN_TAKES_OVER events are in tick `tick` of the final inputs, for everybody.
  struct TakeoverAtMsg
  {
    uint16_t round = 0;
    uint32_t tick = 0;
    uint8_t slotMask = 0;
  };

  // host -> peer, in answer to any HELLO it will not accept. Carries the host's own
  // numbers so the peer can say what differs.
  struct RejectMsg
  {
    uint32_t nonce = 0;
    uint8_t reason = 0;        // RejectReason
    uint16_t hostProtocol = PROTOCOL_VERSION;
    uint64_t hostBuildId = 0;
    uint64_t hostDataHash = 0;
    std::string text;          // a sentence for the player
  };

  struct SlotInfo
  {
    uint8_t owner = NO_PEER;   // the peer id that plays the slot, NO_PEER = a bot
    uint8_t team = 0;
    bool operator==(const SlotInfo& o) const { return owner == o.owner && team == o.team; }
  };

  struct LobbyPeer
  {
    uint8_t id = 0;
    uint8_t slotMask = 0;
    uint16_t rttMs = 0xFFFF;   // 0xFFFF = not measured yet
    bool ready = false;        // version 2: the peer said it is ready (the host is always ready)
    std::string name;
  };

  // host -> every peer, about twice a second whatever the state (it is also the
  // host's keepalive in the lobby).
  struct LobbyMsg
  {
    uint8_t phase = 0;         // LobbyPhase
    uint16_t round = 0;        // the round the next START will be (or the running one)
    uint8_t mapWidth = 17;
    uint8_t mapHeight = 13;
    uint8_t blockPercentage = 55;
    bool npcSpawner = true;
    std::vector<SlotInfo> slots;
    std::vector<LobbyPeer> peers;   // the host included (id 0)
  };
  enum LobbyPhase : uint8_t { PHASE_LOBBY = 0, PHASE_RUNNING = 1, PHASE_BETWEEN_ROUNDS = 2 };

  // host -> every peer, repeated until the peer's first INPUT of the round arrives.
  // Everything a peer needs to build the same round: the MatchConfig, who plays
  // which slot, and D.
  struct StartMsg
  {
    uint16_t round = 0;
    uint64_t seed = 0;
    uint8_t mapWidth = 17;
    uint8_t mapHeight = 13;
    uint8_t blockPercentage = 55;
    bool npcSpawner = true;
    struct Slot
    {
      uint8_t owner = NO_PEER;
      uint8_t team = 0;
      bool ai = true;          // starts as a bot (a human slot starts as a human)
    };
    std::vector<Slot> slots;
    uint8_t delay = 3;         // D, the input delay in ticks
  };

  // One peer's input for one tick: for each slot in `mask`, a PlayerInput byte, plus
  // the control events it asks for. Slots not in the mask were not sampled here.
  struct InputRecord
  {
    uint8_t mask = 0;
    uint8_t bytes[MAX_SLOTS] = {0, 0, 0, 0, 0, 0};   // indexed by slot, valid where mask has the bit
    std::vector<ControlEvent> events;
    bool operator==(const InputRecord& o) const;
  };

  // peer -> host, every time there is something new, and again while anything is
  // unacknowledged: ALL records the host has not acknowledged, oldest first (D7).
  struct InputMsg
  {
    uint16_t round = 0;
    uint32_t ack = 0;          // the highest tick t such that the peer holds the host's TickInputs 1..t
    uint32_t first = 1;        // the tick of records[0]
    std::vector<InputRecord> records;
  };

  // host -> peer, the same idea: every confirmed tick the peer has not acknowledged.
  struct TicksMsg
  {
    uint16_t round = 0;
    uint32_t ack = 0;          // the highest tick t such that the host holds this peer's records for every tick up to t
    uint32_t first = 1;
    uint8_t waitingMask = 0;   // peers (bit = peer id) the host is waiting on for its next tick
    // Who plays each slot after the newest tick in this packet (a peer id, NO_PEER = a bot):
    // how a machine learns whether a takeover it asked for was granted.
    uint8_t owners[MAX_SLOTS] = {NO_PEER, NO_PEER, NO_PEER, NO_PEER, NO_PEER, NO_PEER};
    std::vector<TickInputs> ticks;
  };

  struct ChecksumEntry
  {
    uint32_t tick = 0;
    uint64_t hash = 0;
    bool operator==(const ChecksumEntry& o) const { return tick == o.tick && hash == o.hash; }
  };
  // peer -> host: its latest checkpoint hashes (stateHash() at multiples of 30).
  struct ChecksumMsg
  {
    uint16_t round = 0;
    std::vector<ChecksumEntry> entries;   // oldest first, at most MAX_CHECKSUMS
  };

  // host -> every peer: the state at `tick` differs between the host and `offender`.
  struct DesyncMsg
  {
    uint16_t round = 0;
    uint32_t tick = 0;
    uint64_t hostHash = 0;
    uint64_t peerHash = 0;
    uint8_t offender = NO_PEER;
  };

  struct PingMsg
  {
    uint32_t seq = 0;
    uint32_t sentMs = 0;       // the sender's vnet_time(), low 32 bits; echoed back untouched
  };

  struct KeepaliveMsg
  {
    uint16_t round = 0;
    uint32_t ack = 0;          // as in INPUT (peer) / TICKS (host)
  };

  struct LeaveMsg
  {
    uint8_t reason = LEAVE_QUIT;
  };

  // peer -> host (version 2): "I am / am not ready to start". A state, not an event: the peer
  // repeats it while the host's LOBBY does not show the same, and `seq` (the peer's own counter)
  // makes a late duplicate of an older answer harmless. `round` is the round the next START
  // will be, so an answer from before a round started cannot be applied to the lobby after it.
  struct ReadyMsg
  {
    uint16_t round = 0;
    uint32_t seq = 0;
    bool ready = false;
  };

  // -- bytes ------------------------------------------------------------------------

  class Writer
  {
    public:
      void u8(uint8_t v) { m_buf.push_back(v); }
      void u16(uint16_t v);
      void u32(uint32_t v);
      void u64(uint64_t v);
      // A string of at most max bytes: a u8 length, then the bytes (longer is cut).
      void str(const std::string& s, size_t max = MAX_NAME);
      size_t size() const { return m_buf.size(); }
      std::vector<uint8_t>& bytes() { return m_buf; }
    private:
      std::vector<uint8_t> m_buf;
  };

  class Reader
  {
    public:
      Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
      uint8_t u8();
      uint16_t u16();
      uint32_t u32();
      uint64_t u64();
      std::string str(size_t max = MAX_NAME);
      size_t remaining() const { return m_size - m_pos; }
      // False once a read ran past the end (the value returned then was 0).
      bool ok() const { return m_ok; }
      // ok(), and the whole datagram was used.
      bool done() const { return m_ok && m_pos == m_size; }
    private:
      const uint8_t* m_data;
      size_t m_size;
      size_t m_pos = 0;
      bool m_ok = true;
  };

  // -- encode / decode ----------------------------------------------------------------

  // Starts a datagram with its header; the encoders below append the body.
  void writeHeader(Writer& w, const Header& h);
  // Reads the header of a datagram. False if it is too short or the magic is wrong.
  bool readHeader(Reader& r, Header& out);

  void encode(Writer& w, const HelloMsg& m);
  void encode(Writer& w, const WelcomeMsg& m);
  void encode(Writer& w, const RejectMsg& m);
  void encode(Writer& w, const LobbyMsg& m);
  void encode(Writer& w, const StartMsg& m);
  void encode(Writer& w, const InputMsg& m);
  void encode(Writer& w, const TicksMsg& m);
  void encode(Writer& w, const ChecksumMsg& m);
  void encode(Writer& w, const DesyncMsg& m);
  void encode(Writer& w, const PingMsg& m);
  void encode(Writer& w, const KeepaliveMsg& m);
  void encode(Writer& w, const LeaveMsg& m);
  void encode(Writer& w, const ReadyMsg& m);
  void encode(Writer& w, const WelcomeMidMatchMsg& m);
  void encode(Writer& w, const ReadyAtMsg& m);
  void encode(Writer& w, const TakeoverAtMsg& m);

  // Each reads the body that follows a header and returns whether the datagram was
  // exactly a well-formed message of that type. `r` is positioned after the header.
  // (HELLO's decoder reads only the protocol version first: see decodeHelloVersion.)
  bool decode(Reader& r, HelloMsg& m);
  bool decode(Reader& r, WelcomeMsg& m);
  bool decode(Reader& r, RejectMsg& m);
  bool decode(Reader& r, LobbyMsg& m);
  bool decode(Reader& r, StartMsg& m);
  bool decode(Reader& r, InputMsg& m);
  bool decode(Reader& r, TicksMsg& m);
  bool decode(Reader& r, ChecksumMsg& m);
  bool decode(Reader& r, DesyncMsg& m);
  bool decode(Reader& r, PingMsg& m);
  bool decode(Reader& r, KeepaliveMsg& m);
  bool decode(Reader& r, LeaveMsg& m);
  bool decode(Reader& r, ReadyMsg& m);
  bool decode(Reader& r, WelcomeMidMatchMsg& m);
  bool decode(Reader& r, ReadyAtMsg& m);
  bool decode(Reader& r, TakeoverAtMsg& m);

  // The protocol version at the start of a HELLO body, readable whatever follows it
  // (a HELLO of another version may be laid out differently). False if the body has
  // fewer than two bytes.
  bool decodeHelloVersion(const uint8_t* body, size_t size, uint16_t& version);

  // Sizes the senders use to fill a datagram without overflowing it.
  size_t encodedSize(const InputRecord& r);
  size_t encodedSize(const TickInputs& t);
  // The bytes of an INPUT / TICKS datagram before its first record.
  const size_t INPUT_FIXED_SIZE = HEADER_SIZE + 2 + 4 + 4 + 1;
  const size_t TICKS_FIXED_SIZE = HEADER_SIZE + 2 + 4 + 4 + 1 + 1 + MAX_SLOTS;

  // True for a control event the simulation would accept in a round of numSlots
  // players: a known kind and a slot inside the roster.
  bool validEvent(const ControlEvent& e, int numSlots);

  std::string messageName(uint8_t type);
}

#endif /* _NETPROTOCOL_H */
