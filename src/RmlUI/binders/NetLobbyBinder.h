/*
 * NetLobbyBinder.h
 *
 * Pastes the lobby model (src/net/LobbyModel) into assets/menu/netlobby.rml (brief 08). The
 * document has six fixed rows, so a frame that changes a ping or a ready mark changes text and
 * classes only: no element is rebuilt and a focused button keeps its focus. Every call is
 * idempotent and cheap when nothing changed (a string is set only if it differs).
 */
#ifndef NETLOBBYBINDER_H
#define NETLOBBYBINDER_H

#include <RmlUi/Core/ElementDocument.h>
#include <string>

#include "net/LobbyModel.h"

class NetLobbyBinder
{
  public:
    // The lobby: title, status, settings, the table, Start / Ready and Leave.
    static void applyLobby(Rml::ElementDocument* doc, const netplay::LobbyScreen& screen);
    // "Connecting to ..." (no table yet, only a Cancel button).
    static void applyConnecting(Rml::ElementDocument* doc, const std::string& title, const std::string& status);
    // The page for a session that ended or a join that failed: the headline and its lines, one button.
    static void applyEnd(Rml::ElementDocument* doc, const netplay::EndMessage& message, const std::string& buttonLabel);
    // Plain text as RML: escaped, newlines become <br />.
    static std::string toRml(const std::string& text);
    // The document id of row cells, for tests: "r<slot>-<cell>" with cell in num, name, team, teamtext, ready, ping, kick.
    static std::string cellId(int slot, const char* cell);

  private:
    static void setText(Rml::ElementDocument* doc, const std::string& id, const std::string& text);
    static void setDisplay(Rml::ElementDocument* doc, const std::string& id, const char* value);
    static void setClass(Rml::ElementDocument* doc, const std::string& id, const char* cls, bool on);
};

#endif
