#include "EventHandlerNetLobby.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include "MickLogger.h"
#include "net/LobbyModel.h"

const char* const EventHandlerNetLobby::RML_FILENAME = "netlobby.rml";
const char* const EventHandlerNetForm::HOST_FILENAME = "nethost.rml";
const char* const EventHandlerNetForm::JOIN_FILENAME = "netjoin.rml";
const char* const EventHandlerNetForm::RELAY_HOST_FILENAME = "nethostrelay.rml";
const char* const EventHandlerNetForm::RELAY_JOIN_FILENAME = "netjoinrelay.rml";

NetUiRequests& EventHandlerNetLobby::requests()
{
  static NetUiRequests r;
  return r;
}

void EventHandlerNetLobby::ProcessEvent(Rml::Event&, const Rml::String& value)
{
  NetUiRequests& rq = requests();
  if (value == "netstart")
  {
    rq.startMatch = true;
  }
  else if (value == "netleave")
  {
    rq.leave = true;
  }
  else if (value == "netcopy")
  {
    rq.copyRoom = true;
  }
  else if (value.rfind("netteam ", 0) == 0)
  {
    rq.teamSlot = std::atoi(value.c_str() + 8);
  }
  else if (value.rfind("netkick ", 0) == 0)
  {
    rq.kickSlot = std::atoi(value.c_str() + 8);
  }
  else if (value.rfind("netset ", 0) == 0)
  {
    const std::string which = value.substr(7);
    // The numbers are NetGame::HostSetting (kept in this order; GameEngine static_asserts nothing, the lobby test clicks each).
    rq.setting = which == "mapw" ? 0 : which == "maph" ? 1 : which == "blocks" ? 2 : which == "spawner" ? 3 : which == "slots" ? 4 : -1;
  }
  else
  {
    std::MickLogger::getInstance()->debug(this, std::string("netlobby: unknown command ").append(value));
  }
}

// -- the forms ---------------------------------------------------------------------------------------

namespace
{
std::string g_prefsPath;
std::function<std::string()> g_prefsPathProvider;
netplay::NetPrefs g_prefs;
bool g_prefsLoaded = false;
int g_localPlayers = 1;   // the "Players here" button of the open form
std::string g_defaultRelay;
std::string g_defaultRoom;
bool g_pageSecure = false;
bool g_relayExplicit = false;
std::function<void(const std::string&)> g_beforeLoad;
std::function<void(const std::string&)> g_afterSave;

// The file's path: set directly, or asked of the provider the first time it is needed.
const std::string& prefsPath()
{
  if (g_prefsPath.empty() && g_prefsPathProvider)
  {
    g_prefsPath = g_prefsPathProvider();
    g_prefsPathProvider = nullptr;
  }
  return g_prefsPath;
}

netplay::NetPrefs& prefs()
{
  if (!g_prefsLoaded)
  {
    g_prefsLoaded = true;
    if (!prefsPath().empty())
    {
      if (g_beforeLoad) g_beforeLoad(prefsPath());
      netplay::loadNetPrefs(prefsPath(), g_prefs);   // a missing or corrupt file leaves the defaults
    }
  }
  return g_prefs;
}

void savePrefs()
{
  if (!prefsPath().empty() && !netplay::saveNetPrefs(prefsPath(), g_prefs))
  {
    std::MickLogger::getInstance()->warn(nullptr, std::string("Could not save ").append(prefsPath()));
  }
  else if (g_afterSave)
  {
    g_afterSave(prefsPath());
  }
}

std::string valueOf(Rml::ElementDocument* doc, const char* id)
{
  if (Rml::Element* e = doc->GetElementById(id))
  {
    if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(e))
    {
      return input->GetValue();
    }
  }
  return std::string();
}

void setValue(Rml::ElementDocument* doc, const char* id, const std::string& value)
{
  if (Rml::Element* e = doc->GetElementById(id))
  {
    if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(e))
    {
      input->SetValue(value);
    }
  }
}

void showError(Rml::ElementDocument* doc, const std::string& text)
{
  if (Rml::Element* e = doc->GetElementById("formerror"))
  {
    e->SetInnerRML(text);   // plain sentences from LobbyModel: no markup characters in them
  }
}
// The note under the host form names the port in the field, as typed (what is typed is only ever shown as text).
void setPortNote(Rml::ElementDocument* doc, const std::string& typed)
{
  std::string shown;
  for (char c : typed)
  {
    if (c >= '0' && c <= '9') shown += c;
  }
  if (shown.empty()) shown = "?";
  for (const char* id : {"portnote"})
  {
    if (Rml::Element* e = doc->GetElementById(id)) e->SetInnerRML(shown);
  }
}
} // namespace

void EventHandlerNetForm::setPrefsPath(const std::string& path)
{
  g_prefsPath = path;
  g_prefsPathProvider = nullptr;
  g_prefsLoaded = false;
  g_prefs = netplay::NetPrefs();
}

void EventHandlerNetForm::setPrefsPathProvider(std::function<std::string()> provider)
{
  g_prefsPath.clear();
  g_prefsPathProvider = std::move(provider);
  g_prefsLoaded = false;
  g_prefs = netplay::NetPrefs();
}

void EventHandlerNetForm::rememberHost(const std::string& name, int port, int localPlayers)
{
  netplay::NetPrefs& p = prefs();
  p.name = name;
  p.hostPort = port;
  p.localPlayers = localPlayers;
  savePrefs();
}

void EventHandlerNetForm::setRelayDefaults(const std::string& relay, const std::string& room, bool pageIsSecure, bool relayIsExplicit)
{
  g_relayExplicit = relayIsExplicit;
  g_defaultRelay = relay;
  g_defaultRoom = room;
  g_pageSecure = pageIsSecure;
}

void EventHandlerNetForm::setPrefsSyncHooks(std::function<void(const std::string&)> beforeLoad, std::function<void(const std::string&)> afterSave)
{
  g_beforeLoad = std::move(beforeLoad);
  g_afterSave = std::move(afterSave);
}

void EventHandlerNetForm::rememberRelay(const std::string& name, const std::string& relay, const std::string& room, int localPlayers)
{
  netplay::NetPrefs& p = prefs();
  p.name = name;
  const std::string pageRelay = !g_defaultRelay.empty() ? g_defaultRelay : netplay::defaultRelayAddress();
  p.relay = relay == pageRelay ? std::string() : relay;   // the default is not a choice: do not pin the visitor to it
  if (!room.empty()) p.room = room;
  p.localPlayers = localPlayers;
  savePrefs();
}

void EventHandlerNetForm::rememberJoin(const std::string& name, const std::string& address, int localPlayers)
{
  netplay::NetPrefs& p = prefs();
  p.name = name;
  p.joinAddress = address;
  p.localPlayers = localPlayers;
  savePrefs();
}

void EventHandlerNetForm::ProcessEvent(Rml::Event& event, const Rml::String& value)
{
  Rml::ElementDocument* doc = event.GetTargetElement()->GetOwnerDocument();
  NetUiRequests& rq = EventHandlerNetLobby::requests();
  if (value == "netforminit")
  {
    const netplay::NetPrefs& p = prefs();
    g_localPlayers = p.localPlayers;
    setValue(doc, "f-name", p.name);
    setValue(doc, "f-port", std::to_string(p.hostPort));
    setValue(doc, "f-addr", p.joinAddress);
    const std::string pageRelay = !g_defaultRelay.empty() ? g_defaultRelay : netplay::defaultRelayAddress();
    setValue(doc, "f-relay", g_relayExplicit ? pageRelay : (!p.relay.empty() ? p.relay : pageRelay));
    setValue(doc, "f-room", !g_defaultRoom.empty() ? g_defaultRoom : p.room);
    if (Rml::Element* b = doc->GetElementById("f-local"))
    {
      b->SetInnerRML(std::to_string(g_localPlayers));
    }
    setPortNote(doc, valueOf(doc, "f-port"));
  }
  else if (value == "netport")
  {
    setPortNote(doc, valueOf(doc, "f-port"));
  }
  else if (value == "netlocal")
  {
    g_localPlayers = g_localPlayers >= 4 ? 1 : g_localPlayers + 1;   // the four keyboard sets; a pad joins in the round
    if (Rml::Element* b = doc->GetElementById("f-local"))
    {
      b->SetInnerRML(std::to_string(g_localPlayers));
    }
  }
  else if (value == "nethostgo")
  {
    int port = 0;
    std::string error;
    if (!netplay::parsePort(valueOf(doc, "f-port"), &port, &error))
    {
      showError(doc, error);
      return;
    }
    rq.form.name = netplay::cleanPlayerName(valueOf(doc, "f-name"));
    rq.form.port = port;
    rq.form.localPlayers = g_localPlayers;
    rq.hostGo = true;
  }
  else if (value == "netrelayhostgo" || value == "netrelayjoingo")
  {
    const bool joining = value == "netrelayjoingo";
    std::string error, normalised;
    netplay::RelayTarget target;
    if (!netplay::parseRelayAddress(valueOf(doc, "f-relay"), g_pageSecure, &target, &error, &normalised))
    {
      showError(doc, error);
      return;
    }
    std::string room;
    if (joining && !netplay::parseRoomCode(valueOf(doc, "f-room"), &room, &error))
    {
      showError(doc, error);
      return;
    }
    rq.form.name = netplay::cleanPlayerName(valueOf(doc, "f-name"));
    rq.form.relay = normalised;
    rq.form.relayWs = target.ws;
    rq.form.relayUdp = target.udp;
    rq.form.room = room;
    rq.form.localPlayers = g_localPlayers;
    (joining ? rq.relayJoinGo : rq.relayHostGo) = true;
  }
  else if (value == "netjoingo")
  {
    std::string error, normalised;
    if (!netplay::parseHostPort(valueOf(doc, "f-addr"), 7777, nullptr, nullptr, &error, &normalised))
    {
      showError(doc, error);
      return;
    }
    rq.form.name = netplay::cleanPlayerName(valueOf(doc, "f-name"));
    rq.form.address = normalised;
    rq.form.localPlayers = g_localPlayers;
    rq.joinGo = true;
  }
  else
  {
    std::MickLogger::getInstance()->debug(this, std::string("netform: unknown command ").append(value));
  }
}
