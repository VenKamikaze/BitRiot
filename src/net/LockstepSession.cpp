#include "LockstepSession.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace netplay
{
namespace
{
const size_t RTT_WINDOW = 16;
const size_t MAX_HASHES = 64;
// Events from peers in one merged tick: 18, leaving 6 (one per slot) for the host's own RETURN_TO_AI
// events, so a tick never holds more than 4 * MAX_SLOTS = 24.
const size_t MAX_PEER_EVENTS = 18;
const uint32_t DESYNC_RESEND_MS = 100;
const uint64_t REMOVED_MEMORY_MS = 30000;   // how long a removed peer's packets are answered with LEAVE_REMOVED
const uint32_t CHECKSUM_RESEND_MS = 400;

std::string hex64(uint64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "0x%016llx", (unsigned long long) v);
  return buf;
}

TickInputs emptyTick()
{
  return TickInputs();
}

uint8_t slotBits(int numSlots)
{
  return (uint8_t) ((1u << std::min(numSlots, MAX_SLOTS)) - 1u);
}
} // namespace

// -- construction -------------------------------------------------------------------------

LockstepSession::LockstepSession()
{
  m_owner.fill(NO_PEER);
  std::memset(&m_hostAddr, 0, sizeof(m_hostAddr));
  for (Member& m : m_members)
  {
    std::memset(&m.addr, 0, sizeof(m.addr));
  }
}

LockstepSession::~LockstepSession()
{
  if (m_v != nullptr && m_sock >= 0)
  {
    vnet_close(m_v, m_sock);
  }
}

bool LockstepSession::openSocket(vnet* v, uint16_t port, int* sock, uint16_t* localPort, std::string* error)
{
  if (v == nullptr)
  {
    if (error) *error = "no vnet context";
    return false;
  }
  const int s = vnet_open(v, port);
  if (s < 0)
  {
    if (error) *error = std::string("cannot open UDP port ") + std::to_string(port) + ": " + vnet_strerror(s);
    return false;
  }
  vnet_addr a;
  std::memset(&a, 0, sizeof(a));
  *localPort = 0;
  if (vnet_local_addr(v, s, &a) == 0)
  {
    *localPort = a.port;
  }
  *sock = s;
  return true;
}

void LockstepSession::init(vnet* v, int sock, uint16_t port, const SessionOptions& o)
{
  m_v = v;
  m_sock = sock;
  m_localPort = port;
  m_opt = o;
  if (m_opt.localPlayers < 1) m_opt.localPlayers = 1;
  if (m_opt.localPlayers > MAX_SLOTS) m_opt.localPlayers = MAX_SLOTS;
  m_rng.seed(o.randomSeed);
  m_now = vnet_time(v);
}

std::unique_ptr<LockstepSession> LockstepSession::host(vnet* v, uint16_t port, const SessionOptions& options, std::string* error)
{
  int sock = -1;
  uint16_t localPort = 0;
  if (!openSocket(v, port, &sock, &localPort, error))
  {
    return nullptr;
  }
  std::unique_ptr<LockstepSession> s(new LockstepSession());
  s->init(v, sock, localPort, options);
  s->m_role = HOST;
  s->m_state = LOBBY;
  s->m_myId = HOST_ID;
  do
  {
    s->m_sessionId = (uint32_t) s->m_rng();
  } while (s->m_sessionId == 0);
  Member& me = s->m_members[0];
  me.present = true;
  me.name = options.name;
  me.localPlayers = s->m_opt.localPlayers;
  me.lastHeard = s->m_now;
  s->recomputeRoster();
  return s;
}

std::unique_ptr<LockstepSession> LockstepSession::join(vnet* v, const vnet_addr& hostAddr, const SessionOptions& options, std::string* error)
{
  int sock = -1;
  uint16_t localPort = 0;
  if (!openSocket(v, 0, &sock, &localPort, error))
  {
    return nullptr;
  }
  std::unique_ptr<LockstepSession> s(new LockstepSession());
  s->init(v, sock, localPort, options);
  s->m_role = PEER;
  s->m_state = CONNECTING;
  s->m_hostAddr = hostAddr;
  s->m_members[0].present = true;
  s->m_members[0].addr = hostAddr;
  s->m_members[0].lastHeard = s->m_now;
  s->m_connectStart = s->m_now;
  do
  {
    s->m_myNonce = (uint32_t) s->m_rng();
  } while (s->m_myNonce == 0);
  // The first HELLO goes at once; pump() repeats it.
  HelloMsg h;
  h.buildId = s->m_opt.buildId;
  h.dataHash = s->m_opt.dataHash;
  h.localPlayers = (uint8_t) s->m_opt.localPlayers;
  h.nonce = s->m_myNonce;
  h.name = s->m_opt.name;
  Writer w;
  Header hd;
  hd.type = MSG_HELLO;
  writeHeader(w, hd);
  encode(w, h);
  s->sendRaw(hostAddr, w);
  s->m_lastHelloSent = s->m_now;
  return s;
}

uint32_t LockstepSession::newToken()
{
  uint32_t t;
  do
  {
    t = (uint32_t) m_rng();
  } while (t == 0);
  return t;
}

void LockstepSession::end(EndReason reason, const std::string& text)
{
  if (m_state == ENDED)
  {
    return;
  }
  m_state = ENDED;
  m_endReason = reason;
  m_endText = text;
}

// -- sending ---------------------------------------------------------------------------------

void LockstepSession::sendRaw(const vnet_addr& to, Writer& w)
{
  if (w.size() > MAX_PACKET || w.size() > (size_t) VNET_MAX_DATAGRAM)
  {
    m_stats.badPackets++;   // a bug in a sender: never put an oversize datagram on the wire
    return;
  }
  const int rc = vnet_sendto(m_v, m_sock, w.bytes().data(), w.size(), &to);
  if (rc < 0)
  {
    m_stats.packetsDropped++;   // VNET_EAGAIN / VNET_EIO: a lost packet, which the protocol repairs
  }
  else
  {
    m_stats.packetsSent++;
  }
}

template <typename Msg>
void LockstepSession::sendMsg(uint8_t type, const vnet_addr& to, const Msg& msg)
{
  Writer w;
  Header h;
  h.type = type;
  h.sender = m_myId;
  h.session = m_sessionId;
  writeHeader(w, h);
  encode(w, msg);
  sendRaw(to, w);
}

void LockstepSession::sendReject(const vnet_addr& to, uint32_t nonce, RejectReason reason, const std::string& text)
{
  RejectMsg m;
  m.nonce = nonce;
  m.reason = reason;
  m.hostProtocol = PROTOCOL_VERSION;
  m.hostBuildId = m_opt.buildId;
  m.hostDataHash = m_opt.dataHash;
  m.text = text;
  sendMsg(MSG_REJECT, to, m);
}

// -- pumping ----------------------------------------------------------------------------------

void LockstepSession::pump()
{
  if (m_state == ENDED)
  {
    return;
  }
  vnet_poll(m_v);
  const uint64_t previous = m_now;
  m_now = vnet_time(m_v);

  if (m_opt.tolerateSparsePump && m_now > previous && m_now - previous > m_opt.sparsePumpGapMs)
  {
    // We were not pumped (a hidden browser tab, a sleeping laptop): the others may well have been talking
    // all along, and what they sent is still queued. Do not count our silence against anybody.
    m_sparseGaps++;
    for (Member& m : m_members)
    {
      if (m.present)
      {
        m.lastHeard = m_now;
      }
    }
    m_connectStart = m_now;
  }

  vnet_status_info st;
  const int transport = vnet_status(m_v, &st);
  if (transport == VNET_STATUS_FAILED)
  {
    end(END_NETWORK, std::string("the network failed: ") + vnet_strerror(st.error));
    return;
  }
  if (transport == VNET_STATUS_CONNECTING)
  {
    // A relay context that has not entered its room yet: nothing can be sent (HELLO is repeated and goes when it can),
    // so the host's time to answer starts when the transport is ready, not when we asked it to connect.
    m_connectStart = m_now;
  }

  drain();
  if (m_state == ENDED)
  {
    return;
  }
  checkTimeouts();
  if (m_state == ENDED)
  {
    return;
  }
  if (m_role == HOST)
  {
    confirmTicks();
    hostSend();
  }
  else
  {
    peerSend();
  }
}

void LockstepSession::drain()
{
  uint8_t buf[VNET_MAX_DATAGRAM];
  for (int i = 0; i < 1024; i++)
  {
    vnet_addr from;
    std::memset(&from, 0, sizeof(from));
    const int n = vnet_recvfrom(m_v, m_sock, buf, sizeof(buf), &from);
    if (n >= 0)
    {
      handle(from, buf, (size_t) n);
      if (m_state == ENDED)
      {
        return;
      }
    }
    else if (n == VNET_EAGAIN || n == VNET_EIO)
    {
      break;
    }
    // VNET_EMSGSIZE: that datagram was consumed; carry on.
  }
}

void LockstepSession::handle(const vnet_addr& from, const uint8_t* data, size_t size)
{
  Reader r(data, size);
  Header h;
  if (!readHeader(r, h))
  {
    m_stats.badPackets++;
    return;
  }
  m_stats.packetsReceived++;
  if (m_role == HOST)
  {
    if (h.type == MSG_HELLO)
    {
      onHello(from, data + HEADER_SIZE, size - HEADER_SIZE);
      return;
    }
    hostHandle(from, h, r);
  }
  else
  {
    peerHandle(from, h, r);
  }
}

// -- host: receiving ---------------------------------------------------------------------------

void LockstepSession::onHello(const vnet_addr& from, const uint8_t* body, size_t size)
{
  uint16_t version = 0;
  if (!decodeHelloVersion(body, size, version))
  {
    m_stats.badPackets++;
    return;
  }
  if (version != PROTOCOL_VERSION)
  {
    sendReject(from, 0, REJECT_VERSION,
               "protocol version " + std::to_string(version) + " here is " + std::to_string(PROTOCOL_VERSION)
               + ": use the same game version as the host");
    return;
  }
  HelloMsg h;
  Reader r(body, size);
  if (!decode(r, h))
  {
    m_stats.badPackets++;
    return;
  }

  if (h.buildId != m_opt.buildId)
  {
    sendReject(from, h.nonce, REJECT_BUILD,
               "different game build (host " + hex64(m_opt.buildId) + ", you " + hex64(h.buildId)
               + "): use the same version as the host");
    return;
  }
  if (h.dataHash != m_opt.dataHash)
  {
    sendReject(from, h.nonce, REJECT_DATA,
               "different game data files (host " + hex64(m_opt.dataHash) + ", you " + hex64(h.dataHash) + ")");
    return;
  }
  // A repeat of a HELLO we already answered: say the same again.
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (m.present && vnet_addr_equal(&m.addr, &from))
    {
      if (m.nonce == h.nonce && !m.gone)
      {
        m.lastHeard = m_now;
        WelcomeMsg w;
        w.nonce = m.nonce;
        w.peerId = (uint8_t) id;
        w.token = m.token;
        w.slotMask = 0;
        for (int s = 0; s < m_numSlots; s++)
        {
          if (m_owner[s] == id) w.slotMask |= (uint8_t) (1u << s);
        }
        sendMsg(MSG_WELCOME, from, w);
        return;
      }
      if (m_state == RUNNING)
      {
        sendReject(from, h.nonce, REJECT_IN_PROGRESS, "a round is in progress");
        return;
      }
      // The same address with a new nonce: that machine restarted. Replace the old entry.
      m = Member();
      std::memset(&m.addr, 0, sizeof(m.addr));
      recomputeRoster();
      break;
    }
  }

  if (h.localPlayers < 1 || h.localPlayers > MAX_SLOTS)
  {
    sendReject(from, h.nonce, REJECT_BAD_REQUEST, "asked for " + std::to_string((int) h.localPlayers) + " players");
    return;
  }
  if (m_state == RUNNING)
  {
    sendReject(from, h.nonce, REJECT_IN_PROGRESS, "a round is in progress; try again between rounds");
    return;
  }
  int humans = 0;
  int freeId = -1;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && !m_members[id].gone)
    {
      humans += m_members[id].localPlayers;
    }
    else if (id > 0 && freeId < 0 && !m_members[id].present)
    {
      freeId = id;
    }
  }
  if (freeId < 0 || humans + h.localPlayers > MAX_SLOTS)
  {
    sendReject(from, h.nonce, REJECT_FULL, "the session is full (" + std::to_string(MAX_SLOTS) + " players at most)");
    return;
  }

  Member& m = m_members[freeId];
  m = Member();
  m.present = true;
  m.addr = from;
  m.name = h.name.empty() ? ("player " + std::to_string(freeId + 1)) : h.name;
  m.token = newToken();
  m.nonce = h.nonce;
  m.localPlayers = h.localPlayers;
  m.lastHeard = m_now;
  m.ackedTicks = 0xFFFFFFFFu;   // it has played nothing: nothing to wait for between rounds
  recomputeRoster();

  WelcomeMsg w;
  w.nonce = m.nonce;
  w.peerId = (uint8_t) freeId;
  w.token = m.token;
  for (int s = 0; s < m_numSlots; s++)
  {
    if (m_owner[s] == freeId) w.slotMask |= (uint8_t) (1u << s);
  }
  sendMsg(MSG_WELCOME, from, w);
  m_lobbyDirty = true;
}

void LockstepSession::hostHandle(const vnet_addr& from, const Header& h, Reader& r)
{
  // Who is this? The address and the id it claims must agree, and the session must be ours.
  int id = -1;
  for (int i = 1; i < MAX_PEERS; i++)
  {
    if (m_members[i].present && vnet_addr_equal(&m_members[i].addr, &from))
    {
      id = i;
      break;
    }
  }
  if (id < 0)
  {
    for (size_t i = 0; i < m_removed.size();)
    {
      Removed& r = m_removed[i];
      if (m_now >= r.until)
      {
        m_removed.erase(m_removed.begin() + (long) i);
        continue;
      }
      if (vnet_addr_equal(&r.addr, &from) && h.session == m_sessionId)
      {
        if (m_now - r.lastSent >= 1000)
        {
          LeaveMsg l;
          l.reason = LEAVE_REMOVED;
          sendMsg(MSG_LEAVE, from, l);
          r.lastSent = m_now;
        }
        return;
      }
      i++;
    }
  }
  if (id < 0 || h.sender != id || h.session != m_sessionId)
  {
    m_stats.badPackets++;
    return;
  }
  Member& m = m_members[id];
  if (m.gone)
  {
    // It was dropped (silent too long, or it left) but is still talking: tell it, at most once a second.
    if (m_now - m.lastSent >= 1000)
    {
      LeaveMsg l;
      l.reason = LEAVE_KICKED;
      sendMsg(MSG_LEAVE, from, l);
      m.lastSent = m_now;
    }
    return;
  }
  const uint64_t heardBefore = m.lastHeard;
  m.lastHeard = m_now;   // taken back below if the packet does not decode: garbage is not a sign of life
  struct Undo
  {
    Member& m; uint64_t before; const Stats& st; uint64_t bad0;
    ~Undo() { if (st.badPackets != bad0) m.lastHeard = before; }
  } undo{m, heardBefore, m_stats, m_stats.badPackets};

  switch (h.type)
  {
    case MSG_INPUT:
    {
      InputMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onInput(m, msg);
      break;
    }
    case MSG_KEEPALIVE:
    {
      KeepaliveMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      if ((m_state == RUNNING || m_state == BETWEEN_ROUNDS) && msg.round == m_round)
      {
        m.startAcked = true;
        m.ackedTicks = std::max(m.ackedTicks, std::min(msg.ack, m_confirmed));
      }
      break;
    }
    case MSG_CHECKSUM:
    {
      ChecksumMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onChecksumHost(m, msg);
      break;
    }
    case MSG_READY:
    {
      ReadyMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onReady(m, msg);
      break;
    }
    case MSG_PONG:
    {
      PingMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      const uint32_t rtt = lowMs(m_now) - msg.sentMs;
      if (rtt < 60000)
      {
        m.rtt.push_back(rtt);
        while (m.rtt.size() > RTT_WINDOW) m.rtt.pop_front();
      }
      break;
    }
    case MSG_LEAVE:
    {
      LeaveMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      removePeer((uint8_t) id, "left");
      break;
    }
    default:
      m_stats.badPackets++;
      break;
  }
}

void LockstepSession::onInput(Member& m, const InputMsg& msg)
{
  if ((m_state != RUNNING && m_state != BETWEEN_ROUNDS) || msg.round != m_round)
  {
    return;
  }
  m.startAcked = true;
  m.ackedTicks = std::max(m.ackedTicks, std::min(msg.ack, m_confirmed));
  if (m_state != RUNNING)
  {
    return;
  }
  for (size_t k = 0; k < msg.records.size(); k++)
  {
    const uint32_t t = msg.first + (uint32_t) k;
    // A record for tick t exists only after its peer stepped t - D, which needs the host's
    // t - D, so a tick far past what is confirmed is garbage (and would only fill memory).
    if (t <= m.contig || t > m_confirmed + (uint32_t) m_delay + 1 || m.recs.count(t))
    {
      continue;
    }
    m.recs[t] = msg.records[k];
  }
  while (m.recs.count(m.contig + 1))
  {
    m.contig++;
  }
}

void LockstepSession::onReady(Member& m, const ReadyMsg& msg)
{
  // Only the lobby asks for it, only for the round the next START will be, and only the newest answer counts
  // (a late duplicate of an older one must not undo a newer one).
  if (m_state != LOBBY || msg.round != static_cast<uint16_t>(m_round + 1) || msg.seq <= m.readySeq)
  {
    return;
  }
  m.readySeq = msg.seq;
  if (m.ready != msg.ready)
  {
    m.ready = msg.ready;
    recomputeRoster();   // the view (and so the next LOBBY) shows it
  }
}

void LockstepSession::onChecksumHost(Member& m, const ChecksumMsg& msg)
{
  // Also between rounds: a peer's report for the last tick can arrive after the host has
  // stepped it and moved on to the scoreboard.
  if ((m_state != RUNNING && m_state != BETWEEN_ROUNDS) || msg.round != m_round)
  {
    return;
  }
  int id = 0;
  for (int i = 1; i < MAX_PEERS; i++)
  {
    if (&m_members[i] == &m) id = i;
  }
  for (const ChecksumEntry& e : msg.entries)
  {
    m.hashes[e.tick] = e.hash;
  }
  while (m.hashes.size() > MAX_HASHES)
  {
    m.hashes.erase(m.hashes.begin());
  }
  for (const ChecksumEntry& e : msg.entries)
  {
    compareChecksum((uint8_t) id, e.tick);
  }
}

void LockstepSession::compareChecksum(uint8_t peerId, uint32_t tick)
{
  if (m_desynced)
  {
    return;
  }
  auto mine = m_hostHashes.find(tick);
  auto theirs = m_members[peerId].hashes.find(tick);
  if (mine == m_hostHashes.end() || theirs == m_members[peerId].hashes.end())
  {
    return;
  }
  if (mine->second != theirs->second)
  {
    DesyncInfo d;
    d.round = m_round;
    d.tick = tick;
    d.hostHash = mine->second;
    d.peerHash = theirs->second;
    d.offender = peerId;
    declareDesync(d);
  }
}

void LockstepSession::declareDesync(const DesyncInfo& d)
{
  m_desynced = true;
  m_desync = d;
  m_lastDesyncSent = 0;   // hostSend() sends it now and repeats it
}

void LockstepSession::reportChecksum(uint32_t tick, uint64_t hash)
{
  if (m_state != RUNNING)
  {
    return;
  }
  if (m_role == HOST)
  {
    m_lastHostHashTick = std::max(m_lastHostHashTick, tick);
    m_hostHashes[tick] = hash;
    while (m_hostHashes.size() > MAX_HASHES)
    {
      m_hostHashes.erase(m_hostHashes.begin());
    }
    for (int id = 1; id < MAX_PEERS; id++)
    {
      if (m_members[id].present && !m_members[id].gone)
      {
        compareChecksum((uint8_t) id, tick);
      }
    }
  }
  else
  {
    ChecksumEntry e;
    e.tick = tick;
    e.hash = hash;
    m_myChecksums.push_back(e);
    while (m_myChecksums.size() > MAX_CHECKSUMS)
    {
      m_myChecksums.pop_front();
    }
    m_checksumDirty = true;
  }
}

// -- host: the roster, the lobby and removing peers ---------------------------------------------------

int LockstepSession::peerCount() const
{
  int n = 0;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && !m_members[id].gone) n++;
  }
  return n;
}

void LockstepSession::setLobbySettings(const LobbySettings& s)
{
  m_settings = s;
  if (m_role == HOST && m_state != RUNNING)
  {
    recomputeRoster();
    m_lobbyDirty = true;
  }
}

void LockstepSession::recomputeRoster()
{
  if (m_state == RUNNING)
  {
    rebuildLobbyView();   // slot ownership changes by takeovers while a round runs
    return;
  }
  int humans = 0;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && !m_members[id].gone) humans += m_members[id].localPlayers;
  }
  int total = std::max(2, std::max(humans, m_settings.totalSlots));
  total = std::min(total, MAX_SLOTS);
  m_numSlots = total;
  m_owner.fill(NO_PEER);
  int next = 0;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone) continue;
    for (int k = 0; k < m.localPlayers && next < MAX_SLOTS; k++)
    {
      m_owner[next++] = (uint8_t) id;
    }
  }
  rebuildLobbyView();
}

// The team of a slot: the host's choice, or the slot's own number (free for all). A choice outside 0..slots-1 (it was made
// when there were more slots) is not a team this round has: the MatchConfig would refuse the round.
uint8_t LockstepSession::teamOf(int slot) const
{
  if (slot < (int) m_settings.teams.size() && m_settings.teams[slot] >= 0 && m_settings.teams[slot] < m_numSlots)
  {
    return (uint8_t) m_settings.teams[slot];
  }
  return (uint8_t) slot;
}

void LockstepSession::rebuildLobbyView()
{
  m_lobbyDirty = true;
  m_lobby.phase = (uint8_t) (m_state == RUNNING ? PHASE_RUNNING : (m_state == BETWEEN_ROUNDS ? PHASE_BETWEEN_ROUNDS : PHASE_LOBBY));
  m_lobby.round = (uint16_t) (m_state == RUNNING ? m_round : m_round + 1);
  m_lobby.settings = m_settings;
  m_lobby.slots.clear();
  for (int s = 0; s < m_numSlots; s++)
  {
    SlotInfo si;
    si.owner = m_owner[s];
    si.team = teamOf(s);
    m_lobby.slots.push_back(si);
  }
  m_lobby.peers.clear();
  for (int id = 0; id < MAX_PEERS; id++)
  {
    const Member& m = m_members[id];
    if (!m.present) continue;
    LobbyEntry e;
    e.id = (uint8_t) id;
    e.name = m.name;
    e.connected = !m.gone;
    e.ready = id == 0 || m.ready;
    for (int s = 0; s < m_numSlots; s++)
    {
      if (m_owner[s] == id) e.slotMask |= (uint8_t) (1u << s);
    }
    if (!m.rtt.empty())
    {
      uint64_t sum = 0;
      const size_t n = std::min<size_t>(m.rtt.size(), 4);
      for (size_t i = m.rtt.size() - n; i < m.rtt.size(); i++) sum += m.rtt[i];
      e.rttMs = (int) (sum / n);
    }
    m_lobby.peers.push_back(e);
  }
}

void LockstepSession::removePeer(uint8_t id, const char* why)
{
  Member& m = m_members[id];
  if (!m.present)
  {
    return;
  }
  (void) why;
  if (m_state == RUNNING)
  {
    // Its slots go back to the AI on the next tick the host confirms; nobody waits for it.
    m.gone = true;
    m.recs.clear();
    for (int s = 0; s < m_numSlots; s++)
    {
      if (m_owner[s] == id)
      {
        m_hostEvents.push_back(ControlEvent(s, ControlEvent::RETURN_TO_AI));
      }
    }
    m_waitingMask &= (uint8_t) ~(1u << id);
    // The roster keeps the entry (greyed out in the lobby view) until the round ends.
    for (LobbyEntry& e : m_lobby.peers)
    {
      if (e.id == id) e.connected = false;
    }
  }
  else
  {
    m = Member();
    std::memset(&m.addr, 0, sizeof(m.addr));
    recomputeRoster();
  }
  m_lobbyDirty = true;
}

void LockstepSession::checkTimeouts()
{
  if (m_role == HOST)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      Member& m = m_members[id];
      if (m.present && !m.gone && m_now - m.lastHeard > m_opt.timeoutMs)
      {
        removePeer((uint8_t) id, "timed out");
      }
    }
    return;
  }
  if (m_state == CONNECTING)
  {
    if (m_now - m_connectStart > m_opt.connectTimeoutMs)
    {
      end(END_CONNECT_TIMEOUT, "the host did not answer");
    }
    return;
  }
  if (m_now - m_members[0].lastHeard > m_opt.timeoutMs)
  {
    end(END_HOST_LOST, "lost the connection to the host");
  }
}

// Peers on the host-side stay in the lobby view with their measured RTT: refresh it
// from the live RTT windows. (recomputeRoster() builds the whole view; this is the cheap
// refresh used before each LOBBY broadcast.)
void LockstepSession::broadcastLobby()
{
  recomputeRoster();
  LobbyMsg msg;
  msg.phase = m_lobby.phase;
  msg.round = m_lobby.round;
  msg.mapWidth = (uint8_t) m_settings.mapWidth;
  msg.mapHeight = (uint8_t) m_settings.mapHeight;
  msg.blockPercentage = (uint8_t) m_settings.blockPercentage;
  msg.npcSpawner = m_settings.npcSpawner;
  msg.slots = m_lobby.slots;
  for (const LobbyEntry& e : m_lobby.peers)
  {
    LobbyPeer p;
    p.id = e.id;
    p.slotMask = e.slotMask;
    p.rttMs = e.rttMs < 0 ? 0xFFFF : (uint16_t) std::min(e.rttMs, 0xFFFE);
    p.ready = e.ready;
    p.name = e.name;
    msg.peers.push_back(p);
  }
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (m.present && !m.gone)
    {
      sendMsg(MSG_LOBBY, m.addr, msg);
      m.lastSent = m_now;
    }
  }
  m_lastLobbySent = m_now;
  m_lobbyDirty = false;
}

// -- choosing D ---------------------------------------------------------------------------------------

int LockstepSession::chooseDelay(uint32_t worstRttMs, uint32_t jitterMs, uint32_t tickMs, int maxDelay)
{
  if (tickMs == 0) tickMs = 33;
  const uint32_t total = worstRttMs + jitterMs;
  const int d = (int) ((total + tickMs - 1) / tickMs) + 1;
  return std::max(MIN_DELAY, std::min(std::max(MIN_DELAY, maxDelay), d));
}

uint32_t LockstepSession::hostSilenceMs() const
{
  if (m_role == HOST || m_state == ENDED || m_state == CONNECTING)
  {
    return 0;
  }
  return m_now > m_members[0].lastHeard ? (uint32_t) std::min<uint64_t>(m_now - m_members[0].lastHeard, 0xFFFFFFFFu) : 0;
}

int LockstepSession::proposedDelay() const
{
  return computeDelay(nullptr);
}

int LockstepSession::computeDelay(std::string* note) const
{
  if (m_opt.fixedDelay > 0)
  {
    const int d = std::max(1, std::min(m_opt.fixedDelay, 16));
    if (note) *note = "D=" + std::to_string(d) + " (forced)";
    return d;
  }
  uint32_t worstTotal = 0;
  uint32_t worstRtt = 0;
  uint32_t worstJitter = 0;
  bool any = false;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    const Member& m = m_members[id];
    if (!m.present || m.gone || m.rtt.empty()) continue;
    any = true;
    uint32_t worst = 0;
    uint64_t sum = 0;
    for (uint32_t s : m.rtt)
    {
      worst = std::max(worst, s);
      sum += s;
    }
    const uint64_t mean = sum / m.rtt.size();
    uint64_t dev = 0;
    for (uint32_t s : m.rtt)
    {
      dev += s > mean ? s - mean : mean - s;
    }
    const uint32_t jitter = (uint32_t) (dev / m.rtt.size());
    if (worst + jitter >= worstTotal)
    {
      worstTotal = worst + jitter;
      worstRtt = worst;
      worstJitter = jitter;
    }
  }
  if (!any)
  {
    if (note) *note = "D=" + std::to_string(DEFAULT_DELAY) + " (nothing measured yet)";
    return DEFAULT_DELAY;
  }
  const int d = chooseDelay(worstTotal, 0, m_opt.tickMs, m_opt.maxDelay);
  if (note)
  {
    *note = "D=" + std::to_string(d) + " from a worst round trip of " + std::to_string(worstRtt) + " ms plus jitter "
          + std::to_string(worstJitter) + " ms at " + std::to_string(m_opt.tickMs) + " ms a tick";
  }
  return d;
}

int LockstepSession::peerRtt(uint8_t id) const
{
  if (id >= MAX_PEERS || m_members[id].rtt.empty()) return -1;
  return (int) m_members[id].rtt.back();
}

// -- rounds ----------------------------------------------------------------------------------------------

bool LockstepSession::canStartRound() const
{
  if (m_role != HOST || (m_state != LOBBY && m_state != BETWEEN_ROUNDS))
  {
    return false;
  }
  if (m_state == BETWEEN_ROUNDS)
  {
    if (m_desynced)
    {
      return false;
    }
    for (int id = 1; id < MAX_PEERS; id++)
    {
      const Member& m = m_members[id];
      if (m.present && !m.gone && m.ackedTicks < m_finalTick)
      {
        return false;
      }
      // Every peer that played the round has also reported the hash of its last checkpoint (the
      // round's last tick): until the host has compared it, a desync in the final ticks could race
      // the next START. (A machine that joined after the round has played nothing.)
      if (m.present && !m.gone && m.ackedTicks != 0xFFFFFFFFu && m_lastHostHashTick != 0
          && m.hashes.count(m_lastHostHashTick) == 0)
      {
        return false;
      }
    }
  }
  if (m_state == LOBBY && m_opt.requireReady && notReadyCount() > 0)
  {
    return false;
  }
  return true;
}

std::string LockstepSession::cannotStartBecause() const
{
  if (canStartRound())
  {
    return "";
  }
  if (m_role != HOST)
  {
    return "only the host starts a round";
  }
  if (m_state == LOBBY && m_opt.requireReady && notReadyCount() > 0)
  {
    const int waiting = notReadyCount();
    return "waiting for " + std::to_string(waiting) + (waiting == 1 ? " player" : " players") + " to be ready";
  }
  if (m_state == RUNNING)
  {
    return "a round is running";
  }
  if (m_desynced)
  {
    return "the last round ended out of sync";
  }
  return "waiting for the other players to finish the last round";
}

int LockstepSession::notReadyCount() const
{
  int n = 0;
  if (m_role == HOST)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      const Member& m = m_members[id];
      if (m.present && !m.gone && !m.ready) n++;
    }
    return n;
  }
  for (const LobbyEntry& e : m_lobby.peers)
  {
    if (e.id != HOST_ID && e.connected && !e.ready) n++;
  }
  return n;
}

void LockstepSession::setReady(bool ready)
{
  // Only in a lobby that is asking: not while connecting, in a round, on the scoreboard, or when this machine joined
  // between rounds (the host's lobby is not open then; it asks again when it goes back to the lobby).
  if (m_role != PEER || m_state != LOBBY || m_lobby.phase != PHASE_LOBBY || m_wantReady == ready)
  {
    return;
  }
  m_wantReady = ready;
  m_readySeq++;
  m_lastReadySent = 0;   // sent by the next pump(), and repeated until the host's LOBBY shows it
}

void LockstepSession::sendReady()
{
  bool hostSees = false;
  for (const LobbyEntry& e : m_lobby.peers)
  {
    if (e.id == m_myId) hostSees = e.ready;
  }
  if (m_lobby.round == 0 || (hostSees == m_wantReady && m_lastReadySent != 0))
  {
    return;   // nothing to say, or the host already shows what we want
  }
  if (m_lastReadySent != 0 && m_now - m_lastReadySent < m_opt.resendMs * 3)
  {
    return;
  }
  if (m_readySeq == 0)
  {
    return;   // never asked: the host's default (not ready) stands
  }
  ReadyMsg r;
  r.round = m_lobby.round;
  r.seq = m_readySeq;
  r.ready = m_wantReady;
  sendMsg(MSG_READY, m_hostAddr, r);
  m_members[0].lastSent = m_now;
  m_lastReadySent = m_now;
}

bool LockstepSession::kick(uint8_t peerId)
{
  if (m_role != HOST || m_state != LOBBY || peerId == HOST_ID || peerId >= MAX_PEERS || !m_members[peerId].present
      || m_members[peerId].gone)
  {
    return false;
  }
  LeaveMsg l;
  l.reason = LEAVE_REMOVED;
  for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m_members[peerId].addr, l);
  // The three LEAVEs above may all be lost: for a while a packet from this address is answered with the same reason.
  Removed r;
  r.addr = m_members[peerId].addr;
  r.until = m_now + REMOVED_MEMORY_MS;
  r.lastSent = m_now;
  m_removed.push_back(r);
  removePeer(peerId, "removed by the host");
  return true;
}

bool LockstepSession::returnToLobby()
{
  if (m_role != HOST || m_state != BETWEEN_ROUNDS || !canStartRound())
  {
    return false;
  }
  // Every ready flag is already clear: setupRound() cleared them when the round started, and onReady() ignores
  // answers outside the lobby, so nobody can have said ready since.
  m_state = LOBBY;
  recomputeRoster();
  return true;
}

bool LockstepSession::buildStart(uint64_t seed, StartMsg& out, std::string* error)
{
  out = StartMsg();
  out.round = (uint16_t) (m_round + 1);
  out.seed = seed;
  out.mapWidth = (uint8_t) std::max(0, std::min(255, m_settings.mapWidth));
  out.mapHeight = (uint8_t) std::max(0, std::min(255, m_settings.mapHeight));
  out.blockPercentage = (uint8_t) std::max(0, std::min(255, m_settings.blockPercentage));
  out.npcSpawner = m_settings.npcSpawner;
  out.delay = (uint8_t) computeDelay(&m_delayNote);
  for (int s = 0; s < m_numSlots; s++)
  {
    StartMsg::Slot sl;
    sl.owner = m_owner[s];
    sl.team = teamOf(s);
    sl.ai = m_owner[s] == NO_PEER;
    out.slots.push_back(sl);
  }
  MatchConfig c;
  c.seed = seed;
  c.mapWidth = out.mapWidth;
  c.mapHeight = out.mapHeight;
  c.blockPercentage = out.blockPercentage;
  c.npcSpawnerEnabled = out.npcSpawner;
  for (const StartMsg::Slot& sl : out.slots)
  {
    MatchConfig::Slot ms;
    ms.team = sl.team;
    ms.ai = sl.ai;
    c.slots.push_back(ms);
  }
  const std::string problem = c.problem();
  if (!problem.empty())
  {
    if (error) *error = "cannot start: " + problem;
    return false;
  }
  return true;
}

bool LockstepSession::startRound(uint64_t seed, std::string* error)
{
  if (m_role != HOST)
  {
    if (error) *error = "only the host starts a round";
    return false;
  }
  if (m_state != LOBBY && m_state != BETWEEN_ROUNDS)
  {
    if (error) *error = "a round is already running";
    return false;
  }
  if (!canStartRound())
  {
    if (error) *error = cannotStartBecause();
    return false;
  }
  recomputeRoster();
  StartMsg s;
  if (!buildStart(seed, s, error))
  {
    return false;
  }
  // Humans who left in the previous round are gone for good.
  for (int id = 1; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && m_members[id].gone)
    {
      m_members[id] = Member();
      std::memset(&m_members[id].addr, 0, sizeof(m_members[id].addr));
    }
  }
  setupRound(s, 0);
  return true;
}

void LockstepSession::setupRound(const StartMsg& s, uint8_t /*unused*/)
{
  m_round = s.round;
  m_delay = std::max(1, (int) s.delay);
  m_numSlots = (int) s.slots.size();
  m_owner.fill(NO_PEER);
  uint8_t localMask = 0;
  for (int i = 0; i < m_numSlots; i++)
  {
    m_owner[i] = s.slots[i].owner;
    if (s.slots[i].owner == m_myId) localMask |= (uint8_t) (1u << i);
  }

  m_roundStart = RoundStart();
  m_roundStart.config.seed = s.seed;
  m_roundStart.config.mapWidth = s.mapWidth;
  m_roundStart.config.mapHeight = s.mapHeight;
  m_roundStart.config.blockPercentage = s.blockPercentage;
  m_roundStart.config.npcSpawnerEnabled = s.npcSpawner;
  for (const StartMsg::Slot& sl : s.slots)
  {
    MatchConfig::Slot ms;
    ms.team = sl.team;
    ms.ai = sl.ai;
    m_roundStart.config.slots.push_back(ms);
  }
  m_roundStart.delay = m_delay;
  m_roundStart.round = m_round;
  m_roundStart.localSlotMask = localMask;
  m_roundStart.owner = m_owner;
  m_roundStartPending = true;

  m_ownersAt.clear();
  m_roundStartOwners = m_owner;
  m_ticks.clear();
  for (int t = 1; t <= m_delay; t++)
  {
    m_ticks[(uint32_t) t] = emptyTick();
  }
  m_confirmed = (uint32_t) m_delay;
  m_nextStep = 1;
  m_nextLocal = (uint32_t) m_delay + 1;
  m_finalTick = 0;
  m_myRecs.clear();
  m_ackOfMine = (uint32_t) m_delay;
  m_lastAckSent = 0;
  m_mySentUpTo = (uint32_t) m_delay;
  m_lastInputSent = 0;
  m_waitingMask = 0;
  m_lastTicksHeard = m_now;
  m_hostEvents.clear();
  m_claims.clear();
  m_rejectedClaims.clear();
  m_ownersSeen = m_owner;
  m_ownersAsOf = 0;
  m_hostHashes.clear();
  m_lastHostHashTick = 0;
  m_myChecksums.clear();
  m_checksumDirty = false;
  m_desynced = false;
  m_desync = DesyncInfo();
  m_haveQueuedStart = false;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    m.startAcked = false;
    m.contig = (uint32_t) m_delay;
    m.recs.clear();
    m.ackedTicks = (uint32_t) m_delay;
    m.sentUpTo = (uint32_t) m_delay;
    m.lastTicksSent = 0;
    m.lastStartSent = 0;
    m.hashes.clear();
    m.ready = false;
  }
  m_wantReady = false;
  m_state = RUNNING;
  m_lobbyDirty = true;
  if (m_role == HOST)
  {
    rebuildLobbyView();
  }
}

bool LockstepSession::takeRoundStart(RoundStart& out)
{
  if (!m_roundStartPending)
  {
    return false;
  }
  out = m_roundStart;
  m_roundStartPending = false;
  return true;
}

// -- confirming ticks (host) -------------------------------------------------------------------------------

bool LockstepSession::canStep() const
{
  return m_state == RUNNING && !m_desynced && m_ticks.count(m_nextStep) != 0;
}

const TickInputs& LockstepSession::stepInputs() const
{
  return m_ticks.at(m_nextStep);
}

void LockstepSession::submitLocal(const TickInputs& sample, uint8_t sampledMask)
{
  if (m_state != RUNNING)
  {
    return;
  }
  InputRecord rec;
  rec.mask = (uint8_t) (sampledMask & slotBits(m_numSlots));
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if (rec.mask & (1u << i))
    {
      rec.bytes[i] = sample.slot[i].bits;
    }
  }
  for (const ControlEvent& e : sample.control)
  {
    if (rec.events.size() < 4 * (size_t) MAX_SLOTS && validEvent(e, m_numSlots))
    {
      rec.events.push_back(e);
      if (e.kind == ControlEvent::HUMAN_TAKES_OVER)
      {
        m_claims[m_nextLocal].push_back(e.slot);   // the answer is in that tick's final inputs
      }
    }
  }
  m_myRecs[m_nextLocal++] = rec;
}

void LockstepSession::tickStepped()
{
  if (m_state != RUNNING)
  {
    return;
  }
  // The tick just stepped may answer a takeover request this machine made D ticks ago.
  auto claim = m_claims.find(m_nextStep);
  if (claim != m_claims.end())
  {
    auto fin = m_ticks.find(m_nextStep);
    for (int slot : claim->second)
    {
      // Granted if the final tick carries the takeover AND the slot is now ours: another machine's
      // takeover of the same bot, in the same tick, puts the same event in the same tick.
      bool event = false;
      if (fin != m_ticks.end())
      {
        for (const ControlEvent& e : fin->second.control)
        {
          if (e.slot == slot && e.kind == ControlEvent::HUMAN_TAKES_OVER) event = true;
        }
      }
      const uint8_t owner = m_role == HOST ? m_owner[slot] : m_ownersSeen[slot];
      if (!event || owner != m_myId) m_rejectedClaims.push_back(slot);
    }
  }
  while (!m_claims.empty() && m_claims.begin()->first <= m_nextStep)
  {
    m_claims.erase(m_claims.begin());
  }
  m_nextStep++;
  if (m_role == HOST)
  {
    trimHistory();
  }
  else
  {
    while (!m_ticks.empty() && m_ticks.begin()->first < m_nextStep)
    {
      m_ticks.erase(m_ticks.begin());
    }
  }
}

void LockstepSession::trimHistory()
{
  uint32_t keepFrom = m_nextStep - 1;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    const Member& m = m_members[id];
    if (m.present && !m.gone)
    {
      keepFrom = std::min(keepFrom, m.ackedTicks);
    }
  }
  while (!m_ticks.empty() && m_ticks.begin()->first <= keepFrom)
  {
    m_ticks.erase(m_ticks.begin());
  }
  // Keep one owners entry at or below the oldest kept tick (the "as of" lookup finds it).
  while (m_ownersAt.size() > 1 && std::next(m_ownersAt.begin())->first <= keepFrom)
  {
    m_ownersAt.erase(m_ownersAt.begin());
  }
}

std::vector<int> LockstepSession::takeRejectedClaims()
{
  std::vector<int> out;
  out.swap(m_rejectedClaims);
  return out;
}

uint32_t LockstepSession::unsteppedTicks() const
{
  return m_confirmed >= m_nextStep - 1 ? m_confirmed - (m_nextStep - 1) : 0;
}

int LockstepSession::catchUpTicks(uint32_t unsteppedConfirmed, int delay)
{
  return unsteppedConfirmed > (uint32_t) delay + 2 ? 2 : 0;
}

int LockstepSession::extraTicksWanted() const
{
  return (m_state == RUNNING && canStep()) ? catchUpTicks(unsteppedTicks(), m_delay) : 0;
}

uint8_t LockstepSession::waitingMask() const
{
  return m_waitingMask;
}

std::string LockstepSession::waitingText() const
{
  if (m_state != RUNNING || canStep())
  {
    return "";
  }
  std::string text;
  if (m_waitingMask != 0)
  {
    for (int id = 0; id < MAX_PEERS; id++)
    {
      if (!(m_waitingMask & (1u << id))) continue;
      std::string name = "player " + std::to_string(id + 1);
      for (const LobbyEntry& e : m_lobby.peers)
      {
        if (e.id == id) name = e.name;
      }
      text += (text.empty() ? "" : ", ") + name;
    }
  }
  else if (m_role == PEER)
  {
    text = "host";
  }
  else
  {
    return "";
  }
  return "waiting for " + text;
}

bool LockstepSession::mergeTick(uint32_t t, TickInputs& out)
{
  out = TickInputs();
  // Members in id order: the host first. For each: its control requests first (a takeover
  // makes the slot its own before the slot's bytes are read), then the bytes of the slots
  // it owns.
  for (int id = 0; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone) continue;
    const InputRecord* rec = nullptr;
    if (t > (uint32_t) m_delay)
    {
      if (id == 0)
      {
        auto it = m_myRecs.find(t);
        if (it != m_myRecs.end()) rec = &it->second;
      }
      else
      {
        auto it = m.recs.find(t);
        if (it != m.recs.end()) rec = &it->second;
      }
    }
    if (rec == nullptr) continue;
    // At most one event per slot per machine per tick (the first), and at most MAX_PEER_EVENTS in
    // all, so that with the host's own leaver events a tick never exceeds the protocol's and the
    // record's limit (4 per slot); the excess is dropped, the same way on the one machine that decides.
    uint32_t slotsUsed = 0;
    // Of several events for one slot in a record the LAST is the machine's intent (take, then give back
    // in one tick, means it gave it back); the earlier ones are dropped.
    int lastFor[MAX_SLOTS];
    for (int i = 0; i < MAX_SLOTS; i++) lastFor[i] = -1;
    for (size_t i = 0; i < rec->events.size(); i++)
    {
      if (validEvent(rec->events[i], m_numSlots)) lastFor[rec->events[i].slot] = (int) i;
    }
    for (size_t ei = 0; ei < rec->events.size(); ei++)
    {
      const ControlEvent& e = rec->events[ei];
      if (!validEvent(e, m_numSlots)) continue;
      const int s = e.slot;
      if (lastFor[s] != (int) ei) continue;
      if (slotsUsed & (1u << s)) continue;
      if (out.control.size() >= MAX_PEER_EVENTS) break;
      if (e.kind == ControlEvent::HUMAN_TAKES_OVER)
      {
        if (m_owner[s] != NO_PEER) continue;   // taken already, or a human's own slot
        m_owner[s] = (uint8_t) id;
        slotsUsed |= 1u << s;
        out.control.push_back(e);
      }
      else
      {
        if (m_owner[s] != id) continue;        // only the owner gives a slot back
        m_owner[s] = NO_PEER;
        slotsUsed |= 1u << s;
        out.control.push_back(e);
      }
    }
    for (int s = 0; s < m_numSlots; s++)
    {
      if ((rec->mask & (1u << s)) && m_owner[s] == id)
      {
        out.slot[s] = PlayerInput(rec->bytes[s]);
      }
    }
  }
  // Slots of peers that left: back to the AI, on the first tick confirmed after they went.
  for (const ControlEvent& e : m_hostEvents)
  {
    m_owner[e.slot] = NO_PEER;
    out.control.push_back(e);
  }
  m_hostEvents.clear();
  return true;
}

void LockstepSession::confirmTicks()
{
  if (m_state != RUNNING || m_desynced)
  {
    return;
  }
  for (;;)
  {
    const uint32_t t = m_confirmed + 1;
    uint8_t waiting = 0;
    if (t > (uint32_t) m_delay)
    {
      if (m_myRecs.count(t) == 0)
      {
        m_waitingMask = 0;   // the host itself has not stepped far enough: nobody else to blame
        return;
      }
      for (int id = 1; id < MAX_PEERS; id++)
      {
        const Member& m = m_members[id];
        if (m.present && !m.gone && m.contig < t)
        {
          waiting |= (uint8_t) (1u << id);
        }
      }
      if (waiting != 0)
      {
        m_waitingMask = waiting;
        return;
      }
    }
    TickInputs merged;
    mergeTick(t, merged);
    m_ticks[t] = merged;
    m_ownersAt[t] = m_owner;
    m_confirmed = t;
    m_myRecs.erase(m_myRecs.begin(), m_myRecs.upper_bound(t));
    for (int id = 1; id < MAX_PEERS; id++)
    {
      Member& m = m_members[id];
      m.recs.erase(m.recs.begin(), m.recs.upper_bound(t));
    }
    m_waitingMask = 0;
  }
}

void LockstepSession::roundFinished()
{
  if (m_state != RUNNING)
  {
    return;
  }
  m_finalTick = m_nextStep - 1;
  m_state = BETWEEN_ROUNDS;
  m_lobbyDirty = true;
  if (m_role == HOST)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      if (m_members[id].present && m_members[id].gone)
      {
        m_members[id] = Member();
        std::memset(&m_members[id].addr, 0, sizeof(m_members[id].addr));
      }
    }
    recomputeRoster();
  }
  else if (m_haveQueuedStart)
  {
    const StartMsg queued = m_queuedStart;
    m_haveQueuedStart = false;
    setupRound(queued, 0);
    sendInputToHost(true);
  }
}

// -- host: sending --------------------------------------------------------------------------------------------------

void LockstepSession::sendTicksTo(Member& m, bool force)
{
  if (!m.present || m.gone)
  {
    return;
  }
  uint32_t from = m.ackedTicks + 1;
  if (!m_opt.redundancy)
  {
    from = std::max(from, m.sentUpTo + 1);
  }
  const bool haveNew = from <= m_confirmed;
  if (!force)
  {
    if (!haveNew)
    {
      return;
    }
    const bool fresh = m_confirmed > m.sentUpTo;
    if (!fresh && m_now - m.lastTicksSent < m_opt.resendMs)
    {
      return;
    }
  }
  TicksMsg msg;
  msg.round = m_round;
  msg.ack = m.contig;
  msg.first = haveNew ? from : m_confirmed + 1;
  msg.waitingMask = m_waitingMask;
  size_t used = TICKS_FIXED_SIZE;
  if (haveNew)
  {
    for (uint32_t t = from; t <= m_confirmed; t++)
    {
      auto it = m_ticks.find(t);
      if (it == m_ticks.end())
      {
        break;
      }
      const size_t sz = encodedSize(it->second);
      if (used + sz > MAX_PACKET || msg.ticks.size() >= MAX_TICKS_PER_PACKET)
      {
        break;
      }
      msg.ticks.push_back(it->second);
      used += sz;
    }
  }
  if (!msg.ticks.empty())
  {
    m.sentUpTo = std::max(m.sentUpTo, msg.first + (uint32_t) msg.ticks.size() - 1);
  }
  // The owners are those after the last tick actually in this packet (the round's own, even after
  // the host has dealt the next round's slots), not after the newest confirmed tick.
  {
    const uint32_t last = msg.ticks.empty() ? msg.first - 1 : msg.first + (uint32_t) msg.ticks.size() - 1;
    auto at = m_ownersAt.upper_bound(last);
    const std::array<uint8_t, MAX_SLOTS>& o = at == m_ownersAt.begin() ? m_roundStartOwners : std::prev(at)->second;
    for (int i = 0; i < MAX_SLOTS; i++) msg.owners[i] = o[i];
  }
  sendMsg(MSG_TICKS, m.addr, msg);
  m.lastTicksSent = m_now;
  m.lastSent = m_now;
}

void LockstepSession::sendStartTo(Member& m)
{
  StartMsg s;
  s.round = m_round;
  s.seed = m_roundStart.config.seed;
  s.mapWidth = (uint8_t) m_roundStart.config.mapWidth;
  s.mapHeight = (uint8_t) m_roundStart.config.mapHeight;
  s.blockPercentage = (uint8_t) m_roundStart.config.blockPercentage;
  s.npcSpawner = m_roundStart.config.npcSpawnerEnabled;
  s.delay = (uint8_t) m_delay;
  for (int i = 0; i < m_numSlots; i++)
  {
    StartMsg::Slot sl;
    sl.owner = m_roundStart.owner[i];
    sl.team = (uint8_t) m_roundStart.config.slots[i].team;
    sl.ai = m_roundStart.config.slots[i].ai;
    s.slots.push_back(sl);
  }
  sendMsg(MSG_START, m.addr, s);
  m.lastStartSent = m_now;
  m.lastSent = m_now;
}

void LockstepSession::sendPings()
{
  const uint32_t interval = m_state == RUNNING ? 1000 : m_opt.pingMs;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone || m_now - m.lastPing < interval)
    {
      continue;
    }
    PingMsg p;
    p.seq = ++m.pingSeq;
    p.sentMs = lowMs(m_now);
    sendMsg(MSG_PING, m.addr, p);
    m.lastPing = m_now;
    m.lastSent = m_now;
  }
}

void LockstepSession::hostSend()
{
  if (m_desynced && (m_state == RUNNING || m_state == BETWEEN_ROUNDS) && m_now - m_lastDesyncSent >= DESYNC_RESEND_MS)
  {
    DesyncMsg d;
    d.round = m_desync.round;
    d.tick = m_desync.tick;
    d.hostHash = m_desync.hostHash;
    d.peerHash = m_desync.peerHash;
    d.offender = m_desync.offender;
    for (int id = 1; id < MAX_PEERS; id++)
    {
      Member& m = m_members[id];
      if (m.present && !m.gone)
      {
        sendMsg(MSG_DESYNC, m.addr, d);
        m.lastSent = m_now;
      }
    }
    m_lastDesyncSent = m_now;
  }

  const bool running = m_state == RUNNING;
  const bool between = m_state == BETWEEN_ROUNDS;
  if (running)
  {
    trimHistory();
  }
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone)
    {
      continue;
    }
    if (running && !m.startAcked && m_now - m.lastStartSent >= m_opt.resendMs * 2)
    {
      sendStartTo(m);
    }
    if (running)
    {
      sendTicksTo(m, false);
      if (m_now - m.lastSent >= m_opt.keepaliveMs)
      {
        sendTicksTo(m, true);   // an empty TICKS: ack, who we wait on, and a sign of life
      }
    }
    else if (between)
    {
      if (m.ackedTicks < m_finalTick)
      {
        sendTicksTo(m, false);
      }
      if (m_now - m.lastSent >= m_opt.keepaliveMs)
      {
        KeepaliveMsg k;
        k.round = m_round;
        k.ack = m.contig;
        sendMsg(MSG_KEEPALIVE, m.addr, k);
        m.lastSent = m_now;
      }
    }
  }

  if (m_state == LOBBY || m_state == BETWEEN_ROUNDS || (running && m_now - m_lastLobbySent >= 2 * m_opt.lobbyMs))
  {
    if (m_lobbyDirty || m_now - m_lastLobbySent >= m_opt.lobbyMs)
    {
      broadcastLobby();
    }
  }
  if (m_state == LOBBY)
  {
    // Keepalives in the lobby are the LOBBY broadcasts; a peer silent in the lobby is
    // dropped on its own timeout.
    for (int id = 1; id < MAX_PEERS; id++)
    {
      Member& m = m_members[id];
      if (m.present && !m.gone && m_now - m.lastSent >= m_opt.keepaliveMs)
      {
        KeepaliveMsg k;
        sendMsg(MSG_KEEPALIVE, m.addr, k);
        m.lastSent = m_now;
      }
    }
  }
  sendPings();
}

// -- the peer ------------------------------------------------------------------------------------------------------------

void LockstepSession::peerHandle(const vnet_addr& from, const Header& h, Reader& r)
{
  if (!vnet_addr_equal(&from, &m_hostAddr))
  {
    m_stats.badPackets++;
    return;
  }
  if (m_state == CONNECTING)
  {
    if (h.type == MSG_WELCOME)
    {
      WelcomeMsg w;
      if (!decode(r, w) || w.nonce != m_myNonce)
      {
        m_stats.badPackets++;
        return;
      }
      m_sessionId = h.session;
      m_myId = w.peerId;
      m_state = LOBBY;
      m_members[0].lastHeard = m_now;
      m_lobbyDirty = true;
      return;
    }
    if (h.type == MSG_REJECT)
    {
      RejectMsg rj;
      if (!decode(r, rj) || (rj.nonce != 0 && rj.nonce != m_myNonce))
      {
        m_stats.badPackets++;
        return;
      }
      m_reject.reason = rj.reason;
      m_reject.hostProtocol = rj.hostProtocol;
      m_reject.hostBuildId = rj.hostBuildId;
      m_reject.hostDataHash = rj.hostDataHash;
      m_reject.text = rj.text;
      end(END_REJECTED, "the host refused us: " + rj.text);
      return;
    }
    m_stats.badPackets++;
    return;
  }
  if (h.session != m_sessionId || h.sender != HOST_ID)
  {
    m_stats.badPackets++;
    return;
  }
  const uint64_t hostHeardBefore = m_members[0].lastHeard;
  m_members[0].lastHeard = m_now;
  struct Undo
  {
    Member& m; uint64_t before; const Stats& st; uint64_t bad0;
    ~Undo() { if (st.badPackets != bad0) m.lastHeard = before; }
  } undo{m_members[0], hostHeardBefore, m_stats, m_stats.badPackets};
  switch (h.type)
  {
    case MSG_LOBBY:
    {
      LobbyMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onLobby(msg);
      break;
    }
    case MSG_START:
    {
      StartMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onStart(msg);
      break;
    }
    case MSG_TICKS:
    {
      TicksMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onTicks(msg);
      break;
    }
    case MSG_DESYNC:
    {
      DesyncMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onDesync(msg);
      break;
    }
    case MSG_PING:
    {
      PingMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      sendMsg(MSG_PONG, m_hostAddr, msg);
      break;
    }
    case MSG_KEEPALIVE:
    {
      KeepaliveMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      if ((m_state == RUNNING || m_state == BETWEEN_ROUNDS) && msg.round == m_round)
      {
        m_ackOfMine = std::max(m_ackOfMine, msg.ack);
      }
      break;
    }
    case MSG_LEAVE:
    {
      LeaveMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      if (msg.reason == LEAVE_KICKED)
      {
        end(END_KICKED, "the host dropped you (no packets from you for too long)");
      }
      else if (msg.reason == LEAVE_REMOVED)
      {
        end(END_REMOVED, "the host removed you from the game");
      }
      else if (msg.reason == LEAVE_DESYNC)
      {
        end(END_HOST_CLOSED, "the host stopped the match: the game state diverged");
      }
      else
      {
        end(END_HOST_CLOSED, "the host ended the session");
      }
      break;
    }
    default:
      m_stats.badPackets++;
      break;
  }
}

void LockstepSession::onLobby(const LobbyMsg& msg)
{
  m_lobby.phase = msg.phase;
  m_lobby.round = msg.round;
  m_lobby.settings.mapWidth = msg.mapWidth;
  m_lobby.settings.mapHeight = msg.mapHeight;
  m_lobby.settings.blockPercentage = msg.blockPercentage;
  m_lobby.settings.npcSpawner = msg.npcSpawner;
  m_lobby.slots = msg.slots;
  m_lobby.peers.clear();
  for (const LobbyPeer& p : msg.peers)
  {
    LobbyEntry e;
    e.id = p.id;
    e.name = p.name;
    e.slotMask = p.slotMask;
    e.rttMs = p.rttMs == 0xFFFF ? -1 : p.rttMs;
    e.ready = p.ready;
    m_lobby.peers.push_back(e);
  }
  // The host went back to the lobby (the scoreboard's "Back to lobby"): so do we, once our own round is over
  // here. The round number says it is the lobby after the round we played, not an old packet of an earlier one.
  if (m_state == BETWEEN_ROUNDS && msg.phase == PHASE_LOBBY && msg.round == static_cast<uint16_t>(m_round + 1))
  {
    m_state = LOBBY;
    m_wantReady = false;
  }
}

void LockstepSession::onStart(const StartMsg& msg)
{
  if (m_state == RUNNING && msg.round == m_round)
  {
    sendInputToHost(true);   // the host has not seen our first INPUT yet
    return;
  }
  if (msg.round <= m_round || (m_haveQueuedStart && msg.round == m_queuedStart.round))
  {
    if (m_state == BETWEEN_ROUNDS && msg.round == m_round)
    {
      sendInputToHost(true);
    }
    return;
  }
  // Is this a round we can play? Check it as strictly as the host did.
  MatchConfig c;
  c.seed = msg.seed;
  c.mapWidth = msg.mapWidth;
  c.mapHeight = msg.mapHeight;
  c.blockPercentage = msg.blockPercentage;
  c.npcSpawnerEnabled = msg.npcSpawner;
  for (const StartMsg::Slot& sl : msg.slots)
  {
    MatchConfig::Slot ms;
    ms.team = sl.team;
    ms.ai = sl.ai;
    c.slots.push_back(ms);
  }
  const std::string problem = c.problem();
  if (!problem.empty() || msg.delay < 1 || msg.delay > 16)
  {
    end(END_BAD_HOST, "the host sent a match this game cannot play: " + (problem.empty() ? std::string("bad input delay") : problem));
    return;
  }
  if (m_state == RUNNING)
  {
    m_queuedStart = msg;   // applied when the round in progress is finished here
    m_haveQueuedStart = true;
    return;
  }
  setupRound(msg, 0);
  sendInputToHost(true);
}

void LockstepSession::onTicks(const TicksMsg& msg)
{
  if ((m_state != RUNNING && m_state != BETWEEN_ROUNDS) || msg.round != m_round)
  {
    return;
  }
  m_lastTicksHeard = m_now;
  m_waitingMask = msg.waitingMask;
  const uint32_t asOf = msg.first + (uint32_t) msg.ticks.size();   // the owners are those after the packet's newest tick
  if (asOf >= m_ownersAsOf)
  {
    m_ownersAsOf = asOf;
    for (int i = 0; i < MAX_SLOTS; i++)
    {
      m_ownersSeen[i] = msg.owners[i];
    }
  }
  m_ackOfMine = std::max(m_ackOfMine, msg.ack);
  while (!m_myRecs.empty() && m_myRecs.begin()->first <= m_ackOfMine)
  {
    m_myRecs.erase(m_myRecs.begin());
  }
  for (const TickInputs& t : msg.ticks)
  {
    for (const ControlEvent& e : t.control)
    {
      if (!validEvent(e, m_numSlots))
      {
        m_stats.badPackets++;   // the host would never send this: refuse the whole packet
        return;
      }
    }
  }
  for (size_t k = 0; k < msg.ticks.size(); k++)
  {
    const uint32_t tick = msg.first + (uint32_t) k;
    if (tick < m_nextStep || m_ticks.count(tick))
    {
      continue;
    }
    TickInputs t = msg.ticks[k];
    for (int s = m_numSlots; s < MAX_SLOTS; s++)
    {
      t.slot[s] = PlayerInput();   // the simulation ignores them, a record refuses them
    }
    m_ticks[tick] = t;
  }
  while (m_ticks.count(m_confirmed + 1))
  {
    m_confirmed++;
  }
}

void LockstepSession::onDesync(const DesyncMsg& msg)
{
  if ((m_state != RUNNING && m_state != BETWEEN_ROUNDS) || msg.round != m_round || m_desynced)
  {
    return;
  }
  m_desynced = true;
  m_desync.round = msg.round;
  m_desync.tick = msg.tick;
  m_desync.hostHash = msg.hostHash;
  m_desync.peerHash = msg.peerHash;
  m_desync.offender = msg.offender;
}

void LockstepSession::sendInputToHost(bool force)
{
  if (m_state != RUNNING && m_state != BETWEEN_ROUNDS)
  {
    return;
  }
  uint32_t from = m_ackOfMine + 1;
  if (!m_opt.redundancy)
  {
    from = std::max(from, m_mySentUpTo + 1);
  }
  InputMsg msg;
  msg.round = m_round;
  msg.ack = m_confirmed;
  msg.first = from;
  size_t used = INPUT_FIXED_SIZE;
  for (uint32_t t = from; t < m_nextLocal; t++)
  {
    auto it = m_myRecs.find(t);
    if (it == m_myRecs.end())
    {
      break;
    }
    const size_t sz = encodedSize(it->second);
    if (used + sz > MAX_PACKET || msg.records.size() >= MAX_TICKS_PER_PACKET)
    {
      break;
    }
    msg.records.push_back(it->second);
    used += sz;
  }
  if (msg.records.empty() && !force)
  {
    return;
  }
  if (!msg.records.empty())
  {
    m_mySentUpTo = std::max(m_mySentUpTo, from + (uint32_t) msg.records.size() - 1);
  }
  sendMsg(MSG_INPUT, m_hostAddr, msg);
  m_lastInputSent = m_now;
  m_lastAckSent = m_confirmed;
  m_members[0].lastSent = m_now;
}

void LockstepSession::sendChecksums(bool force)
{
  if (m_myChecksums.empty() || (m_state != RUNNING && m_state != BETWEEN_ROUNDS))
  {
    return;   // between rounds too: the last tick's hash is reported as the round ends
  }
  if (!force && m_now - m_lastChecksumSent < CHECKSUM_RESEND_MS)
  {
    return;
  }
  ChecksumMsg msg;
  msg.round = m_round;
  msg.entries.assign(m_myChecksums.begin(), m_myChecksums.end());
  sendMsg(MSG_CHECKSUM, m_hostAddr, msg);
  m_lastChecksumSent = m_now;
  m_members[0].lastSent = m_now;
  m_checksumDirty = false;
}

void LockstepSession::peerSend()
{
  if (m_state == CONNECTING)
  {
    if (m_now - m_lastHelloSent >= m_opt.helloMs)
    {
      HelloMsg h;
      h.buildId = m_opt.buildId;
      h.dataHash = m_opt.dataHash;
      h.localPlayers = (uint8_t) m_opt.localPlayers;
      h.nonce = m_myNonce;
      h.name = m_opt.name;
      Writer w;
      Header hd;
      hd.type = MSG_HELLO;
      writeHeader(w, hd);
      encode(w, h);
      sendRaw(m_hostAddr, w);
      m_lastHelloSent = m_now;
    }
    return;
  }
  if (m_state == RUNNING || m_state == BETWEEN_ROUNDS)
  {
    const bool unacked = m_state == RUNNING && m_nextLocal - 1 > m_ackOfMine;
    const bool fresh = m_state == RUNNING && m_nextLocal - 1 > m_mySentUpTo;
    const bool ackAdvanced = m_confirmed != m_lastAckSent;
    if (fresh || ackAdvanced || (unacked && m_now - m_lastInputSent >= m_opt.resendMs))
    {
      sendInputToHost(true);
    }
    else if (m_now - m_members[0].lastSent >= m_opt.keepaliveMs)
    {
      if (m_state == RUNNING)
      {
        sendInputToHost(true);   // an empty INPUT is the running keepalive
      }
      else
      {
        KeepaliveMsg k;
        k.round = m_round;
        k.ack = m_confirmed;
        sendMsg(MSG_KEEPALIVE, m_hostAddr, k);
        m_members[0].lastSent = m_now;
      }
    }
    sendChecksums(m_checksumDirty);
    return;
  }
  // LOBBY
  sendReady();
  if (m_now - m_members[0].lastSent >= m_opt.keepaliveMs)
  {
    KeepaliveMsg k;
    sendMsg(MSG_KEEPALIVE, m_hostAddr, k);
    m_members[0].lastSent = m_now;
  }
}

void LockstepSession::flushOutgoing(uint32_t maxMs)
{
  // Datagrams held back by simulated latency are released by vnet_poll() once their time has
  // come, so give them that long (bounded) before the context is destroyed. Backends that cannot
  // wait (loopback, the browser) return at once.
  if (m_v == nullptr || vnet_wait(m_v, 0) < 0)
  {
    return;
  }
  // Only datagrams accepted by vnet_sendto() and not yet on the wire (held by simulated latency) need it.
  auto held = [this]() {
    vnet_stats st;
    vnet_get_stats(m_v, &st);
    return (int64_t) st.tx_datagrams - (int64_t) st.wire_datagrams - (int64_t) st.impair_lost - (int64_t) st.impair_overflow;
  };
  const uint64_t until = vnet_time(m_v) + maxMs;
  while (held() > 0 && vnet_time(m_v) < until)
  {
    vnet_poll(m_v);
    vnet_wait(m_v, 5);
  }
  vnet_poll(m_v);
}

// -- leaving ---------------------------------------------------------------------------------------------------------------------

void LockstepSession::leave(LeaveReason reason)
{
  if (m_state == ENDED)
  {
    return;
  }
  LeaveMsg l;
  l.reason = reason;
  if (m_role == HOST)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      if (m_members[id].present)
      {
        for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m_members[id].addr, l);
      }
    }
  }
  else if (m_state != CONNECTING)
  {
    for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m_hostAddr, l);
  }
  end(reason == LEAVE_DESYNC ? END_DESYNC : END_LEFT, reason == LEAVE_DESYNC ? "stopped after a desync" : "you left");
}
} // namespace netplay
