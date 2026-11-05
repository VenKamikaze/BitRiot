#include "NetLobbyBinder.h"

#include <RmlUi/Core/Element.h>

std::string NetLobbyBinder::toRml(const std::string& text)
{
  std::string out;
  for (char c : text)
  {
    switch (c)
    {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\n': out += "<br />"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string NetLobbyBinder::cellId(int slot, const char* cell)
{
  return "r" + std::to_string(slot) + "-" + cell;
}

void NetLobbyBinder::setText(Rml::ElementDocument* doc, const std::string& id, const std::string& text)
{
  if (Rml::Element* e = doc->GetElementById(id))
  {
    const std::string rml = toRml(text);
    if (e->GetInnerRML() != rml)
    {
      e->SetInnerRML(rml);
    }
  }
}

void NetLobbyBinder::setDisplay(Rml::ElementDocument* doc, const std::string& id, const char* value)
{
  if (Rml::Element* e = doc->GetElementById(id))
  {
    // The value last set is kept in an attribute: a property read back is a keyword, not the string put in.
    if (e->GetAttribute<Rml::String>("shown", "") != value)
    {
      e->SetAttribute("shown", Rml::String(value));
      e->SetProperty("display", value);
    }
  }
}

void NetLobbyBinder::setClass(Rml::ElementDocument* doc, const std::string& id, const char* cls, bool on)
{
  if (Rml::Element* e = doc->GetElementById(id))
  {
    if (e->IsClassSet(cls) != on)
    {
      e->SetClass(cls, on);
    }
  }
}

void NetLobbyBinder::applyLobby(Rml::ElementDocument* doc, const netplay::LobbyScreen& sc)
{
  if (!doc)
  {
    return;
  }
  using netplay::SlotKind;
  setDisplay(doc, "lobbyview", "block");
  setDisplay(doc, "endview", "none");
  setText(doc, "nettitle", sc.title);
  setText(doc, "netstatus", sc.status);

  // The settings: the host edits them with buttons, a peer reads them.
  setText(doc, "settingstext", sc.isHost ? std::string() : sc.settings);
  setDisplay(doc, "settingstext", sc.isHost ? "none" : "inline");
  setDisplay(doc, "settingsbuttons", sc.isHost ? "inline" : "none");
  if (sc.isHost)
  {
    static const char* const ids[5] = {"set-mapw", "set-maph", "set-blocks", "set-spawner", "set-slots"};
    for (int i = 0; i < 5; i++)
    {
      setText(doc, ids[i], sc.settingButtons[i]);
    }
  }

  for (const netplay::LobbyRow& r : sc.rows)
  {
    const std::string rowId = "row-" + std::to_string(r.slot);
    setClass(doc, rowId, "open", r.kind == SlotKind::OPEN);
    setClass(doc, rowId, "mine", r.mine);
    setClass(doc, rowId, "editable", r.teamEditable);
    setClass(doc, rowId, "kickable", r.kickable);
    setText(doc, cellId(r.slot, "num"), r.number);
    setText(doc, cellId(r.slot, "name"), r.name);

    const std::string team = r.team >= 0 ? "Team " + std::to_string(r.team + 1) : std::string();
    setText(doc, cellId(r.slot, "team"), team);
    setText(doc, cellId(r.slot, "teamtext"), team);
    for (int t = 0; t < 6; t++)
    {
      const std::string cls = "team" + std::to_string(t);
      setClass(doc, cellId(r.slot, "team"), cls.c_str(), r.team == t);
      setClass(doc, cellId(r.slot, "teamtext"), cls.c_str(), r.team == t);
    }

    std::string ready;
    if (r.kind == SlotKind::HOST) ready = "host";
    else if (r.showReady) ready = r.ready ? "ready" : "waiting";
    else if (r.kind == SlotKind::AI) ready = "-";
    setText(doc, cellId(r.slot, "ready"), ready);
    setClass(doc, cellId(r.slot, "ready"), "ok", r.showReady && r.ready);
    setClass(doc, cellId(r.slot, "ready"), "wait", r.showReady && !r.ready);
    setText(doc, cellId(r.slot, "ping"), r.pingMs >= 0 ? std::to_string(r.pingMs) + " ms" : std::string());
  }

  // Buttons: the host starts, a peer toggles ready.
  const bool primary = sc.isHost || sc.canToggleReady;
  setDisplay(doc, "netstart", primary ? "inline-block" : "none");
  setText(doc, "netstart", sc.startLabel);
  setClass(doc, "netstart", "disabled", sc.isHost ? !sc.canStart : !sc.canToggleReady);
  setText(doc, "netleave", "Leave");
}

void NetLobbyBinder::applyConnecting(Rml::ElementDocument* doc, const std::string& title, const std::string& status)
{
  if (!doc)
  {
    return;
  }
  setDisplay(doc, "lobbyview", "none");
  setDisplay(doc, "endview", "block");
  setText(doc, "nettitle", title);
  if (Rml::Element* e = doc->GetElementById("enderrors"))
  {
    const std::string rml = "<p>" + toRml(status) + "</p>";
    if (e->GetInnerRML() != rml)
    {
      e->SetInnerRML(rml);
    }
  }
  setDisplay(doc, "netstart", "none");
  setText(doc, "netleave", "Cancel");
}

void NetLobbyBinder::applyEnd(Rml::ElementDocument* doc, const netplay::EndMessage& m, const std::string& buttonLabel)
{
  if (!doc)
  {
    return;
  }
  setDisplay(doc, "lobbyview", "none");
  setDisplay(doc, "endview", "block");
  setText(doc, "nettitle", m.title);
  if (Rml::Element* e = doc->GetElementById("enderrors"))
  {
    std::string rml;
    for (size_t i = 0; i < m.lines.size(); i++)
    {
      rml += std::string("<p") + (i == 0 && m.isError ? " class=\"first\"" : "") + ">" + toRml(m.lines[i]) + "</p>";
    }
    if (e->GetInnerRML() != rml)
    {
      e->SetInnerRML(rml);
    }
  }
  setDisplay(doc, "netstart", "none");
  setText(doc, "netleave", buttonLabel);
}
