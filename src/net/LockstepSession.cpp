#include "LockstepSession.h"

#include <algorithm>
#include <cassert>
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
// A reconnect token from another address is honoured when its old owner has been silent this long, or (a live duplicate) never:
// the HELLO is held back for TOKEN_GRACE_MS and then taken for a new player.
const uint64_t TOKEN_QUIET_MS = 3000;   // above brief 12's sparse-pump bound (a hidden browser tab pumps about once a second; 12 tolerates 1.5 s)
const uint64_t TOKEN_GRACE_MS = 4000;   // must stay above TOKEN_QUIET_MS: a crashed machine's token is honoured before the HELLO is taken for a new player
// Streaming the record to a late joiner (brief 13). A window of packets is sent whenever the joiner's acknowledgement moves
// (so the speed is a window per round trip), and the first two again if it has not moved for CATCHUP_RESEND_MS.
const size_t CATCHUP_WINDOW_PACKETS = 8;
const uint32_t CATCHUP_WINDOW_TICKS = 1000;   // (a datagram holds about 150 ticks: 7 datagrams of the 8 at most)
const uint32_t CATCHUP_RESEND_MS = 100;
const uint32_t CATCHUP_NEAR_TICKS = 150;     // closer to the head than this and the ordinary TICKS repeat does the rest
const uint32_t CATCHUP_INFO_MS = 250;        // WELCOME_MIDMATCH repeats (the head moves while it catches up)


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
  const HelloMsg h = s->makeHello();
  Writer w;
  Header hd;
  hd.type = MSG_HELLO;
  writeHeader(w, hd);
  encode(w, h);
  s->sendRaw(hostAddr, w);
  s->m_lastHelloSent = s->m_now;
  return s;
}

HelloMsg LockstepSession::makeHello() const
{
  HelloMsg h;
  h.buildId = m_opt.buildId;
  h.dataHash = m_opt.dataHash;
  h.localPlayers = (uint8_t) m_opt.localPlayers;
  h.nonce = m_myNonce;
  h.token = m_opt.reconnectToken;
  h.flags = m_opt.spectate ? HELLO_SPECTATE : 0;
  h.name = m_opt.name;
  return h;
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
  m_now = vnet_time(m_v);

  vnet_status_info st;
  if (vnet_status(m_v, &st) == VNET_STATUS_FAILED)
  {
    end(END_NETWORK, std::string("the network failed: ") + vnet_strerror(st.error));
    return;
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

void LockstepSession::sendWelcome(Member& m, const vnet_addr& to)
{
  const int id = (int) (&m - &m_members[0]);
  if (m.midJoin && m_state == RUNNING)
  {
    WelcomeMidMatchMsg w;
    w.nonce = m.nonce;
    w.peerId = (uint8_t) id;
    w.token = m.token;
    w.round = m_round;
    w.head = m_confirmed;
    w.offerMask = m.offerMask;
    sendMsg(MSG_WELCOME_MIDMATCH, to, w);
    m.lastInfoSent = m_now;
    return;
  }
  WelcomeMsg w;
  w.nonce = m.nonce;
  w.peerId = (uint8_t) id;
  w.token = m.token;
  w.slotMask = 0;
  for (int s = 0; s < m_numSlots; s++)
  {
    if (m_owner[s] == id) w.slotMask |= (uint8_t) (1u << s);
  }
  sendMsg(MSG_WELCOME, to, w);
}

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

  // A machine the host removed whose WELCOME was lost still repeats the HELLO it was removed under: tell it, whether or not an
  // entry of it is still around (in a round a removed member stays, gone). A new nonce (a restarted game) is not a repeat, and
  // gets in; there is no ban.
  for (const Removed& rm : m_removed)
  {
    if (m_now < rm.until && rm.nonce == h.nonce && vnet_addr_equal(&rm.addr, &from))
    {
      LeaveMsg l;
      l.reason = LEAVE_REMOVED;
      sendMsg(MSG_LEAVE, from, l);
      return;
    }
  }

  // Who is this? A reconnect token names the player whatever address it comes from (a browser's new context may
  // have another address while the old one is still registered); without one, the address does. But a token only
  // counts if the old incarnation has gone quiet: two live machines that share a token (two windows on one prefs file) must
  // not evict each other. A HELLO whose token belongs to a machine that is still talking from another address is held back
  // for TOKEN_GRACE_MS (a crashed machine falls silent in that time, and then the token is honoured); if the old one is
  // still talking after that, this HELLO is a new player and the token is ignored.
  int known = -1;
  bool byToken = false;
  if (h.token != 0)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      Member& o = m_members[id];
      if (!o.present || o.token != h.token) continue;
      const bool sameAddr = vnet_addr_equal(&o.addr, &from);
      const bool quiet = o.gone || sameAddr || m_now - o.lastHeard >= TOKEN_QUIET_MS;
      if (quiet)
      {
        known = id;
        byToken = true;
        break;
      }
      // Alive, from another address.
      size_t at = 0;
      while (at < m_pendingTokens.size() && !(m_pendingTokens[at].nonce == h.nonce && vnet_addr_equal(&m_pendingTokens[at].addr, &from))) at++;
      if (at == m_pendingTokens.size())
      {
        PendingToken pt;
        pt.addr = from;
        pt.nonce = h.nonce;
        pt.since = m_now;
        if (m_pendingTokens.size() >= 8) m_pendingTokens.erase(m_pendingTokens.begin());
        m_pendingTokens.push_back(pt);
        return;   // wait: it repeats the HELLO
      }
      if (m_now - m_pendingTokens[at].since < TOKEN_GRACE_MS)
      {
        return;
      }
      break;      // the old one is alive: a new player, the token means nothing here
    }
  }
  if (known < 0)
  {
    for (int id = 1; id < MAX_PEERS; id++)
    {
      if (m_members[id].present && vnet_addr_equal(&m_members[id].addr, &from))
      {
        known = id;
        break;
      }
    }
  }
  if (known >= 0)
  {
    Member& m = m_members[known];
    if (m.nonce == h.nonce && vnet_addr_equal(&m.addr, &from) && !m.gone)
    {
      // A repeat of a HELLO we already answered: say the same again.
      m.lastHeard = m_now;
      sendWelcome(m, from);
      return;
    }
  }

  if (h.localPlayers < 1 || h.localPlayers > MAX_SLOTS)
  {
    sendReject(from, h.nonce, REJECT_BAD_REQUEST, "asked for " + std::to_string((int) h.localPlayers) + " players");
    return;
  }
  const bool spectate = (h.flags & HELLO_SPECTATE) != 0;

  if (m_state == RUNNING)
  {
    if (!m_opt.allowHotJoin)
    {
      sendReject(from, h.nonce, REJECT_IN_PROGRESS, "a round is in progress; try again between rounds");
      return;
    }
    if (m_desynced || !m_historyComplete)
    {
      sendReject(from, h.nonce, REJECT_IN_PROGRESS, m_desynced ? "the round has stopped (out of sync); try again between rounds"
                                                               : "the round is too long to join; try again between rounds");
      return;
    }
    int id = known;
    uint32_t token = 0;
    uint8_t offer = 0;
    if (known >= 0)
    {
      // The same player again: a reconnect (its token), or a restart on the same address. The old incarnation is gone from
      // now on: its slots go back to the AI on the next confirmed tick, and what it owned is offered back to the new one.
      if (!m_members[known].gone)
      {
        removePeer((uint8_t) known, "reconnected");
      }
      if (byToken)
      {
        token = m_members[known].token;
        offer = m_members[known].lostMask;
      }
    }
    else
    {
      // A free id; failing that, one that belonged to a machine that is gone (its token dies with it).
      for (int i = 1; i < MAX_PEERS && id < 0; i++)
      {
        if (!m_members[i].present) id = i;
      }
      for (int i = 1; i < MAX_PEERS && id < 0; i++)
      {
        if (m_members[i].gone) id = i;
      }
      if (id < 0)
      {
        sendReject(from, h.nonce, REJECT_FULL, "the session is full (" + std::to_string(MAX_PEERS) + " machines at most)");
        return;
      }
    }
    admitHotJoiner(id, from, h, offer, token);
    return;
  }

  // The lobby, or between two rounds.
  uint32_t keepToken = 0;
  int forced = -1;
  if (known >= 0)
  {
    // The same player again (restarted, or from another address): the old entry is replaced in place.
    keepToken = byToken ? m_members[known].token : 0;
    forced = known;
    m_members[known] = Member();
    std::memset(&m_members[known].addr, 0, sizeof(m_members[known].addr));
    recomputeRoster();
  }
  int humans = 0;
  int freeId = -1;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && !m_members[id].gone)
    {
      if (!m_members[id].spectator) humans += m_members[id].localPlayers;
    }
    else if (id > 0 && freeId < 0 && !m_members[id].present)
    {
      freeId = id;
    }
  }
  if (forced >= 0) freeId = forced;
  if (freeId < 0 || (!spectate && humans + h.localPlayers > MAX_SLOTS))
  {
    sendReject(from, h.nonce, REJECT_FULL, "the session is full (" + std::to_string(MAX_SLOTS) + " players at most)");
    return;
  }

  Member& m = m_members[freeId];
  m = Member();
  m.present = true;
  m.addr = from;
  m.name = h.name.empty() ? ("player " + std::to_string(freeId + 1)) : h.name;
  m.token = keepToken != 0 ? keepToken : newToken();
  m.nonce = h.nonce;
  m.localPlayers = h.localPlayers;
  m.spectator = spectate;
  m.spectatorByChoice = spectate;
  m.lastHeard = m_now;
  m.ackedTicks = 0xFFFFFFFFu;   // it has played nothing: nothing to wait for between rounds
  recomputeRoster();
  sendWelcome(m, from);
  m_lobbyDirty = true;
}

bool LockstepSession::admitHotJoiner(int id, const vnet_addr& from, const HelloMsg& h, uint8_t offer, uint32_t token)
{
  Member& m = m_members[id];
  m = Member();
  m.present = true;
  m.addr = from;
  m.name = h.name.empty() ? ("player " + std::to_string(id + 1)) : h.name;
  m.token = token != 0 ? token : newToken();
  m.nonce = h.nonce;
  m.localPlayers = h.localPlayers;
  m.lastHeard = m_now;
  m.spectator = (h.flags & HELLO_SPECTATE) != 0;
  m.spectatorByChoice = m.spectator;
  m.midJoin = true;
  m.offerMask = offer;
  // Ticks 1..D are empty on every machine; the record is streamed from there.
  m.contig = (uint32_t) m_delay;
  m.ackedTicks = (uint32_t) m_delay;
  m.sentUpTo = (uint32_t) m_delay;
  m_hotJoins++;
  recomputeRoster();   // while a round runs this only rebuilds the view
  sendWelcome(m, from);
  m_lobbyDirty = true;
  return true;
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
      l.reason = m.kickReason;
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
    case MSG_READY_AT:
    {
      ReadyAtMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onReadyAt(m, msg);
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
    if (t <= m.contig || t > m_confirmed + (uint32_t) m_delay + 1 || m.recs.count(t) || !m.waited(t))
    {
      continue;   // (a spectator's records, and a late joiner's before its takeover tick, are not wanted)
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
  Member& pm = m_members[peerId];
  if (mine->second != theirs->second)
  {
    DesyncInfo d;
    d.round = m_round;
    d.tick = tick;
    d.hostHash = mine->second;
    d.peerHash = theirs->second;
    d.offender = peerId;
    if (pm.midJoin || pm.spectator)
    {
      // A machine that joined mid-round (or only watches: its state decides nothing) and fast-forwarded to here diverged. The match is what it is: the host and the
      // players are fine, so only that machine is stopped (it is told, and writes its evidence), and the match goes on.
      evictDesynced(peerId, d);
      return;
    }
    declareDesync(d);
  }
  else if (pm.midJoin || pm.spectator)
  {
    pm.verified = true;   // its state agrees with the host's: it may be given a slot
  }
}

void LockstepSession::evictDesynced(int id, const DesyncInfo& d)
{
  Member& m = m_members[id];
  DesyncMsg msg;
  msg.round = d.round;
  msg.tick = d.tick;
  msg.hostHash = d.hostHash;
  msg.peerHash = d.peerHash;
  msg.offender = d.offender;
  for (int k = 0; k < 3; k++) sendMsg(MSG_DESYNC, m.addr, msg);
  m.kickReason = LEAVE_DESYNC;
  m.token = 0;
  removePeer((uint8_t) id, "its state differs from the host's");
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
  // Who watches is decided afresh with every roster: a machine that asked to (HELLO_SPECTATE) always does; one the slots ran out
  // for (late joiners are not held to the six-human cap) watches until the roster is dealt again, and is dealt a slot as soon as
  // there is one.
  for (int id = 1; id < MAX_PEERS; id++)
  {
    m_members[id].spectator = m_members[id].spectatorByChoice;
  }
  int humans = 0;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    if (m_members[id].present && !m_members[id].gone && !m_members[id].spectator) humans += m_members[id].localPlayers;
  }
  int total = std::max(2, std::max(humans, m_settings.totalSlots));
  total = std::min(total, MAX_SLOTS);
  m_numSlots = total;
  m_owner.fill(NO_PEER);
  int next = 0;
  for (int id = 0; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone || m.spectator) continue;
    for (int k = 0; k < m.localPlayers && next < MAX_SLOTS; k++)
    {
      m_owner[next++] = (uint8_t) id;
    }
  }
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& sm = m_members[id];
    if (!sm.present || sm.gone || sm.spectator) continue;
    bool any = false;
    for (int s = 0; s < m_numSlots; s++) any = any || m_owner[s] == id;
    if (!any) sm.spectator = true;
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
    e.ready = id == 0 || m.ready || m.spectator;   // a spectator is never asked
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
    m.lostMask = 0;
    for (int s = 0; s < m_numSlots; s++)
    {
      if (m_owner[s] == id)
      {
        m_hostEvents.push_back(ControlEvent(s, ControlEvent::RETURN_TO_AI));
        m.lostMask |= (uint8_t) (1u << s);   // what a reconnect with its token is offered back
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
        continue;
      }
      // A late joiner that says it is ready but whose checksums never agree or never arrive is not given a slot for good.
      if (m_state == RUNNING && m.present && !m.gone && m.midJoin && !m.spectator && m.readyAt && !m.verified && !m.takeoverDecided
          && m_now - m.readyAtSince > m_opt.timeoutMs)
      {
        LeaveMsg l;
        l.reason = LEAVE_KICKED;
        for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m.addr, l);
        removePeer((uint8_t) id, "never verified");
        continue;
      }
      // R3 (brief 07's review): between rounds, a peer that has every tick but never reports the last checkpoint's hash
      // holds up the next round for good. Past a bound it is dropped.
      if (m_state == BETWEEN_ROUNDS && m.present && !m.gone && !m.spectator && !(m.midJoin && m.takeoverMask == 0)
          && m_lastHostHashTick != 0 && !m_desynced
          && m.ackedTicks != 0xFFFFFFFFu && m.ackedTicks >= m_finalTick && m.hashes.count(m_lastHostHashTick) == 0
          && m_now - m_roundEndedAt > m_opt.finalHashWaitMs)
      {
        LeaveMsg l;
        l.reason = LEAVE_KICKED;
        for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m.addr, l);
        removePeer((uint8_t) id, "never reported the last checksum");
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

int LockstepSession::chooseDelay(uint32_t worstRttMs, uint32_t jitterMs, uint32_t tickMs)
{
  if (tickMs == 0) tickMs = 33;
  const uint32_t total = worstRttMs + jitterMs;
  const int d = (int) ((total + tickMs - 1) / tickMs) + 1;
  return std::max(MIN_DELAY, std::min(MAX_DELAY, d));
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
    if (!m.present || m.gone || m.spectator || m.rtt.empty()) continue;
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
  const int d = chooseDelay(worstTotal, 0, m_opt.tickMs);
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
      // A spectator, and a late joiner who is still catching up or has not been given a slot, are not waited for.
      if (m.spectator || (m.midJoin && m.takeoverMask == 0))
      {
        continue;
      }
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
      if (m.present && !m.gone && !m.spectator && !m.ready) n++;
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
  if (m_role != HOST || (m_state != LOBBY && m_state != RUNNING && m_state != BETWEEN_ROUNDS) || peerId == HOST_ID
      || peerId >= MAX_PEERS || !m_members[peerId].present || m_members[peerId].gone)
  {
    return false;
  }
  LeaveMsg l;
  l.reason = LEAVE_REMOVED;
  for (int k = 0; k < 3; k++) sendMsg(MSG_LEAVE, m_members[peerId].addr, l);
  // The three LEAVEs above may all be lost: for a while a packet from this address is answered with the same reason.
  Removed r;
  r.addr = m_members[peerId].addr;
  r.nonce = m_members[peerId].nonce;
  r.until = m_now + REMOVED_MEMORY_MS;
  r.lastSent = m_now;
  m_removed.push_back(r);
  m_members[peerId].kickReason = LEAVE_REMOVED;   // (during a round the entry stays, gone: a late packet gets this)
  removePeer(peerId, "removed by the host");
  if (m_members[peerId].present)
  {
    m_members[peerId].token = 0;   // a removed machine cannot come back with its token
  }
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
  setupRound(s, false);
  return true;
}

void LockstepSession::setupRound(const StartMsg& s, bool joining)
{
  m_round = s.round;
  m_delay = std::max(1, (int) s.delay);
  m_numSlots = (int) s.slots.size();
  m_owner.fill(NO_PEER);
  uint8_t localMask = 0;
  for (int i = 0; i < m_numSlots; i++)
  {
    m_owner[i] = s.slots[i].owner;
    // (A late joiner's slots come only from TAKEOVER_AT: a reconnect's old slot is in START's owners, but it is a bot by now.)
    if (s.slots[i].owner == m_myId && !joining) localMask |= (uint8_t) (1u << i);
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
  m_history.clear();
  m_historyComplete = true;
  m_roundEndedAt = 0;
  for (int t = 1; t <= m_delay; t++)
  {
    m_ticks[(uint32_t) t] = emptyTick();
    m_history.push_back(emptyTick());
  }
  // A machine that joins a round already running is not waited for until the host has given it a slot; one that only
  // watches never is. Everybody else produces records from the start.
  m_midJoin = joining;
  m_joinStage = joining ? JOIN_CATCHING_UP : JOIN_NONE;
  // No slot in START (and not a late joiner, whose slots come from TAKEOVER_AT): this machine watches the round, sends no records.
  m_participateFrom = (joining || m_opt.spectate || localMask == 0) ? 0xFFFFFFFFu : 0u;
  m_grantedMask = 0;
  m_grantedPending = 0;
  m_takeoverTick = 0;
  m_takeoverReason = 0;
  m_lastReadyAtSent = 0;
  m_firstReadyAtSent = 0;
  if (!joining)
  {
    m_headKnown = 0;
    m_offerMask = 0;
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
    m.midJoin = false;
    m.verified = false;
    m.readyAt = false;
    m.readyAtSince = 0;
    m.readyAtTick = 0;
    m.takeoverTick = 0;
    m.takeoverMask = 0;
    m.takeoverReason = 0;
    m.takeoverApplied = false;
    m.takeoverDecided = false;
    m.offerMask = 0;
    m.lostMask = 0;
    m.kickReason = LEAVE_KICKED;
    m.lastInfoSent = 0;
    m.lastCatchAck = 0;
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

// -- host: late joiners (brief 13) --------------------------------------------------------------------------

// Slots dealt to a late joiner whose takeover has not applied yet.
uint8_t LockstepSession::reservedSlots() const
{
  uint8_t mask = 0;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    const Member& m = m_members[id];
    if (m.present && !m.gone && m.midJoin && m.takeoverMask != 0 && !m.takeoverApplied) mask |= m.takeoverMask;
  }
  return mask;
}

// The bots nobody has been promised: what a late joiner can be given.
uint8_t LockstepSession::freeBotSlots() const
{
  uint8_t mask = 0;
  const uint8_t reserved = reservedSlots();
  for (int s = 0; s < m_numSlots; s++)
  {
    if (m_owner[s] == NO_PEER && !(reserved & (1u << s)) && (!m_slotFilter || m_slotFilter(s))) mask |= (uint8_t) (1u << s);
  }
  return mask;
}

// The input delay a machine's recent round trips need (the same formula as the round's own D, over the newest samples only:
// what it is like now that it has caught up, not while it was busy fast-forwarding).
uint32_t LockstepSession::delayNeededBy(const Member& m) const
{
  if (m.rtt.empty())
  {
    return 0;
  }
  uint32_t worst = 0;
  const size_t n = std::min<size_t>(m.rtt.size(), 3);
  for (size_t i = m.rtt.size() - n; i < m.rtt.size(); i++) worst = std::max(worst, m.rtt[i]);
  return (uint32_t) chooseDelay(worst, 0, m_opt.tickMs);
}

void LockstepSession::onReadyAt(Member& m, const ReadyAtMsg& msg)
{
  if (m_state != RUNNING || !m.midJoin || msg.round != m_round)
  {
    return;
  }
  if (!m.readyAt)
  {
    m.readyAt = true;
    m.readyAtSince = m_now;
  }
  m.readyAtTick = std::max(m.readyAtTick, msg.tick);
  if (!m.takeoverDecided)
  {
    scheduleTakeover(m);
  }
  if (m.takeoverDecided)
  {
    TakeoverAtMsg t;
    t.round = m_round;
    t.tick = m.takeoverTick;
    t.slotMask = m.takeoverMask;
    t.reason = m.takeoverReason;
    sendMsg(MSG_TAKEOVER_AT, m.addr, t);   // again for every READY_AT: the joiner repeats it until this arrives
    m.lastSent = m_now;
  }
}

// Gives a late joiner its slots: the bots it was offered back (a reconnect), then the lowest free ones, as many as it asked
// for. The takeover is a HUMAN_TAKES_OVER in the final inputs of a tick some way ahead, so it reaches every machine in the same
// tick like any other; the host starts waiting for the joiner's inputs from that tick. No free bot: it watches.
void LockstepSession::scheduleTakeover(Member& m)
{
  const int id = (int) (&m - &m_members[0]);
  const int rttNow = peerRtt((uint8_t) id);
  const uint32_t rttTicks = rttNow > 0 ? ((uint32_t) rttNow + m_opt.tickMs - 1) / std::max<uint32_t>(1, m_opt.tickMs) : 0;
  if (m.readyAtTick + (uint32_t) m_delay + 8 + rttTicks < m_confirmed)
  {
    return;   // not near the live tick: it could not make its first record in time. (It says READY_AT again.)
  }
  if (!m_hostEvents.empty())
  {
    return;   // an earlier incarnation's slots are still being handed back to the AI
  }
  if (!m.spectator && !m.verified)
  {
    return;   // not trusted yet: one of its checksums has to agree with the host's first
  }
  // Its round trip has to be one this round's input delay can carry, or the host would stall for it on every tick: a machine
  // that is too far away watches instead. (Two round trips measured since it caught up are needed, or two seconds.)
  if (!m.spectator && m.rtt.size() < 2 && m_now - m.readyAtSince < 2000)
  {
    return;
  }
  uint8_t chosen = 0;
  uint8_t reason = m.spectator ? TAKEOVER_SPECTATOR : TAKEOVER_NO_FREE_BOT;
  if (!m.spectator && delayNeededBy(m) > (uint32_t) m_delay)
  {
    reason = TAKEOVER_TOO_SLOW;
  }
  else if (!m.spectator)
  {
    const uint8_t free = freeBotSlots();
    int count = 0;
    for (int s = 0; s < m_numSlots && count < m.localPlayers; s++)
    {
      if ((m.offerMask & free) & (1u << s)) { chosen |= (uint8_t) (1u << s); count++; }
    }
    for (int s = 0; s < m_numSlots && count < m.localPlayers; s++)
    {
      if ((free & (1u << s)) && !(chosen & (1u << s))) { chosen |= (uint8_t) (1u << s); count++; }
    }
  }
  m.takeoverDecided = true;
  m.takeoverMask = chosen;
  m.takeoverReason = chosen != 0 ? TAKEOVER_GRANTED : reason;
  if (chosen != 0)
  {
    // After the newest confirmed tick, a round trip, and the input delay: by then the joiner is stepping near the head and
    // makes its record for that tick on time (and backfills with neutral inputs if the message was slow).
    m.takeoverTick = m_confirmed + (uint32_t) m_delay + 2 + rttTicks;
    m.contig = m.takeoverTick - 1;   // records before it are not wanted
    m.recs.clear();
  }
  else
  {
    m.takeoverTick = m_confirmed + 1;
  }
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
  if (m_nextLocal < m_participateFrom)
  {
    m_nextLocal++;   // a watcher, or a joiner not yet given a slot: no record (the host does not want one)
    // Nor can it claim a bot: the host never hears of it, so the local layer is told it was not granted.
    for (const ControlEvent& e : sample.control)
    {
      if (e.kind == ControlEvent::HUMAN_TAKES_OVER && validEvent(e, m_numSlots) && !(m_grantedMask & (1u << e.slot)))
      {
        m_rejectedClaims.push_back(e.slot);
      }
    }
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
    if (m.present && !m.gone && !m.midJoin)   // (a late joiner is served from the history)
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
  if (m_state != RUNNING || canStep() || catchingUp())
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
  // Scheduled takeovers are never refused or delayed, so they are counted out of the peers' share of a tick (18 + 6 leavers = 24
  // would be 30 with six more): the peers get what is left of MAX_PEER_EVENTS after them.
  size_t scheduledTotal = 0, scheduled = 0;
  for (int id = 1; id < MAX_PEERS; id++)
  {
    const Member& sm = m_members[id];
    if (sm.present && !sm.gone && sm.midJoin && sm.takeoverMask != 0 && !sm.takeoverApplied && sm.takeoverTick == t)
    {
      for (int s = 0; s < m_numSlots; s++) if (sm.takeoverMask & (1u << s)) scheduledTotal++;
    }
  }
  const uint8_t reserved = reservedSlots();   // dealt to a late joiner, whose takeover has not applied yet: nobody else may claim them
  for (int id = 0; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    if (!m.present || m.gone) continue;
    // A late joiner's takeover, scheduled by the host: it applies at this tick for everybody, before the joiner's own bytes.
    if (m.midJoin && m.takeoverMask != 0 && !m.takeoverApplied && m.takeoverTick == t)
    {
      for (int s = 0; s < m_numSlots; s++)
      {
        if (!(m.takeoverMask & (1u << s)) || m_owner[s] != NO_PEER) continue;
        m_owner[s] = (uint8_t) id;
        out.control.push_back(ControlEvent(s, ControlEvent::HUMAN_TAKES_OVER));
        scheduled++;
      }
      m.takeoverApplied = true;
    }
    const InputRecord* rec = nullptr;
    if (t > (uint32_t) m_delay && m.waited(t))
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
      if (out.control.size() - scheduled >= MAX_PEER_EVENTS - scheduledTotal) break;
      if (e.kind == ControlEvent::HUMAN_TAKES_OVER)
      {
        if (m_owner[s] != NO_PEER || (reserved & (1u << s))) continue;   // taken already, a human's own slot, or dealt to a joiner
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
  // A merged tick holds at most 4 * MAX_SLOTS = 24 events, the limit of the wire format and of MatchRecord. The peers' events
  // are capped at MAX_PEER_EVENTS (18) minus the scheduled takeovers of this tick, which are never refused and number at most six
  // (one per slot): peers + takeovers <= 18, and the host's own leaver events (one per slot) add at most 6. Leaving
  // `- scheduledTotal` out would still hold in every case a test builds (it takes an 18-event flood in the very tick of a
  // takeover to exceed 24), which is why that mutation survives: this assert is the guard for it.
  assert(out.control.size() <= (size_t) 4 * MAX_SLOTS);
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
        if (m.present && !m.gone && m.waited(t) && m.contig < t)
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
    if (m_history.size() + 1 == t && m_history.size() < m_opt.historyLimit)
    {
      m_history.push_back(merged);   // the record of the round, for a machine that joins later
    }
    else if (m_history.size() + 1 <= t)
    {
      m_historyComplete = false;
    }
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
  m_roundEndedAt = m_now;
  m_midJoin = false;
  m_joinStage = JOIN_NONE;
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
    setupRound(queued, false);
    sendInputToHost(true);
  }
}

// -- host: sending --------------------------------------------------------------------------------------------------

const TickInputs* LockstepSession::confirmedTick(uint32_t t) const
{
  if (t >= 1 && t <= m_history.size())
  {
    return &m_history[t - 1];
  }
  auto it = m_ticks.find(t);
  return it == m_ticks.end() ? nullptr : &it->second;
}

// One TICKS datagram for `m` starting at tick `from` (as many confirmed ticks as fit, none when `from` is past the
// newest). Returns the first tick it did not carry.

uint32_t LockstepSession::sendTickRange(Member& m, uint32_t from)
{
  TicksMsg msg;
  msg.round = m_round;
  msg.ack = m.contig;
  msg.first = from <= m_confirmed ? from : m_confirmed + 1;
  msg.waitingMask = m_waitingMask;
  size_t used = TICKS_FIXED_SIZE;
  for (uint32_t t = msg.first; t <= m_confirmed; t++)
  {
    const TickInputs* tick = confirmedTick(t);
    if (tick == nullptr)
    {
      break;
    }
    const size_t sz = encodedSize(*tick);
    if (used + sz > MAX_PACKET || msg.ticks.size() >= MAX_TICKS_PER_PACKET)
    {
      break;
    }
    msg.ticks.push_back(*tick);
    used += sz;
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
  return msg.first + (uint32_t) msg.ticks.size();
}

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
  sendTickRange(m, haveNew ? from : m_confirmed + 1);
}

// The record of the round so far, to a machine that joined late: a window of datagrams each time its acknowledgement moves
// (a window per round trip), the first two again when it has not moved for a while. Close to the head the ordinary TICKS
// repeat takes over.
void LockstepSession::sendCatchUp(Member& m)
{
  const uint32_t head = m_confirmed;
  const uint32_t acked = m.ackedTicks;
  if (head <= acked + CATCHUP_NEAR_TICKS)
  {
    sendTicksTo(m, false);
    return;
  }
  const bool advanced = acked != m.lastCatchAck;
  if (!advanced && m_now - m.lastTicksSent < CATCHUP_RESEND_MS)
  {
    return;
  }
  uint32_t from;
  size_t packets;
  if (advanced)
  {
    m.lastCatchAck = acked;
    from = std::max(acked + 1, m.sentUpTo + 1);
    packets = CATCHUP_WINDOW_PACKETS;
  }
  else
  {
    from = acked + 1;
    packets = 2;
  }
  const uint32_t limit = acked + CATCHUP_WINDOW_TICKS;
  for (size_t p = 0; p < packets && from <= head && from <= limit; p++)
  {
    from = sendTickRange(m, from);
  }
  if (m.sentUpTo > acked) m_stats.maxCatchUpInFlight = std::max<uint64_t>(m_stats.maxCatchUpInFlight, m.sentUpTo - acked);
  m.lastTicksSent = m_now;
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
  for (int id = 1; id < MAX_PEERS; id++)
  {
    Member& m = m_members[id];
    // A late joiner that is about to be given a slot is measured often: its round trip decides whether it can have one.
    const uint32_t interval = (m_state == RUNNING && !(m.midJoin && !m.takeoverDecided)) ? 1000 : m_opt.pingMs;
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
      if (m.midJoin)
      {
        sendCatchUp(m);
        // Where the round is now (the head moves while the joiner catches up); stops once it has said it is ready.
        if (!m.readyAt && m_now - m.lastInfoSent >= CATCHUP_INFO_MS)
        {
          sendWelcome(m, m.addr);
        }
      }
      else
      {
        sendTicksTo(m, false);
      }
      if (m_now - m.lastSent >= m_opt.keepaliveMs)
      {
        sendTicksTo(m, true);   // an empty TICKS: ack, who we wait on, and a sign of life
      }
    }
    else if (between)
    {
      if (m.ackedTicks < m_finalTick)
      {
        if (m.midJoin) sendCatchUp(m); else sendTicksTo(m, false);
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
      m_token = w.token;
      m_state = LOBBY;
      m_members[0].lastHeard = m_now;
      m_lobbyDirty = true;
      return;
    }
    if (h.type == MSG_WELCOME_MIDMATCH)
    {
      WelcomeMidMatchMsg w;
      if (!decode(r, w) || w.nonce != m_myNonce)
      {
        m_stats.badPackets++;
        return;
      }
      m_sessionId = h.session;
      m_myId = w.peerId;
      m_token = w.token;
      m_midJoin = true;               // setupRound(joining) when the START of that round arrives
      m_joinRound = w.round;
      m_headKnown = w.head;
      m_offerMask = w.offerMask;
      m_state = LOBBY;
      m_members[0].lastHeard = m_now;
      m_lobbyDirty = true;
      return;
    }
    if (h.type == MSG_LEAVE)
    {
      // The host removed us while we were still asking (our WELCOME was lost): it answers the repeated HELLO this way.
      LeaveMsg l;
      if (decode(r, l) && l.reason == LEAVE_REMOVED)
      {
        end(END_REMOVED, "the host removed you from the game");
        return;
      }
      m_stats.badPackets++;
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
    case MSG_WELCOME_MIDMATCH:
    {
      WelcomeMidMatchMsg msg;
      if (!decode(r, msg) || msg.nonce != m_myNonce) { m_stats.badPackets++; return; }
      onWelcomeMidMatch(msg);
      break;
    }
    case MSG_TAKEOVER_AT:
    {
      TakeoverAtMsg msg;
      if (!decode(r, msg)) { m_stats.badPackets++; return; }
      onTakeoverAt(msg);
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

void LockstepSession::onWelcomeMidMatch(const WelcomeMidMatchMsg& msg)
{
  // Later copies (the host repeats it while we catch up) carry the head, which moves.
  if (msg.round != m_joinRound && m_state != LOBBY)
  {
    return;
  }
  m_headKnown = std::max(m_headKnown, msg.head);
}

void LockstepSession::onTakeoverAt(const TakeoverAtMsg& msg)
{
  if (m_state != RUNNING || !m_midJoin || msg.round != m_round || m_joinStage == JOIN_DONE)
  {
    return;
  }
  if (m_firstReadyAtSent != 0 && m_now - m_firstReadyAtSent < m_opt.testDropTakeoverForMs)
  {
    return;
  }
  m_joinStage = JOIN_DONE;
  m_takeoverTick = msg.tick;
  m_grantedMask = (uint8_t) (msg.slotMask & slotBits(m_numSlots));
  m_grantedPending = m_grantedMask;
  m_takeoverReason = m_grantedMask != 0 ? TAKEOVER_GRANTED : (msg.reason != TAKEOVER_GRANTED ? msg.reason : TAKEOVER_NO_FREE_BOT);
  if (m_grantedMask == 0)
  {
    return;   // it watches
  }
  // From this tick on the host waits for our records. Those before it are not made; the ones we have already stepped past
  // (the message was slow) are neutral: nobody has pressed a key for slots we did not have.
  m_participateFrom = msg.tick;
  m_ackOfMine = std::max(m_ackOfMine, msg.tick - 1);
  m_mySentUpTo = std::max(m_mySentUpTo, msg.tick - 1);
  for (uint32_t t = msg.tick; t < m_nextLocal; t++)
  {
    InputRecord rec;
    rec.mask = m_grantedMask;
    m_myRecs[t] = rec;
  }
}

double LockstepSession::catchUpFraction() const
{
  if (m_joinStage != JOIN_CATCHING_UP)
  {
    return 1.0;
  }
  const uint32_t head = catchUpHead();
  if (head == 0)
  {
    return 0.0;
  }
  const uint32_t stepped = m_nextStep - 1;
  return stepped >= head ? 1.0 : (double) stepped / (double) head;
}

uint8_t LockstepSession::takeGrantedSlots()
{
  const uint8_t m = m_grantedPending;
  m_grantedPending = 0;
  return m;
}

bool LockstepSession::spectating() const
{
  return m_opt.spectate || (m_joinStage == JOIN_DONE && m_grantedMask == 0)
         || (m_state == RUNNING && m_joinStage == JOIN_NONE && m_participateFrom == 0xFFFFFFFFu);
}

void LockstepSession::sendReadyAt()
{
  if (m_lastReadyAtSent != 0 && m_now - m_lastReadyAtSent < m_opt.resendMs * 3)
  {
    return;
  }
  ReadyAtMsg r;
  r.round = m_round;
  r.tick = m_nextStep - 1 > m_opt.testReadyAtLag ? m_nextStep - 1 - m_opt.testReadyAtLag : 0;
  if (m_firstReadyAtSent == 0) m_firstReadyAtSent = m_now;
  sendMsg(MSG_READY_AT, m_hostAddr, r);
  m_members[0].lastSent = m_now;
  m_lastReadyAtSent = m_now;
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
  // A late joiner still catching up on the round (the host does not wait for it) follows too: the host stops sending that
  // round's ticks once it is back in the lobby, so it would be stuck in the round for good.
  if ((m_state == BETWEEN_ROUNDS || (m_state == RUNNING && (m_midJoin || m_opt.spectate || m_participateFrom == 0xFFFFFFFFu)))
      && msg.phase == PHASE_LOBBY && msg.round == static_cast<uint16_t>(m_round + 1))
  {
    m_state = LOBBY;
    m_wantReady = false;
    m_midJoin = false;
    m_joinStage = JOIN_NONE;
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
    if (supersedes())
    {
      // A machine that is still catching up on an older round, or only watches, is not waited for by the host: the new
      // round begins now, and the rest of the old one is dropped.
      setupRound(msg, false);
      sendInputToHost(true);
      return;
    }
    m_queuedStart = msg;   // applied when the round in progress is finished here
    m_haveQueuedStart = true;
    return;
  }
  // The START of the round we were told (WELCOME_MIDMATCH) is running: we join it, and catch up.
  const bool joining = m_midJoin && m_state == LOBBY && msg.round == m_joinRound;
  setupRound(msg, joining);
  sendInputToHost(true);
}

bool LockstepSession::supersedes() const
{
  return m_opt.spectate || m_participateFrom == 0xFFFFFFFFu || (m_midJoin && (m_joinStage != JOIN_DONE || m_grantedMask == 0));
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
  m_stats.inputRecordsSent += msg.records.size();
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
      const HelloMsg h = makeHello();
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
    if (m_state == RUNNING && m_joinStage == JOIN_CATCHING_UP && m_nextStep > m_confirmed && m_confirmed >= m_headKnown)
    {
      m_joinStage = JOIN_WAITING;   // stepped everything the host has confirmed, and that is the live tick
    }
    if (m_state == RUNNING && m_joinStage == JOIN_WAITING && !m_desynced)
    {
      sendReadyAt();
    }
    const bool producing = m_participateFrom != 0xFFFFFFFFu;
    const bool unacked = m_state == RUNNING && producing && m_nextLocal - 1 > m_ackOfMine;
    const bool fresh = m_state == RUNNING && producing && m_nextLocal - 1 > m_mySentUpTo;
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
