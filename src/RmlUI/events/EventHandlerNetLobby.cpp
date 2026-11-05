#include "EventHandlerNetLobby.h"

#include "MickLogger.h"

const char* const EventHandlerNetLobby::RML_FILENAME = "netlobby.rml";

NetUiRequests& EventHandlerNetLobby::requests()
{
  static NetUiRequests r;
  return r;
}

void EventHandlerNetLobby::ProcessEvent(Rml::Event&, const Rml::String& value)
{
  if (value == "netstart")
  {
    requests().startMatch = true;
  }
  else if (value == "netleave")
  {
    requests().leave = true;
  }
  else
  {
    std::MickLogger::getInstance()->debug(this, std::string("netlobby: unknown command ").append(value));
  }
}
