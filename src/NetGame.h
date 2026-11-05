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

#include <memory>
#include <string>

#include "GameSettings.h"
#include "net/LockstepSession.h"
#include "net/NetMatch.h"

class Simulation;

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
    };

    // Opens the network and the session. A peer's `join` host name is resolved here (the
    // lookup blocks, so it happens before the first frame). Null and `error` on failure.
    static std::unique_ptr<NetGame> create(const Params& params, Simulation& sim, std::string* error);
    ~NetGame();

    netplay::LockstepSession& session() { return *m_session; }
    netplay::NetMatch& match() { return *m_match; }
    bool isHost() const { return m_params.host; }
    const Params& params() const { return m_params; }

    // Once a frame, in every state: polls the network, answers, sends keepalives.
    void pump();
    // Sleeps up to `ms` milliseconds, waking early when a datagram may have arrived (the
    // main loop's sleep, so that a tick the host has just confirmed is stepped now and not
    // up to a frame later: that is up to 33 ms less round trip, which is input delay saved).
    // False if this backend cannot wait (the caller sleeps as usual).
    bool wait(uint32_t ms);

    // The player's own choices, saved when netplay started and put back when it stops
    // (a match overwrites the roster, the player count and the map).
    void restoreSettings();

    // Host: brings the lobby settings up to date with the menu's (map, blocks, spawner)
    // and with what the players need (a taller map for five or six players), then starts a round
    // with a fresh seed. False, and `error`, if it cannot.
    bool startRound(std::string* error);

    // The lobby page.
    std::string lobbyTitle() const;
    std::string lobbyText() const;
    // A line for the title bar / overlay: the connection, the delay, and who the game waits for.
    std::string statusLine() const;
    // What the stall overlay says ("waiting for peer1"), or "".
    std::string stallText() const;

  private:
    NetGame() {}
    void updateLobbySettings();

    Params m_params;
    Simulation* m_sim = nullptr;
    vnet* m_v = nullptr;
    std::unique_ptr<netplay::LockstepSession> m_session;
    std::unique_ptr<netplay::NetMatch> m_match;
    std::GameSettings::Snapshot m_saved;
    bool m_haveSaved = false;
};

#endif /* _NETGAME_H */
