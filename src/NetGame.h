/*
 * NetGame.h
 *
 * The game's side of netplay (brief 07): owns the vnet context, the LockstepSession and
 * the NetMatch of a networked game, remembers the player's local settings while a
 * match overwrites them, and words the lobby page and the status line. GameEngine drives it
 * (a call per frame, in every state); the rules of the protocol are in LockstepSession,
 * the stepping of a round in NetMatch.
 *
 * Core, not sim: it needs vnet and the game's settings, never the renderer.
 */

#ifndef _NETGAME_H
#define _NETGAME_H

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "GameSettings.h"
#include "net/LobbyModel.h"
#include "net/LockstepSession.h"
#include "net/NetMatch.h"

class Simulation;

// Looks up a "host:port" on a worker thread, so that a slow or dead name server cannot freeze the menus (vnet_resolve_host
// blocks for as long as DNS takes). The caller polls done() once a frame. Destroying it while the lookup still runs is safe:
// the thread keeps its own reference to the result and just finishes unseen.
class NetResolve
{
  public:
    // Returns 0 and fills the address, or a vnet error code (vnet_strerror words it).
    typedef std::function<int(const std::string&, vnet_addr*)> Resolver;
    static int defaultResolver(const std::string& hostPort, vnet_addr* out);

    explicit NetResolve(const std::string& hostPort, Resolver resolver = &NetResolve::defaultResolver);
    bool done() const;
    // Valid once done(): 0 if the address was found.
    int result() const;
    const vnet_addr& address() const;
    const std::string& hostPort() const { return m_hostPort; }

  private:
    struct State
    {
      mutable std::mutex mutex;
      bool done = false;
      int rc = 0;
      vnet_addr addr;
    };
    std::shared_ptr<State> m_state;
    std::string m_hostPort;
    mutable vnet_addr m_copy;
};

class NetGame
{
  public:
    struct Params
    {
      bool host = false;
      uint16_t port = 7777;        // host: the UDP port to listen on
      std::string join;            // peer: "host:port"
      std::string name;            // shown to the others; empty: a default
      int localPlayers = 1;        // players on this machine (keyboard sets 1..n)
      int totalSlots = 0;          // host: player slots in all (0: as many as humans, at least 2)
      std::string recordDir;       // --record: a round record per round (the final inputs)
      bool requireReady = false;   // host: the lobby's Start waits until every peer is ready (the menus: yes; --host: no, as in 07)
      // Brief 12: play through a vnet relay instead of direct UDP (host: creates a room; peer: joins `room`). `host`,
      // `join`, `port` then mean nothing: the session's socket is on netplay::RELAY_SESSION_PORT of the room.
      bool relay = false;
      std::string relayWs;         // the browser's relay URL ("wss://host/vnet"); native builds ignore it
      std::string relayUdp;        // the native relay "host:port" (numeric, or a name vnet resolves while creating); browsers ignore it
      std::string room;            // peer: the room code
      std::string relayDisplay;    // what the player typed, for the words of an error page
      std::string relayWt;         // development: an explicit WebTransport URL ("https://127.0.0.1:4433/vnet")
      std::string relayCertSha256; // development: the hash of the relay's -dev-cert certificate
      std::string impair;          // a vnet impairment spec ("lat=40,loss=5,seed=3"); VNET_IMPAIR in the environment wins (native)
    };

    // A function the browser transports call (from a browser event) when something has arrived for vnet_poll()
    // (vnet_config.wake). It may only arrange for a poll; see vnet.h. Native builds never call it. Set before create().
    static void setWakeHandler(void (*handler)());

    // Opens the network and the session. A peer's `join` host name is resolved here (the
    // lookup blocks, so this is for the command line, before the first frame; the menus use
    // NetResolve and createResolved). Null and `error` on failure.
    static std::unique_ptr<NetGame> create(const Params& params, Simulation& sim, std::string* error);
    // A peer whose address was looked up already (params.join is only shown to the player).
    static std::unique_ptr<NetGame> createResolved(const Params& params, Simulation& sim, const vnet_addr& hostAddr, std::string* error);
    ~NetGame();

    netplay::LockstepSession& session() { return *m_session; }
    netplay::NetMatch& match() { return *m_match; }
    bool isHost() const { return m_params.host; }
    const Params& params() const { return m_params; }

    // Once a frame, in every state: polls the network, answers, sends keepalives. The moment
    // the session has ended (the host went, we were dropped or removed, a desync) the player's own
    // settings come back, so a page that says "disconnected" never sits on the match's.
    void pump();
    // Sleeps up to `ms` milliseconds, waking early when a datagram may have arrived (the
    // main loop's sleep, so that a tick the host has just confirmed is stepped now and not
    // up to a frame later: that is up to 33 ms less round trip, which is input delay saved).
    // False if this backend cannot wait (the caller sleeps as usual).
    bool wait(uint32_t ms);

    // -- the relay (brief 12) ---------------------------------------------------------------------
    bool viaRelay() const { return m_params.relay; }
    // The room's code once the relay has said it ("" before, and with direct UDP).
    std::string roomCode() const;
    // "relay-wt", "relay-ws", "relay-udp", "udp": what carries the datagrams now.
    std::string transport() const;
    // The transport's own state: VNET_STATUS_CONNECTING / READY / FAILED.
    int transportState() const;
    // The latest round trip to the relay, -1 if none yet (see vnet_status_info::relay_rtt_ms).
    int relayRttMs() const;

    // The player's own choices, saved when netplay started and put back when it stops
    // (a match overwrites the roster, the player count and the map).
    void restoreSettings();

    // Host: brings the lobby settings up to date with the menu's (map, blocks, spawner)
    // and with what the players need (a taller map for five or six players), then starts a round
    // with a fresh seed. False, and `error`, if it cannot.
    bool startRound(std::string* error);

    // -- the lobby (host) -------------------------------------------------------------------------
    enum HostSetting { SET_MAP_WIDTH, SET_MAP_HEIGHT, SET_BLOCKS, SET_SPAWNER, SET_SLOTS };
    // The host's lobby controls: each steps its value to the next (wrapping, the same steps as the game
    // options screen; total slots go auto, 2, 3, 4, 5, 6). The change reaches the peers in the next LOBBY.
    void cycleSetting(HostSetting which);
    // Moves a slot to the next team (0 .. slots-1, wrapping).
    void cycleTeam(int slot);
    // Removes a peer from the lobby.
    bool kick(uint8_t peerId) { return m_session->kick(peerId); }
    // Back from the scoreboard to the lobby.
    bool returnToLobby();

    // What the lobby page needs from the session and the game around it.
    netplay::LobbyContext lobbyContext() const;
    netplay::LobbyScreen lobbyScreen() const;
    // The words for a session that has ended.
    netplay::EndMessage endMessage() const;
    // A line for the overlay: the connection, the delay, and who the game waits for.
    std::string statusLine() const;
    // What the stall overlay says ("waiting for peer1"), or "".
    std::string stallText() const;

  private:
    NetGame() {}
    static std::unique_ptr<NetGame> build(const Params& params, Simulation& sim, const vnet_addr* resolved, std::string* error);
    void updateLobbySettings();

    Params m_params;
    Simulation* m_sim = nullptr;
    vnet* m_v = nullptr;
    std::unique_ptr<netplay::LockstepSession> m_session;
    std::unique_ptr<netplay::NetMatch> m_match;
    std::GameSettings::Snapshot m_saved;
    bool m_haveSaved = false;
    netplay::LobbySettings m_hostSettings;   // what the host chose in the lobby (teams: -1 = the slot's own)
};

#endif /* _NETGAME_H */
