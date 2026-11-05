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
  int teamSlot = -1;            // host: cycle the team of this slot
  int kickSlot = -1;            // host: remove the peer that plays this slot
  int setting = -1;             // host: cycle a setting (NetGame::HostSetting)
  // The forms.
  bool hostGo = false;
  bool joinGo = false;
  struct Form
  {
    std::string name;
    std::string address;        // join: as typed
    int port = 7777;            // host
    int localPlayers = 1;
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
    // Where the settings file lives; empty (the default) means nothing is read or written.
    static void setPrefsPath(const std::string& path);
    // The same, found only when the file is first needed (the game asks SDL for its preferences folder, which creates it:
    // a player who never opens an online page should not get an empty folder).
    static void setPrefsPathProvider(std::function<std::string()> provider);
    // Writes what the last successful host or join used (name, port or address, local players).
    static void rememberHost(const std::string& name, int port, int localPlayers);
    static void rememberJoin(const std::string& name, const std::string& address, int localPlayers);
    // Brief 13: the reconnect token the host at `host` (as typed) issued; and the one saved for that host, or 0.
    static void rememberToken(const std::string& host, uint32_t token);
    static uint32_t savedToken(const std::string& host);
    void ProcessEvent(Rml::Event& event, const Rml::String& value) override;
};

#endif
