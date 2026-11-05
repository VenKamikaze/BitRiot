/*
 * EventHandlerNetLobby.h
 *
 * The buttons of the netplay lobby page (assets/menu/netlobby.rml, brief 07): "Start
 * match" (the host) and "Leave". They only raise flags; GameEngine reads them once a frame
 * (NetGame::uiRequests), because starting or leaving a session is not something to do from
 * inside an RmlUi event.
 */
#ifndef EVENTHANDLERNETLOBBY_H
#define EVENTHANDLERNETLOBBY_H

#include "EventHandler.h"

struct NetUiRequests
{
  bool startMatch = false;
  bool leave = false;
};

class EventHandlerNetLobby : public EventHandler
{
  public:
    static const char* const RML_FILENAME;
    static NetUiRequests& requests();
    void ProcessEvent(Rml::Event& event, const Rml::String& value) override;
};

#endif
