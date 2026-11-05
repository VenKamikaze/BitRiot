#include "NetProtocol.h"

#include <algorithm>

namespace netplay
{
// -- Writer / Reader ------------------------------------------------------------------

void Writer::u16(uint16_t v)
{
  m_buf.push_back((uint8_t) (v & 0xFF));
  m_buf.push_back((uint8_t) (v >> 8));
}

void Writer::u32(uint32_t v)
{
  for (int i = 0; i < 4; i++)
  {
    m_buf.push_back((uint8_t) (v >> (8 * i)));
  }
}

void Writer::u64(uint64_t v)
{
  for (int i = 0; i < 8; i++)
  {
    m_buf.push_back((uint8_t) (v >> (8 * i)));
  }
}

void Writer::str(const std::string& s, size_t max)
{
  const size_t n = std::min(s.size(), max);
  u8((uint8_t) n);
  for (size_t i = 0; i < n; i++)
  {
    m_buf.push_back((uint8_t) s[i]);
  }
}

uint8_t Reader::u8()
{
  if (!m_ok || m_size - m_pos < 1)
  {
    m_ok = false;
    return 0;
  }
  return m_data[m_pos++];
}

uint16_t Reader::u16()
{
  if (!m_ok || m_size - m_pos < 2)
  {
    m_ok = false;
    return 0;
  }
  const uint16_t v = (uint16_t) (m_data[m_pos] | (m_data[m_pos + 1] << 8));
  m_pos += 2;
  return v;
}

uint32_t Reader::u32()
{
  if (!m_ok || m_size - m_pos < 4)
  {
    m_ok = false;
    return 0;
  }
  uint32_t v = 0;
  for (int i = 0; i < 4; i++)
  {
    v |= (uint32_t) m_data[m_pos + i] << (8 * i);
  }
  m_pos += 4;
  return v;
}

uint64_t Reader::u64()
{
  if (!m_ok || m_size - m_pos < 8)
  {
    m_ok = false;
    return 0;
  }
  uint64_t v = 0;
  for (int i = 0; i < 8; i++)
  {
    v |= (uint64_t) m_data[m_pos + i] << (8 * i);
  }
  m_pos += 8;
  return v;
}

std::string Reader::str(size_t max)
{
  const size_t n = u8();
  if (!m_ok || n > max || m_size - m_pos < n)
  {
    m_ok = false;
    return std::string();
  }
  std::string s;
  for (size_t i = 0; i < n; i++)
  {
    const char c = (char) m_data[m_pos + i];
    // Control characters have no place in a name or a message; they are replaced so a
    // hostile peer cannot put escape sequences in someone's terminal or log.
    s.push_back((c >= 0x20 && c != 0x7F) ? c : '?');
  }
  m_pos += n;
  return s;
}

// -- header ---------------------------------------------------------------------------

void writeHeader(Writer& w, const Header& h)
{
  w.u8(MAGIC0);
  w.u8(MAGIC1);
  w.u8(h.type);
  w.u8(h.sender);
  w.u32(h.session);
}

bool readHeader(Reader& r, Header& out)
{
  const uint8_t m0 = r.u8();
  const uint8_t m1 = r.u8();
  out.type = r.u8();
  out.sender = r.u8();
  out.session = r.u32();
  return r.ok() && m0 == MAGIC0 && m1 == MAGIC1;
}

// -- shared pieces ---------------------------------------------------------------------

namespace
{
void writeEvents(Writer& w, const std::vector<ControlEvent>& events)
{
  w.u8((uint8_t) events.size());
  for (const ControlEvent& e : events)
  {
    w.u8((uint8_t) e.slot);
    w.u8((uint8_t) e.kind);
  }
}

// At most MatchRecord's per-tick bound (4 per slot); more is a hostile or broken packet.
const size_t MAX_EVENTS = 4 * MAX_SLOTS;

bool readEvents(Reader& r, std::vector<ControlEvent>& out)
{
  const size_t n = r.u8();
  if (!r.ok() || n > MAX_EVENTS || r.remaining() < 2 * n)
  {
    return false;
  }
  out.clear();
  for (size_t i = 0; i < n; i++)
  {
    const uint8_t slot = r.u8();
    const uint8_t kind = r.u8();
    // The kind byte is kept as it came; whether it is a kind the simulation knows is
    // validEvent()'s question, so that a peer's unknown kind can be dropped by the
    // host instead of making the whole packet (and its inputs) vanish.
    ControlEvent e;
    e.slot = slot;
    e.kind = (ControlEvent::Kind) kind;
    out.push_back(e);
  }
  return r.ok();
}

void writeConfigBits(Writer& w, uint8_t mapW, uint8_t mapH, uint8_t blocks, bool spawner)
{
  w.u8(mapW);
  w.u8(mapH);
  w.u8(blocks);
  w.u8(spawner ? 1 : 0);
}
} // namespace

bool InputRecord::operator==(const InputRecord& o) const
{
  if (mask != o.mask || events != o.events)
  {
    return false;
  }
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if ((mask & (1 << i)) && bytes[i] != o.bytes[i])
    {
      return false;
    }
  }
  return true;
}

size_t encodedSize(const InputRecord& r)
{
  size_t n = 1 + 1; // mask, event count
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    n += (r.mask >> i) & 1;
  }
  return n + 2 * r.events.size();
}

size_t encodedSize(const TickInputs& t)
{
  return MAX_SLOTS + 1 + 2 * t.control.size();
}

bool validEvent(const ControlEvent& e, int numSlots)
{
  return e.slot >= 0 && e.slot < numSlots && e.slot < MAX_SLOTS
      && ((uint8_t) e.kind == (uint8_t) ControlEvent::HUMAN_TAKES_OVER
          || (uint8_t) e.kind == (uint8_t) ControlEvent::RETURN_TO_AI
          || (uint8_t) e.kind == (uint8_t) ControlEvent::LEAVE);
}

std::string messageName(uint8_t type)
{
  switch (type)
  {
    case MSG_HELLO: return "HELLO";
    case MSG_WELCOME: return "WELCOME";
    case MSG_REJECT: return "REJECT";
    case MSG_LOBBY: return "LOBBY";
    case MSG_START: return "START";
    case MSG_INPUT: return "INPUT";
    case MSG_TICKS: return "TICKS";
    case MSG_CHECKSUM: return "CHECKSUM";
    case MSG_DESYNC: return "DESYNC";
    case MSG_PING: return "PING";
    case MSG_PONG: return "PONG";
    case MSG_KEEPALIVE: return "KEEPALIVE";
    case MSG_LEAVE: return "LEAVE";
    default: return "type " + std::to_string((int) type);
  }
}

// -- HELLO / WELCOME / REJECT -----------------------------------------------------------

void encode(Writer& w, const HelloMsg& m)
{
  w.u16(m.protocol);
  w.u64(m.buildId);
  w.u64(m.dataHash);
  w.u8(m.localPlayers);
  w.u32(m.nonce);
  w.u32(m.token);
  w.str(m.name);
}

bool decode(Reader& r, HelloMsg& m)
{
  m.protocol = r.u16();
  m.buildId = r.u64();
  m.dataHash = r.u64();
  m.localPlayers = r.u8();
  m.nonce = r.u32();
  m.token = r.u32();
  m.name = r.str();
  return r.done();
}

bool decodeHelloVersion(const uint8_t* body, size_t size, uint16_t& version)
{
  Reader r(body, size);
  version = r.u16();
  return r.ok();
}

void encode(Writer& w, const WelcomeMsg& m)
{
  w.u32(m.nonce);
  w.u8(m.peerId);
  w.u32(m.token);
  w.u8(m.slotMask);
  w.u8(m.maxSlots);
}

bool decode(Reader& r, WelcomeMsg& m)
{
  m.nonce = r.u32();
  m.peerId = r.u8();
  m.token = r.u32();
  m.slotMask = r.u8();
  m.maxSlots = r.u8();
  return r.done() && m.peerId != HOST_ID && m.peerId < MAX_PEERS;
}

void encode(Writer& w, const RejectMsg& m)
{
  w.u32(m.nonce);
  w.u8(m.reason);
  w.u16(m.hostProtocol);
  w.u64(m.hostBuildId);
  w.u64(m.hostDataHash);
  w.str(m.text, 120);
}

bool decode(Reader& r, RejectMsg& m)
{
  m.nonce = r.u32();
  m.reason = r.u8();
  m.hostProtocol = r.u16();
  m.hostBuildId = r.u64();
  m.hostDataHash = r.u64();
  m.text = r.str(120);
  return r.done();
}

// -- LOBBY / START --------------------------------------------------------------------------

void encode(Writer& w, const LobbyMsg& m)
{
  w.u8(m.phase);
  w.u16(m.round);
  writeConfigBits(w, m.mapWidth, m.mapHeight, m.blockPercentage, m.npcSpawner);
  w.u8((uint8_t) m.slots.size());
  for (const SlotInfo& s : m.slots)
  {
    w.u8(s.owner);
    w.u8(s.team);
  }
  w.u8((uint8_t) m.peers.size());
  for (const LobbyPeer& p : m.peers)
  {
    w.u8(p.id);
    w.u8(p.slotMask);
    w.u16(p.rttMs);
    w.str(p.name);
  }
}

bool decode(Reader& r, LobbyMsg& m)
{
  m.phase = r.u8();
  m.round = r.u16();
  m.mapWidth = r.u8();
  m.mapHeight = r.u8();
  m.blockPercentage = r.u8();
  m.npcSpawner = r.u8() != 0;
  const size_t nSlots = r.u8();
  if (!r.ok() || nSlots > (size_t) MAX_SLOTS || m.phase > PHASE_BETWEEN_ROUNDS)
  {
    return false;
  }
  m.slots.clear();
  for (size_t i = 0; i < nSlots; i++)
  {
    SlotInfo s;
    s.owner = r.u8();
    s.team = r.u8();
    m.slots.push_back(s);
  }
  const size_t nPeers = r.u8();
  if (!r.ok() || nPeers > (size_t) MAX_PEERS)
  {
    return false;
  }
  m.peers.clear();
  for (size_t i = 0; i < nPeers; i++)
  {
    LobbyPeer p;
    p.id = r.u8();
    p.slotMask = r.u8();
    p.rttMs = r.u16();
    p.name = r.str();
    if (!r.ok())
    {
      return false;
    }
    m.peers.push_back(p);
  }
  return r.done();
}

void encode(Writer& w, const StartMsg& m)
{
  w.u16(m.round);
  w.u64(m.seed);
  writeConfigBits(w, m.mapWidth, m.mapHeight, m.blockPercentage, m.npcSpawner);
  w.u8((uint8_t) m.slots.size());
  for (const StartMsg::Slot& s : m.slots)
  {
    w.u8(s.owner);
    w.u8(s.team);
    w.u8(s.ai ? 1 : 0);
  }
  w.u8(m.delay);
}

bool decode(Reader& r, StartMsg& m)
{
  m.round = r.u16();
  m.seed = r.u64();
  m.mapWidth = r.u8();
  m.mapHeight = r.u8();
  m.blockPercentage = r.u8();
  m.npcSpawner = r.u8() != 0;
  const size_t nSlots = r.u8();
  if (!r.ok() || nSlots > (size_t) MAX_SLOTS)
  {
    return false;
  }
  m.slots.clear();
  for (size_t i = 0; i < nSlots; i++)
  {
    StartMsg::Slot s;
    s.owner = r.u8();
    s.team = r.u8();
    s.ai = r.u8() != 0;
    m.slots.push_back(s);
  }
  m.delay = r.u8();
  return r.done();
}

// -- INPUT / TICKS ------------------------------------------------------------------------------

void encode(Writer& w, const InputMsg& m)
{
  w.u16(m.round);
  w.u32(m.ack);
  w.u32(m.first);
  w.u8((uint8_t) std::min(m.records.size(), MAX_TICKS_PER_PACKET));
  for (size_t k = 0; k < m.records.size() && k < MAX_TICKS_PER_PACKET; k++)
  {
    const InputRecord& rec = m.records[k];
    w.u8(rec.mask);
    for (int i = 0; i < MAX_SLOTS; i++)
    {
      if (rec.mask & (1 << i))
      {
        w.u8(rec.bytes[i]);
      }
    }
    writeEvents(w, rec.events);
  }
}

bool decode(Reader& r, InputMsg& m)
{
  m.round = r.u16();
  m.ack = r.u32();
  m.first = r.u32();
  const size_t n = r.u8();
  if (!r.ok() || m.first == 0)
  {
    return false;
  }
  m.records.clear();
  for (size_t k = 0; k < n; k++)
  {
    InputRecord rec;
    rec.mask = r.u8();
    if (!r.ok() || (rec.mask & ~((1 << MAX_SLOTS) - 1)) != 0)
    {
      return false;
    }
    for (int i = 0; i < MAX_SLOTS; i++)
    {
      if (rec.mask & (1 << i))
      {
        rec.bytes[i] = r.u8();
      }
    }
    if (!r.ok() || !readEvents(r, rec.events))
    {
      return false;
    }
    m.records.push_back(rec);
  }
  // first + n must not wrap: a record's tick is a u32.
  return r.done() && (uint64_t) m.first + n <= 0xFFFFFFFFull;
}

void encode(Writer& w, const TicksMsg& m)
{
  w.u16(m.round);
  w.u32(m.ack);
  w.u32(m.first);
  w.u8((uint8_t) std::min(m.ticks.size(), MAX_TICKS_PER_PACKET));
  w.u8(m.waitingMask);
  for (size_t k = 0; k < m.ticks.size() && k < MAX_TICKS_PER_PACKET; k++)
  {
    const TickInputs& t = m.ticks[k];
    for (int i = 0; i < MAX_SLOTS; i++)
    {
      w.u8(t.slot[i].bits);
    }
    writeEvents(w, t.control);
  }
}

bool decode(Reader& r, TicksMsg& m)
{
  m.round = r.u16();
  m.ack = r.u32();
  m.first = r.u32();
  const size_t n = r.u8();
  m.waitingMask = r.u8();
  if (!r.ok() || m.first == 0)
  {
    return false;
  }
  m.ticks.clear();
  for (size_t k = 0; k < n; k++)
  {
    TickInputs t;
    for (int i = 0; i < MAX_SLOTS; i++)
    {
      t.slot[i] = PlayerInput(r.u8());
    }
    if (!r.ok() || !readEvents(r, t.control))
    {
      return false;
    }
    m.ticks.push_back(std::move(t));
  }
  return r.done() && (uint64_t) m.first + n <= 0xFFFFFFFFull;
}

// -- the rest ----------------------------------------------------------------------------------------

void encode(Writer& w, const ChecksumMsg& m)
{
  w.u16(m.round);
  const size_t n = std::min(m.entries.size(), MAX_CHECKSUMS);
  w.u8((uint8_t) n);
  // The newest entries if there are too many.
  for (size_t i = m.entries.size() - n; i < m.entries.size(); i++)
  {
    w.u32(m.entries[i].tick);
    w.u64(m.entries[i].hash);
  }
}

bool decode(Reader& r, ChecksumMsg& m)
{
  m.round = r.u16();
  const size_t n = r.u8();
  if (!r.ok() || n > MAX_CHECKSUMS || r.remaining() < 12 * n)
  {
    return false;
  }
  m.entries.clear();
  for (size_t i = 0; i < n; i++)
  {
    ChecksumEntry e;
    e.tick = r.u32();
    e.hash = r.u64();
    m.entries.push_back(e);
  }
  return r.done();
}

void encode(Writer& w, const DesyncMsg& m)
{
  w.u16(m.round);
  w.u32(m.tick);
  w.u64(m.hostHash);
  w.u64(m.peerHash);
  w.u8(m.offender);
}

bool decode(Reader& r, DesyncMsg& m)
{
  m.round = r.u16();
  m.tick = r.u32();
  m.hostHash = r.u64();
  m.peerHash = r.u64();
  m.offender = r.u8();
  return r.done();
}

void encode(Writer& w, const PingMsg& m)
{
  w.u32(m.seq);
  w.u32(m.sentMs);
}

bool decode(Reader& r, PingMsg& m)
{
  m.seq = r.u32();
  m.sentMs = r.u32();
  return r.done();
}

void encode(Writer& w, const KeepaliveMsg& m)
{
  w.u16(m.round);
  w.u32(m.ack);
}

bool decode(Reader& r, KeepaliveMsg& m)
{
  m.round = r.u16();
  m.ack = r.u32();
  return r.done();
}

void encode(Writer& w, const LeaveMsg& m)
{
  w.u8(m.reason);
}

bool decode(Reader& r, LeaveMsg& m)
{
  m.reason = r.u8();
  return r.done();
}
} // namespace netplay
