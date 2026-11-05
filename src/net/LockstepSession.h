/*
 * LockstepSession.h
 *
 * The network half of deterministic lockstep (brief 07, README D1/D2/D7/D8): one
 * object per machine that joins or hosts a session, finds out who plays what, and then
 * turns "my local input for this tick" into "everybody's final TickInputs for this
 * tick", in order and identical on every machine, over lossy unordered datagrams.
 *
 * It knows the wire messages (NetProtocol.h), vnet (the transport) and the plain value
 * types TickInputs and MatchConfig. It does NOT know SDL, the Simulation, the state
 * hash algorithm or any rendering: a hash is just a number handed in, a dump is the
 * caller's business. That is what lets the tests run N sessions in one process (the
 * simulation allows only one per process) on vnet's loopback network.
 *
 * ---------------------------------------------------------------------------
 * THE PROTOCOL IN ONE PAGE (the full text is doco/netplay/protocol.md)
 *
 * STAR TOPOLOGY. One session is the HOST (peer id 0). It alone decides each tick's
 * final TickInputs (merging every peer's slots and control events), the match
 * settings, the seed, the slot assignment, the input delay D and when a round starts.
 * Peers talk only to the host.
 *
 * TICK NUMBERS. Tick t is the t-th Simulation::step() of a round (t >= 1, the same
 * numbering as MatchRecord). Ticks 1..D need no input: they are empty on every
 * machine from the moment of START. A machine that is about to step tick T samples its
 * local devices and submits the sample for tick T + D (submitLocal). So a record for
 * tick t exists on a machine once it has stepped t - D.
 *
 * CONFIRMING. The host confirms tick t once it holds a record for t from itself and
 * from every peer it still waits for. The confirmed TickInputs are broadcast and are
 * the only thing any machine, the host included, ever steps. A machine can step tick
 * t only when it holds t (canStep()); otherwise it stalls. Because a machine produces a
 * record only by stepping, nobody is ever more than D ticks ahead of the slowest.
 *
 * LOSS (D7). Every INPUT (peer -> host) repeats ALL the records the host has not
 * acknowledged, every TICKS (host -> peer) all the confirmed ticks that peer has not
 * acknowledged, oldest first, up to one datagram. Each carries the ack of the other
 * direction. So any single datagram that gets through repairs every loss before it, and
 * the same code runs over reliable transports. Duplicates and reordering are harmless:
 * a record or tick already held is ignored.
 *
 * LIVENESS. pump() must be called every frame in EVERY state (lobby, running,
 * scoreboard): it polls vnet, answers, and sends keepalives. A machine silent for
 * SessionOptions::timeoutMs (10 s) is gone: a peer's slots go back to the AI (the
 * host schedules RETURN_TO_AI control events on the next tick it confirms), and a
 * host that falls silent ends the session on the peers (there is no host migration).
 *
 * DESYNC. Each machine reports its state hash at agreed ticks (reportChecksum); the host
 * compares each peer's with its own and, on a difference, tells everyone (DESYNC).
 * What to do about it (write dumps and records) is the caller's: desync() says it
 * happened and canStep() turns false.
 * ---------------------------------------------------------------------------
 */

#ifndef _LOCKSTEPSESSION_H
#define _LOCKSTEPSESSION_H

#include <array>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "MatchConfig.h"
#include "NetProtocol.h"
#include "NetRandom.h"
#include "TickInputs.h"
#include "vnet.h"

namespace netplay
{
  struct SessionOptions
  {
    // What this machine's simulation is (Simulation::buildId() / dataHash()); sent in
    // HELLO and compared by the host, which refuses a peer that differs.
    uint64_t buildId = 0;
    uint64_t dataHash = 0;
    std::string name = "player";
    // Player slots this machine wants (1..MAX_SLOTS).
    int localPlayers = 1;

    // Milliseconds of vnet_time().
    uint32_t tickMs = 33;          // one tick (GameSettings::NORMAL_TICKS_SPEED)
    uint32_t timeoutMs = 10000;    // silence after which a machine is gone
    uint32_t connectTimeoutMs = 10000;
    uint32_t keepaliveMs = 250;
    uint32_t lobbyMs = 500;        // LOBBY broadcast interval
    uint32_t resendMs = 40;        // repeat unacknowledged ticks after this long
    uint32_t pingMs = 250;         // PING interval (1000 once a round runs)
    uint32_t helloMs = 250;

    // Host only: force D instead of choosing it from the measured RTTs (0 = choose).
    int fixedDelay = 0;

    // TEST ONLY. false: every tick/record is put on the wire once, in the first packet
    // after it exists, and never repeated. That is what D7 exists to prevent: under loss
    // the session stalls for good. The tests turn it off to show they need it.
    bool redundancy = true;

    // Seed for session ids, nonces and tokens; 0 = from the system (NetRandom.h).
    uint64_t randomSeed = 0;

    // Host only: canStartRound() in the lobby also wants every peer to have said it is ready
    // (setReady(), READY on the wire; protocol version 2). Off by default so that the headless
    // runner and the older tests, which start a round as soon as everyone is connected, are
    // unchanged; the game turns it on. Between rounds nobody has to be ready again: the roster is
    // what played the last round, and the host may start the next one or go back to the lobby.
    bool requireReady = false;

    // -- hot-join, reconnect and spectators (brief 13, protocol version 3) --------------------------------
    // Host: let a HELLO in while a round is running (it catches up from the host's record and takes over a bot).
    // false: such a HELLO is answered with REJECT_IN_PROGRESS, as in versions 1 and 2.
    bool allowHotJoin = true;
    // Peer: watch only. The host never deals this machine a slot or waits for its inputs (HELLO_SPECTATE).
    bool spectate = false;
    // Peer: the reconnect token a WELCOME gave this machine in an earlier session with the same host (0 = none).
    // With it the host knows this is the same player (even from another address) and offers its old slots back.
    uint32_t reconnectToken = 0;
    // Host: how long after a round's end a peer that acknowledged every tick may leave the final tick's checksum
    // unreported before the host drops it (07's review, R3). Without a bound a peer that never reports it holds up
    // the next round for good.
    uint32_t finalHashWaitMs = 8000;
  };

  // What the host's lobby will build the next round from.
  struct LobbySettings
  {
    int mapWidth = 17;
    int mapHeight = 13;
    int blockPercentage = 55;
    bool npcSpawner = true;
    // Total player slots; 0 = as many as there are humans (at least 2). The slots
    // beyond the humans are bots, which a human can take over mid-round.
    int totalSlots = 0;
    // Team of each slot; a missing entry means the slot's own number (free for all).
    std::vector<int> teams;
  };

  // What a started round is, handed to the game / headless runner once per round.
  struct RoundStart
  {
    MatchConfig config;          // seed included
    int delay = 3;
    uint16_t round = 0;
    uint8_t localSlotMask = 0;   // the slots this machine's players drive
    std::array<uint8_t, MAX_SLOTS> owner;   // peer id of each slot, NO_PEER = bot
    RoundStart() { owner.fill(NO_PEER); }
  };

  struct RejectInfo
  {
    uint8_t reason = 0;
    uint16_t hostProtocol = 0;
    uint64_t hostBuildId = 0;
    uint64_t hostDataHash = 0;
    std::string text;
  };

  struct DesyncInfo
  {
    uint16_t round = 0;
    uint32_t tick = 0;
    uint64_t hostHash = 0;
    uint64_t peerHash = 0;
    uint8_t offender = NO_PEER;
  };

  // One line of the lobby, for display.
  struct LobbyEntry
  {
    uint8_t id = 0;
    std::string name;
    uint8_t slotMask = 0;
    int rttMs = -1;     // -1: not measured
    bool connected = true;
    bool ready = false; // a peer's ready flag as the host knows it; the host itself is always ready
  };

  struct LobbyView
  {
    uint8_t phase = PHASE_LOBBY;
    uint16_t round = 0;
    LobbySettings settings;                 // as far as peers know (map, blocks, spawner)
    std::vector<SlotInfo> slots;
    std::vector<LobbyEntry> peers;          // the host included
  };

  class LockstepSession
  {
    public:
      enum Role { HOST, PEER };
      enum State
      {
        CONNECTING,      // peer: HELLO sent, no answer yet
        LOBBY,           // connected, no round running (the first lobby)
        RUNNING,         // a round is being played
        BETWEEN_ROUNDS,  // the round is over here; the scoreboard; waiting for the host's next START
        ENDED            // over: endReason() / endText() say why
      };
      enum EndReason
      {
        END_NONE,
        END_REJECTED,        // the host refused our HELLO (rejectInfo())
        END_CONNECT_TIMEOUT, // the host never answered
        END_HOST_LOST,       // the host went silent
        END_HOST_CLOSED,     // the host said it is leaving
        END_KICKED,          // the host dropped us (we were silent too long)
        END_LEFT,            // we left (leave())
        END_DESYNC,          // a desync ended the match (leave() after one)
        END_NETWORK,         // vnet failed
        END_BAD_HOST,        // the host sent something we cannot play
        END_REMOVED          // the host removed us from the lobby (version 2)
      };

      // Where a machine that joined a round already running is (peer side; JOIN_NONE for everyone else).
      enum JoinStage
      {
        JOIN_NONE,         // not a late joiner (or the round it joined is over and a new one began)
        JOIN_CATCHING_UP,  // the host's record is arriving and being fast-forwarded (catchingUp())
        JOIN_WAITING,      // caught up with the live match; READY_AT is being sent
        JOIN_DONE          // TAKEOVER_AT arrived: from its tick this machine plays grantedSlots() (maybe none: a spectator)
      };

      // Hosts a session on `port` of `v` (0 = ephemeral; localPort() tells which). `v`
      // must outlive the session. Returns null (and `error`) if the socket cannot be
      // opened.
      static std::unique_ptr<LockstepSession> host(vnet* v, uint16_t port, const SessionOptions& options, std::string* error);
      // Joins the session at `hostAddr`. HELLO is repeated until WELCOME or REJECT.
      static std::unique_ptr<LockstepSession> join(vnet* v, const vnet_addr& hostAddr, const SessionOptions& options, std::string* error);

      ~LockstepSession();
      LockstepSession(const LockstepSession&) = delete;
      LockstepSession& operator=(const LockstepSession&) = delete;

      // -- every frame, in every state -----------------------------------------------
      // vnet_poll, receive, answer, time out, send. Cheap when idle. The caller advances
      // virtual time itself in tests (vnet_loopback_network_step).
      void pump();

      Role role() const { return m_role; }
      State state() const { return m_state; }
      bool ended() const { return m_state == ENDED; }
      EndReason endReason() const { return m_endReason; }
      const std::string& endText() const { return m_endText; }
      const RejectInfo& rejectInfo() const { return m_reject; }
      uint8_t myId() const { return m_myId; }
      uint32_t sessionId() const { return m_sessionId; }
      uint16_t localPort() const { return m_localPort; }
      uint64_t nowMs() const { return m_now; }

      // -- the lobby --------------------------------------------------------------------
      // Host: change what the next round is made of. Takes effect in the next LOBBY / START.
      void setLobbySettings(const LobbySettings& s);
      const LobbyView& lobby() const { return m_lobby; }
      // Peers connected, the host excluded.
      int peerCount() const;
      // Host: the delay the next round would get, from the RTTs measured so far.
      int proposedDelay() const;
      // Pure: ceil((worstRtt + jitter) / tickMs) + 1, clamped to MIN_DELAY..MAX_DELAY.
      static int chooseDelay(uint32_t worstRttMs, uint32_t jitterMs, uint32_t tickMs);
      static constexpr int MIN_DELAY = 2;
      static constexpr int MAX_DELAY = 8;
      static constexpr int DEFAULT_DELAY = 3;   // loopback and tests, and a peer with no measurement yet

      // Peer: say that this machine is (not) ready to start. Only meaningful in the lobby; it is
      // forgotten whenever a round starts or the lobby is entered again. The session repeats the
      // answer until the host's LOBBY shows it.
      void setReady(bool ready);
      bool wantsReady() const { return m_wantReady; }
      // The peers (host side: each peer's own answer; peer side: from LOBBY) that are not ready.
      int notReadyCount() const;
      // Host, in the lobby only: remove a peer (it is told: END_REMOVED). False if it is not there
      // or a round is on. The peer may join again; there is no ban list.
      bool kick(uint8_t peerId);
      // Host, between rounds: go back to the lobby (the scoreboard's "Back to lobby"), where the
      // settings can be changed, peers leave or join and everybody is asked to be ready again.
      // Allowed under the same conditions as the next round (canStartRound()); false otherwise.
      // Peers follow when their LOBBY says so and they are done with the round.
      bool returnToLobby();

      // Host: true when a round could start now: in the lobby or between rounds, and
      // every peer has acknowledged the last tick of the previous round (so no peer is
      // left waiting for ticks the new round would stop sending).
      bool canStartRound() const;
      // Host: why canStartRound() is false, as a sentence ("" when it is true).
      std::string cannotStartBecause() const;
      // Host: start a round with this seed. False (and `error`) if it cannot: not the
      // host, a round is running, peers still catching up, or the settings make an
      // invalid MatchConfig. The RoundStart is then available from takeRoundStart(),
      // like on a peer.
      bool startRound(uint64_t seed, std::string* error);
      // Both roles: true once for each round that has started here (the host's own
      // startRound, a peer's START). The game builds its Simulation from it.
      bool takeRoundStart(RoundStart& out);

      // -- a running round ----------------------------------------------------------------
      uint16_t round() const { return m_round; }
      int delay() const { return m_delay; }
      int numSlots() const { return m_numSlots; }
      // The next tick this machine would step (1 after START).
      uint32_t nextStepTick() const { return m_nextStep; }
      // The host's confirmed TickInputs for nextStepTick() are here, and nothing prevents
      // stepping (not desynced, the round is running).
      bool canStep() const;
      // The inputs to step with. Valid only while canStep() is true.
      const TickInputs& stepInputs() const;
      // Submit what the local devices did, for the tick that is about to be stepped (it
      // becomes the record for nextStepTick() + delay()). Call exactly once per stepped
      // tick, before or after the step but before tickStepped(). `sampledMask` is the set
      // of slots whose bytes in `sample` were really sampled here; events (TickInputs
      // ::control) are requests to the host. Bytes outside the mask are ignored.
      void submitLocal(const TickInputs& sample, uint8_t sampledMask);
      // The step happened: move on to the next tick.
      void tickStepped();
      // How many steps beyond the clock's this machine should take this frame to catch
      // up (0..2): 2 when it holds more than delay() + 2 confirmed ticks it has not stepped.
      int extraTicksWanted() const;
      static int catchUpTicks(uint32_t unsteppedConfirmed, int delay);
      // Ticks confirmed (host) / received in order (peer) beyond the last stepped one.
      uint32_t unsteppedTicks() const;
      // Peers (bit = id) the host says it waits on for its next tick; 0 when none/unknown.
      uint8_t waitingMask() const;
      // "waiting for <name>[, <name>]" or "" when not stalled.
      std::string waitingText() const;
      // Takeover requests (HUMAN_TAKES_OVER in a submitted sample) that the host did not
      // grant: the slot was taken by another machine in the same moment (the first claim in
      // id order wins) or was not a bot's. Known when the tick the request was for is stepped
      // (D ticks after it was made). The caller takes the slot back from its local input layer:
      // until then the player believes it drives a slot it does not. Each slot is returned once.
      std::vector<int> takeRejectedClaims();
      // The round is over on this machine (the simulation said so): stop stepping,
      // keep acknowledging and keep the connection alive until the next START or LEAVE.
      void roundFinished();
      // Report this machine's state hash after stepping `tick` (a checkpoint tick). A
      // peer sends it to the host; the host compares it with each peer's.
      void reportChecksum(uint32_t tick, uint64_t hash);
      // True from the moment a desync is known (host: detected; peer: told).
      bool desynced() const { return m_desynced; }
      const DesyncInfo& desyncInfo() const { return m_desync; }

      // Leave: tell the other side (a peer tells the host, which turns its slots into
      // bots; the host tells every peer, which ends the session there), and end.
      void leave(LeaveReason reason = LEAVE_QUIT);
      // After leave(): keeps polling for up to maxMs so datagrams that simulated latency
      // (VNET_IMPAIR) still holds are put on the wire before the context goes away. A no-op on a
      // backend that cannot wait. Without it a LEAVE sent under latency is lost and the others
      // fall back to their 10 s timeout.
      void flushOutgoing(uint32_t maxMs = 250);

      // -- joining a round in progress, reconnecting, watching (peer side; brief 13) -----------------------------
      // The reconnect token the host issued to this machine (WELCOME); 0 before. A game that wants to let a crashed
      // or dropped player back in keeps it, and passes it as SessionOptions::reconnectToken next time.
      uint32_t token() const { return m_token; }
      JoinStage joinStage() const { return m_joinStage; }
      // The round was already running when we joined and we are still stepping the host's record to catch up. While
      // true the caller fast-forwards (NetMatch::fastForward) instead of stepping at the clock's pace.
      bool catchingUp() const { return m_joinStage == JOIN_CATCHING_UP; }
      // How far the catch-up is, 0..1, from what is stepped and the newest tick the host has said it has confirmed.
      double catchUpFraction() const;
      // The newest tick the host has told us it confirmed (grows while catching up).
      uint32_t catchUpHead() const { return std::max(m_headKnown, m_confirmed); }
      // Slots the host offered back (reconnect), as said in WELCOME_MIDMATCH.
      uint8_t offeredSlots() const { return m_offerMask; }
      // The slots this machine plays now or from the tick TAKEOVER_AT named (bit = slot); 0 for a spectator.
      uint8_t grantedSlots() const { return m_grantedMask; }
      // Slots newly granted since the last call, once each: the caller binds its keyboard / pad to them (the sim
      // gets the HUMAN_TAKES_OVER from the host, in the final inputs; the local layer is the caller's).
      uint8_t takeGrantedSlots();
      // True if this machine only watches (SessionOptions::spectate, or no bot was free when it joined).
      bool spectating() const;
      // The tick TAKEOVER_AT named (0 before it arrived).
      uint32_t takeoverTick() const { return m_takeoverTick; }
      // The tick from which the host waits for this machine's inputs (0: always; ~0u: never).
      uint32_t participateFrom() const { return m_participateFrom; }
      // Host: the late joiners / reconnects it has admitted, for display and tests.
      int hotJoinsAdmitted() const { return m_hotJoins; }

      // -- diagnostics -----------------------------------------------------------------------
      struct Stats
      {
        uint64_t packetsSent = 0;
        uint64_t packetsReceived = 0;
        uint64_t packetsDropped = 0;      // vnet refused a send (EAGAIN/EIO) - a lost packet
        uint64_t badPackets = 0;          // undecodable, wrong session, wrong sender
        uint64_t inputRecordsSent = 0;    // peer: per-tick input records put on the wire (a spectator sends none)
      };
      const Stats& stats() const { return m_stats; }
      // The RTT to a peer (host side), -1 if none yet.
      int peerRtt(uint8_t id) const;
      // Gives a human-readable summary of the delay choice ("D=5 from worst RTT 120 ms ...").
      const std::string& delayNote() const { return m_delayNote; }

    private:
      LockstepSession();

      // One machine in the session, as the host sees it (the host's own entry is index 0;
      // a peer only fills entry 0's address and its own).
      struct Member
      {
        bool present = false;
        bool gone = false;           // timed out / left during a round: no longer waited for
        vnet_addr addr;
        std::string name;
        uint32_t token = 0;
        uint32_t nonce = 0;
        int localPlayers = 1;
        uint64_t lastHeard = 0;
        uint64_t lastSent = 0;
        // RTT samples (ms), newest last, at most 16.
        std::deque<uint32_t> rtt;
        uint64_t lastPing = 0;
        uint32_t pingSeq = 0;
        // Round state (host side).
        bool startAcked = false;
        uint32_t contig = 0;                    // highest tick t with this peer's records for 1..t held
        std::map<uint32_t, InputRecord> recs;   // held records above `consumed`
        uint32_t ackedTicks = 0;                // the peer's ack of our confirmed ticks
        uint32_t sentUpTo = 0;                  // highest tick put on the wire to it (redundancy off)
        uint64_t lastTicksSent = 0;
        uint64_t lastStartSent = 0;
        std::map<uint32_t, uint64_t> hashes;    // checkpoint hashes it reported
        bool ready = false;                     // lobby: it said it is ready
        uint32_t readySeq = 0;                  // the highest READY seq applied
        // Hot-join, reconnect and spectators (brief 13).
        bool spectator = false;                 // HELLO_SPECTATE: never dealt a slot, never waited for
        bool midJoin = false;                   // joined (or reconnected) while a round was running
        bool verified = false;                  // one of its checksums matched the host's (a late joiner is trusted from then on)
        bool readyAt = false;                   // READY_AT received
        uint64_t readyAtSince = 0;
        uint32_t readyAtTick = 0;
        uint32_t takeoverTick = 0;              // scheduled: the tick the host starts waiting for it, and its takeover events apply
        uint8_t takeoverMask = 0;               // the slots dealt to it (0 = a spectator)
        bool takeoverApplied = false;
        bool takeoverDecided = false;           // TAKEOVER_AT has been chosen (mask may be 0)
        uint8_t offerMask = 0;                  // reconnect: its old slots that were still bots when it came back
        uint8_t lostMask = 0;                   // the slots it owned when it dropped (what a reconnect is offered)
        uint8_t kickReason = LEAVE_KICKED;      // what a late packet from it is answered with once it is gone
        uint64_t lastInfoSent = 0;              // WELCOME_MIDMATCH repeats while it catches up
        uint32_t lastCatchAck = 0;
        // Whether the host waits for this machine's record for tick t.
        bool waited(uint32_t t) const
        {
          if (spectator) return false;
          if (!midJoin) return true;
          return takeoverMask != 0 && takeoverTick != 0 && t >= takeoverTick;
        }
      };

      static bool openSocket(vnet* v, uint16_t port, int* sock, uint16_t* localPort, std::string* error);
      void init(vnet* v, int sock, uint16_t port, const SessionOptions& o);

      // Receiving.
      void drain();
      void handle(const vnet_addr& from, const uint8_t* data, size_t size);
      void hostHandle(const vnet_addr& from, const Header& h, Reader& r);
      void peerHandle(const vnet_addr& from, const Header& h, Reader& r);
      void onHello(const vnet_addr& from, const uint8_t* body, size_t size);
      void onInput(Member& m, const InputMsg& msg);
      void onTicks(const TicksMsg& msg);
      void onStart(const StartMsg& msg);
      void onLobby(const LobbyMsg& msg);
      void onChecksumHost(Member& m, const ChecksumMsg& msg);
      void onReady(Member& m, const ReadyMsg& msg);
      void onReadyAt(Member& m, const ReadyAtMsg& msg);
      void onTakeoverAt(const TakeoverAtMsg& msg);
      void onWelcomeMidMatch(const WelcomeMidMatchMsg& msg);
      bool supersedes() const;
      void sendReady();
      void onDesync(const DesyncMsg& msg);

      // Sending.
      template <typename Msg> void sendMsg(uint8_t type, const vnet_addr& to, const Msg& msg);
      void sendRaw(const vnet_addr& to, Writer& w);
      void sendReject(const vnet_addr& to, uint32_t nonce, RejectReason reason, const std::string& text);
      void hostSend();
      void peerSend();
      void sendTicksTo(Member& m, bool force);
      void sendCatchUp(Member& m);
      uint32_t sendTickRange(Member& m, uint32_t from);
      const TickInputs* confirmedTick(uint32_t t) const;
      void sendWelcome(Member& m, const vnet_addr& to);
      void sendReadyAt();
      HelloMsg makeHello() const;
      bool admitHotJoiner(int id, const vnet_addr& from, const HelloMsg& h, uint8_t offer, uint32_t token);
      void dropForRestart(int id);
      void evictDesynced(int id, const DesyncInfo& d);
      uint8_t reservedSlots() const;
      uint8_t freeBotSlots() const;
      void scheduleTakeover(Member& m);
      void sendStartTo(Member& m);
      void broadcastLobby();
      void sendKeepalives();
      void sendPings();
      void sendInputToHost(bool force);
      void sendChecksums(bool force);

      // Host logic.
      void confirmTicks();
      bool mergeTick(uint32_t t, TickInputs& out);
      void removePeer(uint8_t id, const char* why);
      void recomputeRoster();
      void rebuildLobbyView();
      uint8_t teamOf(int slot) const;
      int computeDelay(std::string* note) const;
      void checkTimeouts();
      void trimHistory();
      void compareChecksum(uint8_t peerId, uint32_t tick);
      void declareDesync(const DesyncInfo& d);
      void setupRound(const StartMsg& s, bool joining);
      bool buildStart(uint64_t seed, StartMsg& out, std::string* error);
      uint32_t newToken();
      static uint32_t lowMs(uint64_t t) { return (uint32_t) (t & 0xFFFFFFFFu); }

      void end(EndReason reason, const std::string& text);

      vnet* m_v = nullptr;
      int m_sock = -1;
      Role m_role = HOST;
      SessionOptions m_opt;
      State m_state = LOBBY;
      EndReason m_endReason = END_NONE;
      std::string m_endText;
      RejectInfo m_reject;
      uint64_t m_now = 0;
      uint16_t m_localPort = 0;
      NetRandom m_rng;

      uint32_t m_sessionId = 0;
      uint8_t m_myId = NO_PEER;
      uint32_t m_myNonce = 0;
      vnet_addr m_hostAddr;
      uint64_t m_lastHelloSent = 0;
      uint64_t m_connectStart = 0;
      std::array<Member, MAX_PEERS> m_members;   // [0] = the host; on a peer only [0] (the host's address and last-heard) is used
      LobbySettings m_settings;
      LobbyView m_lobby;
      uint64_t m_lastLobbySent = 0;
      bool m_lobbyDirty = true;
      uint64_t m_lastKeepalive = 0;

      // The round.
      uint16_t m_round = 0;
      int m_delay = DEFAULT_DELAY;
      int m_numSlots = 0;
      std::array<uint8_t, MAX_SLOTS> m_owner;
      bool m_roundStartPending = false;           // a RoundStart is waiting for takeRoundStart()
      RoundStart m_roundStart;
      bool m_haveQueuedStart = false;             // peer: a START arrived while the old round still runs
      StartMsg m_queuedStart;
      uint32_t m_nextStep = 1;
      uint32_t m_nextLocal = 1;                   // the tick the next submitLocal() is for
      uint32_t m_finalTick = 0;                   // the last tick stepped when the round finished here
      // Confirmed ticks. Host: all confirmed ticks not yet acknowledged by everyone and stepped.
      // Peer: ticks received, not yet stepped.
      std::map<uint32_t, TickInputs> m_ticks;
      uint32_t m_confirmed = 0;                   // host: highest confirmed tick; peer: highest contiguous received
      uint32_t m_ackOfMine = 0;                   // peer: highest tick of OUR records the host acknowledged
      uint32_t m_lastAckSent = 0;
      std::map<uint32_t, InputRecord> m_myRecs;   // our own records; host: until consumed; peer: until acked
      uint32_t m_mySentUpTo = 0;
      uint64_t m_lastInputSent = 0;
      uint8_t m_waitingMask = 0;
      uint64_t m_lastTicksHeard = 0;
      std::array<uint8_t, MAX_SLOTS> m_ownersSeen;   // peer: who plays each slot, as of the newest TICKS
      uint32_t m_ownersAsOf = 0;
      std::map<uint32_t, std::array<uint8_t, MAX_SLOTS>> m_ownersAt;   // host: who played each slot after each confirmed tick
      std::array<uint8_t, MAX_SLOTS> m_roundStartOwners;
      uint32_t m_lastHostHashTick = 0;
      std::map<uint32_t, std::vector<int>> m_claims;   // tick -> slots asked for in that tick's record
      std::vector<int> m_rejectedClaims;
      struct Removed { vnet_addr addr; uint32_t nonce; uint64_t until; uint64_t lastSent; };
      std::vector<Removed> m_removed;             // host: peers it removed from the lobby; answered with LEAVE_REMOVED for a while
      std::vector<ControlEvent> m_hostEvents;     // host: RETURN_TO_AI for peers that left, for the next confirmed tick

      // Checksums.
      std::map<uint32_t, uint64_t> m_hostHashes;  // host: its own
      std::deque<ChecksumEntry> m_myChecksums;    // peer: last few, resent
      uint64_t m_lastChecksumSent = 0;
      bool m_checksumDirty = false;
      bool m_desynced = false;
      DesyncInfo m_desync;
      uint64_t m_lastDesyncSent = 0;

      // Ready (peer side): what the player wants, and the counter of the answers sent.
      bool m_wantReady = false;
      uint32_t m_readySeq = 0;
      uint64_t m_lastReadySent = 0;

      // Hot-join (brief 13). Host: every confirmed tick of the round, kept so that a joiner can be sent the record
      // from tick 1 (m_ticks is trimmed as peers acknowledge).
      std::vector<TickInputs> m_history;
      bool m_historyComplete = true;
      uint64_t m_roundEndedAt = 0;
      int m_hotJoins = 0;
      // Peer side.
      uint32_t m_token = 0;
      bool m_midJoin = false;
      JoinStage m_joinStage = JOIN_NONE;
      uint32_t m_joinRound = 0;
      uint32_t m_headKnown = 0;
      uint8_t m_offerMask = 0;
      uint8_t m_grantedMask = 0;
      uint8_t m_grantedPending = 0;
      uint32_t m_takeoverTick = 0;
      uint32_t m_participateFrom = 0;
      uint64_t m_lastReadyAtSent = 0;

      std::string m_delayNote;
      Stats m_stats;
  };
}

#endif /* _LOCKSTEPSESSION_H */
