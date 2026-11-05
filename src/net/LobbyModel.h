/*
 * LobbyModel.h
 *
 * What the online lobby page and the in-game overlay SAY, as plain data (brief 08). No SDL, no
 * RmlUi, no sockets: LockstepSession's LobbyView and a few facts in, rows / flags / sentences
 * out. The RmlUi page (src/RmlUI/binders/NetLobbyBinder) only pastes these into the document,
 * so every decision worth testing - which slot is host-local, remote, a computer player or open;
 * who is blocking the start; what to tell a player whose join was refused; how a long or odd
 * name is cut - is a function here, tested without a window.
 *
 * Text is UTF-8 and may be hostile: LockstepSession's strings keep every byte from 0x80 up
 * (brief 07), so a name can be invalid UTF-8. Anything that is shown goes through
 * sanitizeUtf8()/clipUtf8() first.
 */

#ifndef _LOBBYMODEL_H
#define _LOBBYMODEL_H

#include <cstdint>
#include <string>
#include <vector>

#include "LockstepSession.h"

namespace netplay
{
  // -- text ------------------------------------------------------------------------------------

  // `text` with every byte sequence that is not well-formed UTF-8 (a stray continuation byte, a truncated
  // or overlong sequence, a surrogate, a code point past U+10FFFF) replaced by '?', and control characters
  // below 0x20 or 0x7F too. The result is always valid UTF-8.
  std::string sanitizeUtf8(const std::string& text);
  // Number of characters (code points) in well-formed UTF-8.
  size_t utf8Length(const std::string& text);
  // At most `maxChars` characters, cut between characters; when something was cut the last three are
  // "..." (so the result is still `maxChars` long). The input is sanitized first.
  std::string clipUtf8(const std::string& text, size_t maxChars);
  // The longest prefix of at most `maxBytes` bytes that ends between characters (for the wire's 24-byte names).
  std::string clipUtf8Bytes(const std::string& text, size_t maxBytes);

  // -- the lobby table --------------------------------------------------------------------------

  enum class SlotKind
  {
    OPEN,      // a seat beyond the match's player count: a joining player gets one
    AI,        // a computer player (a person can take it over in a round)
    HOST,      // a player on the host's machine
    REMOTE     // a player on a peer's machine
  };

  struct LobbyRow
  {
    int slot = 0;                  // 0-based
    SlotKind kind = SlotKind::OPEN;
    bool mine = false;             // played on this machine
    uint8_t owner = NO_PEER;       // peer id, NO_PEER for AI and open
    std::string name;              // already clipped: "Alice", "Alice 2/3", "Computer", "open"
    std::string number;            // "1".."6"
    int team = -1;                 // 0-based, -1 for an open seat
    bool showReady = false;        // a peer's seat: the ready mark means something
    bool ready = false;
    int pingMs = -1;               // -1: none to show
    bool teamEditable = false;     // the host may change it
    bool kickable = false;         // the host may remove this peer (lobby, a peer's seat)
    uint8_t kickId = NO_PEER;
  };

  struct LobbyContext
  {
    uint8_t myId = NO_PEER;
    bool isHost = false;
    // Host: canStartRound() right now, and the reason when it is false (a sentence), as the session says it.
    bool canStart = false;
    std::string startBlocked;
    // Peer: what this machine asked for with setReady().
    bool myReady = false;
    // The session's state is LockstepSession::LOBBY (as against BETWEEN_ROUNDS, shown for a late joiner).
    bool inLobby = true;
    // Characters of a name that fit the table; longer names are cut with "...".
    size_t nameChars = 22;
    // Brief 12, a game through a relay: whether it is one, the room's code once the relay has said it ("" before),
    // and what carries the datagrams ("relay-wt", "relay-ws", "relay-udp"; "" before it is up).
    bool viaRelay = false;
    std::string roomCode;
    std::string transport;
    // Peer: the host's connection has gone quiet (the host's tab may be in the background) for this long.
    uint32_t hostSilenceMs = 0;
  };

  struct LobbyScreen
  {
    std::vector<LobbyRow> rows;       // always MAX_SLOTS rows
    int shownRows = 0;                // rows [0, shownRows) are seats of this match; the rest are open seats
    std::string title;
    std::string settings;             // "Map 17 x 13, blocks 55%, enemies on, 4 slots"
    std::string status;               // the sentence under the table
    std::string roomCode;             // a relay game once the room exists: the code, to show big ("" otherwise)
    bool isHost = false;
    bool canStart = false;            // the Start button is enabled (host)
    std::string startLabel;           // "Start match" / "Ready" / "Not ready" / "Waiting for the host"
    bool canToggleReady = false;      // the Ready button is enabled (peer)
    bool myReady = false;
    int readyPeers = 0;
    int humanPeers = 0;               // peers excluding the host
    // Host: the labels of the five setting buttons (width, height, blocks, enemies, players).
    std::string settingButtons[5];
  };

  // Rows for the six seats of the table, from the view as this machine has it.
  std::vector<LobbyRow> buildLobbyRows(const LobbyView& view, const LobbyContext& ctx);
  // Rows plus every sentence and flag of the page.
  LobbyScreen buildLobbyScreen(const LobbyView& view, const LobbyContext& ctx);
  // "Map 17 x 13, blocks 55%, enemies on, 4 slots".
  std::string describeSettings(const LobbySettings& s, int slots);

  // -- why a session ended -------------------------------------------------------------------

  struct Identity
  {
    uint16_t protocol = PROTOCOL_VERSION;
    uint64_t buildId = 0;
    uint64_t dataHash = 0;
  };

  // The headline and the lines of the "could not join / disconnected" page.
  struct EndMessage
  {
    std::string title;
    std::vector<std::string> lines;
    bool isError = true;
  };

  // `reason`/`reject`/`sessionText` are the session's endReason(), rejectInfo() and endText(); `mine` is this build's identity;
  // `address` is what the player typed (host:port) for the lines that name it; `joining` is true for a peer.
  EndMessage describeEnd(LockstepSession::EndReason reason, const RejectInfo& reject, const std::string& sessionText,
                         const Identity& mine, const std::string& address);
  // The page for a session over a relay whose transport failed (vnet_status_info::error: VNET_ENOROOM, VNET_ECONNREFUSED ...);
  // `relay` is what the player typed, `room` the code (joiner) or "" (host).
  EndMessage describeRelayFailure(int vnetError, const std::string& relay, const std::string& room, bool hosting);
  // 0x6767612d056ba8af as "0x6767612d056ba8af".
  std::string hex64(uint64_t v);

  // -- addresses and the saved settings ---------------------------------------------------------

  // "host", "host:port", "1.2.3.4:7777", "[::1]:7777" -> host and port (the default port when none is
  // given). False, and a sentence in `error`, for an empty host, spaces or other junk in it, or a port
  // outside 1..65535. `normalised` (optional) gets "host:port" in the form the session wants.
  bool parseHostPort(const std::string& text, int defaultPort, std::string* host, int* port, std::string* error,
                     std::string* normalised = nullptr);
  // A UDP port typed in a field: digits only, 1..65535.
  bool parsePort(const std::string& text, int* port, std::string* error);
  // A name for the others to see: control characters removed, spaces trimmed, at most 24 bytes ending between characters.
  std::string cleanPlayerName(const std::string& text);

  // -- the relay (brief 12) -----------------------------------------------------------------------

  // The relay's UDP port when the player does not type one (vnet-relay's default).
  constexpr int DEFAULT_RELAY_UDP_PORT = 7777;
  // The port every relay session's socket uses. Relay addresses are 10.0.0.N:port, the room's creator is 10.0.0.1, so a joiner
  // reaches the host at "room-host:" + this, and the host listens on it.
  constexpr int RELAY_SESSION_PORT = 7777;

  // What a typed relay address means for each world: the browser uses the WebSocket URL (RELAY_AUTO derives the WebTransport
  // one from a wss:// URL), the native game the relay's UDP "host:port". Either is "" if it cannot be derived.
  struct RelayTarget
  {
    std::string ws;    // "wss://host/vnet" or "ws://host:8080/vnet"
    std::string udp;   // "host:7777"
    std::string host;  // the host part alone
  };
  // Accepts "relay.example.org", "relay.example.org:7777", "[::1]:7777" (a UDP address: the WebSocket URL is derived,
  // wss://host/vnet when the page is served over https and `secure` is true, else ws://host:8080/vnet, the relay's own
  // defaults) and "ws://host:8080/vnet" / "wss://host/vnet" (a URL: the UDP address is host:7777; a missing path is /vnet).
  // False, and a sentence in `error`, for anything else. `normalised` gets the text trimmed.
  bool parseRelayAddress(const std::string& typed, bool secure, RelayTarget* out, std::string* error,
                         std::string* normalised = nullptr);
  // A room code as typed: 6 symbols of ABCDEFGHJKLMNPQRSTUVWXYZ23456789, any case, with spaces or '-' tolerated.
  // `code` gets the canonical upper-case six. False with a sentence otherwise.
  bool parseRoomCode(const std::string& typed, std::string* code, std::string* error);
  // The relay a build offers before the player types one: the build's BITRIOT_DEFAULT_RELAY (a CMake cache entry) or, when
  // it is empty, "127.0.0.1:7777" (a relay run on this machine).
  std::string defaultRelayAddress();

  struct NetPrefs
  {
    std::string name;
    std::string joinAddress;
    int hostPort = 7777;
    int localPlayers = 1;
    // Brief 12: the relay ("" = the build's default) and the last room joined.
    std::string relay;
    std::string room;
    bool operator==(const NetPrefs& o) const
    {
      return name == o.name && joinAddress == o.joinAddress && hostPort == o.hostPort && localPlayers == o.localPlayers
             && relay == o.relay && room == o.room;
    }
  };
  // key=value lines. A missing file is not an error (`out` keeps its defaults, false is returned); a corrupt
  // one is read as far as it makes sense (unknown keys, lines without '=', junk values and over-long values
  // are skipped; the values that survive are cleaned).
  bool loadNetPrefs(const std::string& path, NetPrefs& out);
  bool parseNetPrefs(const std::string& text, NetPrefs& out);
  std::string formatNetPrefs(const NetPrefs& p);
  // Writes via a temporary file (named for this process) and a rename, so a crash cannot leave half a file. False if it
  // cannot be written. (POSIX rename replaces the old file; std::rename on Windows refuses to: handle that when netplay is
  // enabled for MinGW.)
  bool saveNetPrefs(const std::string& path, const NetPrefs& p);

  // -- during a round -----------------------------------------------------------------------------

  // The sentence for a control event in a stepped tick that the others should hear about ("" = none): a slot that a
  // person leaves ("Bob left - now AI"). `who` is the name of the slot's player (clipped here), or "" if unknown.
  std::string eventToast(const ControlEvent& e, const std::string& who);

  // LockstepSession::waitingText() ("waiting for Bob, Carol") as the overlay's sentence ("Waiting for Bob, Carol...").
  std::string stallSentence(const std::string& waitingText);

  // Shows the "waiting for ..." text only after it has been wanted for `delayMs` without a break, so a late packet
  // that holds a tick for a frame does not make the HUD flash.
  class StallTimer
  {
    public:
      explicit StallTimer(uint32_t delayMs = 500) : m_delay(delayMs) {}
      // Call every frame with the text the session wants shown ("" = not stalled) and a clock in ms.
      // Returns the text to show now ("" = nothing).
      const std::string& update(const std::string& wanted, uint32_t nowMs);
    private:
      uint32_t m_delay;
      std::string m_wanted;
      uint32_t m_since = 0;
      std::string m_shown;
  };
}

#endif /* _LOBBYMODEL_H */
