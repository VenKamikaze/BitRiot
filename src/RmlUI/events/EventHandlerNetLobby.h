/*
 * EventHandlerNetLobby.h
 *
 * The buttons of the online pages (brief 07, grown in 08): the lobby (netlobby.rml) and the host and join forms
 * (nethost.rml, netjoin.rml). They only raise flags in NetUiRequests; GameEngine reads them once a frame, because
 * starting or leaving a session, opening a socket or removing a peer is not something to do from inside an RmlUi event.
 */
#ifndef EVENTHANDLERNETLOBBY_H
#define EVENTHANDLERNETLOBBY_H

#include <functional>
#include <string>

#include "EventHandler.h"

struct NetUiRequests
{
  // The lobby.
  bool startMatch = false;      // the host's Start, or a peer's Ready / Not ready (the engine knows which it is)
  bool leave = false;           // Leave, Cancel or Back, on the lobby and on the error page
  bool copyRoom = false;        // brief 12: "Copy room code" (a relay lobby)
  int teamSlot = -1;            // host: cycle the team of this slot
  int kickSlot = -1;            // host: remove the peer that plays this slot
  int setting = -1;             // host: cycle a setting (NetGame::HostSetting)
  // The forms.
  bool hostGo = false;
  bool joinGo = false;
  bool relayHostGo = false;     // brief 12: "Host via relay" -> Open room
  bool relayJoinGo = false;     // brief 12: "Join by room code" -> Connect
  struct Form
  {
    std::string name;
    std::string address;        // join: as typed
    int port = 7777;            // host
    int localPlayers = 1;
    std::string relay;          // relay forms: the relay address as typed
    std::string relayWs;        // ... and what it means for a browser (WebSocket URL)
    std::string relayUdp;       // ... and for a native build (UDP host:port)
    std::string room;           // relay join: the room code, canonical upper case
  } form;
};

class EventHandlerNetLobby : public EventHandler
{
  public:
    static const char* const RML_FILENAME;
    static NetUiRequests& requests();
    void ProcessEvent(Rml::Event& event, const Rml::String& value) override;
};

// The host and join screens. Fills the fields from the saved settings when the page opens, validates them when a button
// is pressed (an error is shown on the page and nothing is requested), and remembers what was used.
class EventHandlerNetForm : public EventHandler
{
  public:
    static const char* const HOST_FILENAME;
    static const char* const JOIN_FILENAME;
    static const char* const RELAY_HOST_FILENAME;   // brief 12: nethostrelay.rml
    static const char* const RELAY_JOIN_FILENAME;   // netjoinrelay.rml
    // Brief 12. The relay a form offers when nothing was saved (a native build: the build's default; a web page: derived
    // from where it was loaded, or the page's own ?relay=), a room code to prefill (a web page's ?room=), and whether the
    // page is served over https (a typed "host" then means wss://host/vnet).
    static void setRelayDefaults(const std::string& relay, const std::string& room, bool pageIsSecure);
    // Called around the settings file (they get its path): `beforeLoad` runs just before it is read (the web build copies the
    // saved text from localStorage into the file then), `afterSave` just after it was written (and copies it back).
    static void setPrefsSyncHooks(std::function<void(const std::string&)> beforeLoad, std::function<void(const std::string&)> afterSave);
    // Where the settings file lives; empty (the default) means nothing is read or written.
    static void setPrefsPath(const std::string& path);
    // The same, found only when the file is first needed (the game asks SDL for its preferences folder, which creates it:
    // a player who never opens an online page should not get an empty folder).
    static void setPrefsPathProvider(std::function<std::string()> provider);
    // Writes what the last successful host or join used (name, port or address, local players).
    static void rememberHost(const std::string& name, int port, int localPlayers);
    static void rememberJoin(const std::string& name, const std::string& address, int localPlayers);
    static void rememberRelay(const std::string& name, const std::string& relay, const std::string& room, int localPlayers);
    void ProcessEvent(Rml::Event& event, const Rml::String& value) override;
};

#endif
