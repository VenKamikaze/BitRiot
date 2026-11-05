// The online screens' layout and binders (brief 08), asserted headlessly on the game's real .rml/.rcss through the
// RmlLayoutHarness: the lobby's slot table lines up for 2 to 6 seats, long and UTF-8 names are cut without overflowing,
// the host's buttons exist only for the host, the error page and the host and join forms fit the window, and the model's
// rows reach the right cells. Geometry only: that it looks good is not something this can say.

#include "support/RmlLayoutHarness.h"

#include <cmath>
#include <string>
#include <vector>

#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/binders/NetLobbyBinder.h"
#include "net/LobbyModel.h"

namespace bitriot_test
{
namespace
{
using namespace netplay;

const float TOLERANCE_PX = 1.0f;
const char* const CELLS[] = {"num", "name", "team", "ready", "ping"};

LobbyEntry peerEntry(uint8_t id, const std::string& name, uint8_t mask, int rtt, bool ready)
{
  LobbyEntry e;
  e.id = id;
  e.name = name;
  e.slotMask = mask;
  e.rttMs = rtt;
  e.ready = ready;
  return e;
}

// A view with `seats` seats: the host first, then remote players (some with long or odd names), then computer players.
LobbyView viewWithSeats(int seats, bool longNames)
{
  LobbyView v;
  v.phase = PHASE_LOBBY;
  v.round = 1;
  v.settings.mapWidth = 17;
  v.settings.mapHeight = seats > 4 ? 19 : 13;
  v.settings.blockPercentage = 55;
  v.settings.npcSpawner = true;
  v.settings.totalSlots = seats;
  const std::string hostName = longNames ? "A very long host name indeed, over the limit" : "Alice";
  const std::string bobName = longNames ? "\xce\xb1\xce\xb2\xce\xb3\xce\xb4\xce\xb5\xce\xb6\xce\xb7\xce\xb8\xce\xb9\xce\xba\xce\xbb\xce\xbc\xce\xbd\xce\xbe\xce\xbf\xcf\x80\xcf\x81\xcf\x83\xcf\x84\xcf\x85" : "Bob";
  const std::string carolName = longNames ? "\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93\xe2\x9c\x93" : "Carol";
  const uint8_t owners[6] = {0, 1, 2, NO_PEER, NO_PEER, NO_PEER};
  for (int i = 0; i < seats; i++)
  {
    SlotInfo s;
    s.owner = owners[i];
    s.team = (uint8_t) i;
    v.slots.push_back(s);
  }
  v.peers.push_back(peerEntry(0, hostName, 0b1, -1, true));
  if (seats >= 2) v.peers.push_back(peerEntry(1, bobName, 0b10, 120, true));
  if (seats >= 3) v.peers.push_back(peerEntry(2, carolName, 0b100, 1234, false));
  return v;
}

class LobbyLayoutTest : public RmlLayoutTest
{
  protected:
    Rml::ElementDocument* lobby(const LobbyView& view, bool isHost, uint8_t myId, bool canStart = false)
    {
      Rml::ElementDocument* doc = loadDocument("netlobby.rml");
      if (doc == nullptr)
      {
        return nullptr;
      }
      LobbyContext ctx;
      ctx.myId = myId;
      ctx.isHost = isHost;
      ctx.canStart = canStart;
      NetLobbyBinder::applyLobby(doc, buildLobbyScreen(view, ctx));
      updateLayout();
      return doc;
    }

    Rml::Element* cell(Rml::ElementDocument* doc, int slot, const char* name)
    {
      return doc->GetElementById(NetLobbyBinder::cellId(slot, name));
    }

    // The header cell of a column: the head row's cells are in document order after the row's own class.
    CellBox headerCell(Rml::ElementDocument* doc, const char* which)
    {
      Rml::Element* head = findByClass(doc, "head");
      EXPECT_NE(head, nullptr);
      const std::string cls = std::string("c-") + which;
      return boxOf(findByClass(head, cls));
    }

    // Laid out and displayed (an element that was hidden after a layout keeps its old box, so the box is no evidence).
    static bool shown(Rml::Element* e)
    {
      return e != nullptr && MickRmlUIElementUtil::isDisplayed(e);
    }

    static const char* columnClass(const char* cellName)
    {
      return std::string(cellName) == "num" ? "slot" : cellName;
    }
};
} // namespace

TEST_F(LobbyLayoutTest, TheLobbyDocumentLoadsAndLaysOut)
{
  Rml::ElementDocument* doc = lobby(viewWithSeats(4, false), true, 0, true);
  ASSERT_NE(doc, nullptr) << "netlobby.rml failed to load";
  EXPECT_GT(doc->GetBox().GetSize(Rml::Box::BORDER).x, 0.0f);
}

// The table lines up: every row's cells start where the header's cells do and have the same width, for every number of
// seats, in both views, with plain and with very long names.
TEST_F(LobbyLayoutTest, TheSlotTableLinesUpForTwoToSixSeats)
{
  for (const bool longNames : {false, true})
  {
    for (const bool host : {true, false})
    {
      for (int seats = 2; seats <= 6; seats++)
      {
        SCOPED_TRACE(std::string(host ? "host" : "peer") + (longNames ? " long names, " : " short names, ") + std::to_string(seats) + " seats");
        // Fresh context per case: no leftovers.
        Rml::ElementDocument* doc = lobby(viewWithSeats(seats, longNames), host, host ? 0 : 1, true);
        ASSERT_NE(doc, nullptr);
        for (const char* name : CELLS)
        {
          const CellBox head = headerCell(doc, columnClass(name));
          ASSERT_GT(head.width, 0.0f) << name;
          for (int slot = 0; slot < 6; slot++)
          {
            Rml::Element* c = cell(doc, slot, name);
            ASSERT_NE(c, nullptr) << name << slot;
            // The team column holds a button for the host: the cell's own parent is the column.
            Rml::Element* column = std::string(name) == "team" ? c->GetParentNode() : c;
            const CellBox box = boxOf(column);
            EXPECT_NEAR(box.left, head.left, TOLERANCE_PX) << name << " of row " << slot << " is not under its heading";
            EXPECT_NEAR(box.width, head.width, TOLERANCE_PX) << name << " of row " << slot;
          }
        }
      }
    }
  }
}

TEST_F(LobbyLayoutTest, RowsStackWithoutOverlapAndWithoutWrappingAndTheWholePageFitsTheWindow)
{
  for (const bool longNames : {false, true})
  {
    for (int seats = 2; seats <= 6; seats++)
    {
      SCOPED_TRACE(std::string(longNames ? "long names, " : "short names, ") + std::to_string(seats) + " seats");
      Rml::ElementDocument* doc = lobby(viewWithSeats(seats, longNames), true, 0, true);
      ASSERT_NE(doc, nullptr);
      float previousBottom = boxOf(findByClass(doc, "head")).top + boxOf(findByClass(doc, "head")).height;
      float rowHeight = -1;
      for (int slot = 0; slot < 6; slot++)
      {
        const CellBox row = boxOf(doc->GetElementById("row-" + std::to_string(slot)));
        EXPECT_GE(row.top, previousBottom - TOLERANCE_PX) << "row " << slot << " overlaps the one above";
        if (rowHeight < 0) rowHeight = row.height;
        EXPECT_NEAR(row.height, rowHeight, TOLERANCE_PX) << "row " << slot << " is taller than the first (a cell wrapped)";
        previousBottom = row.top + row.height;
        // No cell is wider than its row.
        for (const char* name : CELLS)
        {
          const CellBox c = boxOf(cell(doc, slot, name));
          EXPECT_LE(c.left + c.width, row.left + row.width + TOLERANCE_PX) << name << slot;
        }
      }
      const CellBox start = boxOf(doc->GetElementById("netstart"));
      const CellBox leave = boxOf(doc->GetElementById("netleave"));
      EXPECT_LE(start.top + start.height, (float) CONTEXT_HEIGHT) << "the Start button is below the window";
      EXPECT_LE(leave.top + leave.height, (float) CONTEXT_HEIGHT);
      EXPECT_GE(previousBottom, 0.0f);
      EXPECT_LE(previousBottom, start.top + TOLERANCE_PX) << "the table runs into the buttons";
      EXPECT_LE(leave.left + leave.width, (float) CONTEXT_WIDTH);
    }
  }
}

// Long names are cut by the model; this checks that what is left really fits its cell (and so that the cut is what
// keeps it there: an unclipped 25-character name would not).
TEST_F(LobbyLayoutTest, LongAndUtf8NamesDoNotOverflowTheirCells)
{
  Rml::ElementDocument* doc = lobby(viewWithSeats(6, true), true, 0, true);
  ASSERT_NE(doc, nullptr);
  for (int slot = 0; slot < 6; slot++)
  {
    Rml::Element* name = cell(doc, slot, "name");
    ASSERT_NE(name, nullptr);
    EXPECT_LE(name->GetScrollWidth(), name->GetClientWidth() + TOLERANCE_PX) << "the name of row " << slot << " overflows: " << name->GetInnerRML();
  }
  // And without the model's clipping the same name does overflow, so the check above is not vacuous.
  Rml::Element* name = cell(doc, 1, "name");
  name->SetInnerRML(std::string(60, 'W'));
  updateLayout();
  EXPECT_GT(name->GetScrollWidth(), name->GetClientWidth() + TOLERANCE_PX);
}

TEST_F(LobbyLayoutTest, OnlyTheHostHasTeamSettingAndKickButtons)
{
  const LobbyView view = viewWithSeats(4, false);
  Rml::ElementDocument* host = lobby(view, true, 0, true);
  ASSERT_NE(host, nullptr);
  // Team buttons on every seat of the match, none on open seats; kick on the peers' seats only.
  for (int slot = 0; slot < 6; slot++)
  {
    const bool seat = slot < 4;
    Rml::Element* team = cell(host, slot, "team");
    EXPECT_EQ(shown(team), seat) << "team button " << slot;
    Rml::Element* kick = cell(host, slot, "kick");
    const bool peerSeat = slot == 1 || slot == 2;
    EXPECT_EQ(shown(kick), peerSeat) << "kick button " << slot;
  }
  EXPECT_TRUE(shown(host->GetElementById("settingsbuttons")));
  EXPECT_GT(boxOf(host->GetElementById("settingsbuttons")).width, 0.0f);
  const CellBox settings = boxOf(host->GetElementById("settingsbuttons"));
  EXPECT_LE(settings.left + settings.width, (float) CONTEXT_WIDTH) << "the five setting buttons run off the window";

  Rml::ElementDocument* peer = lobby(view, false, 1);
  ASSERT_NE(peer, nullptr);
  for (int slot = 0; slot < 6; slot++)
  {
    EXPECT_FALSE(shown(cell(peer, slot, "team"))) << "a peer has no team button " << slot;
    EXPECT_FALSE(shown(cell(peer, slot, "kick"))) << "a peer has no kick button " << slot;
  }
  EXPECT_FALSE(shown(peer->GetElementById("settingsbuttons")));
  EXPECT_TRUE(shown(peer->GetElementById("settingstext")));
  EXPECT_GT(boxOf(peer->GetElementById("settingstext")).width, 0.0f);
}

// -- the binder: the model's rows reach the right cells ---------------------------------------------------------------

TEST_F(LobbyLayoutTest, TheBinderPutsEachRowsFlagsInItsCells)
{
  const LobbyView view = viewWithSeats(4, false);
  Rml::ElementDocument* doc = lobby(view, true, 0, true);
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(cell(doc, 0, "name")->GetInnerRML(), "Alice (you)");
  EXPECT_EQ(cell(doc, 1, "name")->GetInnerRML(), "Bob");
  EXPECT_EQ(cell(doc, 3, "name")->GetInnerRML(), "Computer");
  EXPECT_EQ(cell(doc, 4, "name")->GetInnerRML(), "open");
  EXPECT_EQ(cell(doc, 0, "ready")->GetInnerRML(), "host");
  EXPECT_EQ(cell(doc, 1, "ready")->GetInnerRML(), "ready");
  EXPECT_TRUE(cell(doc, 1, "ready")->IsClassSet("ok"));
  EXPECT_EQ(cell(doc, 2, "ready")->GetInnerRML(), "waiting");
  EXPECT_TRUE(cell(doc, 2, "ready")->IsClassSet("wait"));
  EXPECT_EQ(cell(doc, 3, "ready")->GetInnerRML(), "-");
  EXPECT_EQ(cell(doc, 1, "ping")->GetInnerRML(), "120 ms");
  EXPECT_EQ(cell(doc, 0, "ping")->GetInnerRML(), "");
  EXPECT_EQ(cell(doc, 1, "team")->GetInnerRML(), "Team 2");
  EXPECT_TRUE(cell(doc, 1, "team")->IsClassSet("team1"));
  EXPECT_EQ(cell(doc, 4, "team")->GetInnerRML(), "");
  EXPECT_TRUE(doc->GetElementById("row-0")->IsClassSet("mine"));
  EXPECT_FALSE(doc->GetElementById("row-1")->IsClassSet("mine"));
  EXPECT_TRUE(doc->GetElementById("row-4")->IsClassSet("open"));
  EXPECT_TRUE(doc->GetElementById("row-0")->IsClassSet("editable"));
  EXPECT_TRUE(doc->GetElementById("row-1")->IsClassSet("kickable"));
  EXPECT_FALSE(doc->GetElementById("row-0")->IsClassSet("kickable"));
  EXPECT_EQ(doc->GetElementById("nettitle")->GetInnerRML(), "Hosting a network game");
  EXPECT_EQ(doc->GetElementById("netstart")->GetInnerRML(), "Start match");
  EXPECT_FALSE(doc->GetElementById("netstart")->IsClassSet("disabled"));
  EXPECT_EQ(doc->GetElementById("set-mapw")->GetInnerRML(), "Width 17");
  EXPECT_EQ(doc->GetElementById("set-slots")->GetInnerRML(), "Players 4");

  // The same lobby when the host may not start yet: the Start button looks disabled.
  doc = lobby(view, true, 0, false);
  EXPECT_TRUE(doc->GetElementById("netstart")->IsClassSet("disabled"));
}

TEST_F(LobbyLayoutTest, APeersButtonIsReadyOrNotReadyAndHiddenWhenThereIsNothingToPress)
{
  const LobbyView view = viewWithSeats(3, false);
  Rml::ElementDocument* doc = loadDocument("netlobby.rml");
  ASSERT_NE(doc, nullptr);
  LobbyContext ctx;
  ctx.myId = 2;
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(view, ctx));
  EXPECT_EQ(doc->GetElementById("netstart")->GetInnerRML(), "Ready");
  ctx.myReady = true;
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(view, ctx));
  EXPECT_EQ(doc->GetElementById("netstart")->GetInnerRML(), "Not ready");
  ctx.inLobby = false;
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(view, ctx));
  updateLayout();
  EXPECT_EQ(doc->GetElementById("netstart")->GetInnerRML(), "Waiting for the host");
  EXPECT_FALSE(shown(doc->GetElementById("netstart"))) << "nothing to press";
  EXPECT_TRUE(doc->GetElementById("netstart")->IsClassSet("disabled"));
}

TEST_F(LobbyLayoutTest, NamesAreEscapedAndNeverBecomeMarkup)
{
  LobbyView view = viewWithSeats(2, false);
  view.peers[1].name = "<b>x</b>&amp;";
  Rml::ElementDocument* doc = lobby(view, true, 0, true);
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(cell(doc, 1, "name")->GetInnerRML(), "&lt;b&gt;x&lt;/b&gt;&amp;amp;");
  EXPECT_EQ(cell(doc, 1, "name")->GetNumChildren(), 1) << "one text node, no element made from a name";
}

// -- the error page, connecting, and the forms --------------------------------------------------------------------

TEST_F(LobbyLayoutTest, EveryErrorPageFitsTheWindowWithItsButton)
{
  Identity mine;
  mine.buildId = 0x1111111111111111ull;
  mine.dataHash = 0x2222222222222222ull;
  RejectInfo rj;
  rj.hostProtocol = 3;
  rj.hostBuildId = 0xAAAAAAAAAAAAAAAAull;
  rj.hostDataHash = 0xBBBBBBBBBBBBBBBBull;
  std::vector<EndMessage> messages;
  for (uint8_t reason : {REJECT_VERSION, REJECT_BUILD, REJECT_DATA, REJECT_FULL, REJECT_IN_PROGRESS, REJECT_BAD_REQUEST})
  {
    rj.reason = reason;
    rj.text = std::string(300, 'x');
    messages.push_back(describeEnd(LockstepSession::END_REJECTED, rj, "", mine, "192.168.100.200:7777"));
  }
  for (LockstepSession::EndReason r : {LockstepSession::END_CONNECT_TIMEOUT, LockstepSession::END_HOST_LOST, LockstepSession::END_HOST_CLOSED,
                                       LockstepSession::END_KICKED, LockstepSession::END_REMOVED, LockstepSession::END_DESYNC})
  {
    messages.push_back(describeEnd(r, rj, "the host stopped the match: the game state diverged", mine, "192.168.100.200:7777"));
  }
  int index = 0;
  for (const EndMessage& m : messages)
  {
    SCOPED_TRACE("message " + std::to_string(index++) + ": " + m.title);
    Rml::ElementDocument* doc = loadDocument("netlobby.rml");
    ASSERT_NE(doc, nullptr);
    NetLobbyBinder::applyEnd(doc, m, "Back");
    updateLayout();
    EXPECT_EQ(doc->GetElementById("nettitle")->GetInnerRML(), NetLobbyBinder::toRml(m.title));
    EXPECT_FALSE(shown(doc->GetElementById("lobbyview"))) << "the table is hidden";
    const CellBox panel = boxOf(doc->GetElementById("endview"));
    const CellBox button = boxOf(doc->GetElementById("netleave"));
    EXPECT_EQ(doc->GetElementById("netleave")->GetInnerRML(), "Back");
    EXPECT_GT(panel.height, 0.0f);
    EXPECT_LE(panel.left + panel.width, (float) CONTEXT_WIDTH);
    EXPECT_LE(button.top + button.height, (float) CONTEXT_HEIGHT) << "the Back button is below the window: the message is too long to read";
    EXPECT_LE(panel.top + panel.height, button.top + TOLERANCE_PX);
    EXPECT_FALSE(shown(doc->GetElementById("netstart"))) << "no Start on an error page";
  }
}

TEST_F(LobbyLayoutTest, ConnectingShowsOnlyACancelButton)
{
  Rml::ElementDocument* doc = loadDocument("netlobby.rml");
  ASSERT_NE(doc, nullptr);
  NetLobbyBinder::applyConnecting(doc, "Connecting...", "Looking up example.org:7777...");
  updateLayout();
  EXPECT_EQ(doc->GetElementById("netleave")->GetInnerRML(), "Cancel");
  EXPECT_FALSE(shown(doc->GetElementById("netstart")));
  EXPECT_NE(doc->GetElementById("enderrors")->GetInnerRML().find("example.org:7777"), std::string::npos);
}

TEST_F(LobbyLayoutTest, TheHostAndJoinFormsKeepEveryFieldOnItsRowAndInsideTheWindow)
{
  for (const char* file : {"nethost.rml", "netjoin.rml"})
  {
    SCOPED_TRACE(file);
    Rml::ElementDocument* doc = loadDocument(file);
    ASSERT_NE(doc, nullptr);
    std::vector<Rml::Element*> rows = findAllByClass(doc, "frow");
    ASSERT_GE(rows.size(), 3u);
    for (Rml::Element* row : rows)
    {
      const CellBox r = boxOf(row);
      EXPECT_NEAR(r.height, boxOf(rows[0]).height, TOLERANCE_PX) << "a field wrapped onto a second line";
      for (int i = 0; i < row->GetNumChildren(); i++)
      {
        const CellBox c = boxOf(row->GetChild(i));
        EXPECT_LE(c.left + c.width, r.left + r.width + TOLERANCE_PX) << "a field runs out of its row";
        EXPECT_GE(c.top, r.top - TOLERANCE_PX) << "a field sits above its row";
        EXPECT_LT(c.top, r.top + r.height - 1.0f) << "a field wrapped below its row (it is not on its label's line)";
      }
    }
    const CellBox go = boxOf(doc->GetElementById("formgo"));
    const CellBox back = boxOf(doc->GetElementById("formback"));
    EXPECT_LE(go.top + go.height, (float) CONTEXT_HEIGHT);
    EXPECT_LE(back.left + back.width, (float) CONTEXT_WIDTH);
    EXPECT_GT(boxOf(doc->GetElementById("f-name")).width, 100.0f);
  }
}

TEST_F(LobbyLayoutTest, TheOverlayLoadsAndItsPiecesStayInsideTheWindow)
{
  Rml::ElementDocument* doc = loadDocument("netoverlay.rml");
  ASSERT_NE(doc, nullptr);
  doc->GetElementById("stallbox")->SetProperty("display", "block");
  doc->GetElementById("stallbox")->SetInnerRML("Waiting for a player with quite a long name...");
  doc->GetElementById("toasts")->SetInnerRML("<p>Bob left - now AI</p><br /><p>Carol left - now AI</p><br />");
  doc->GetElementById("netstatusline")->SetInnerRML("NET peer D=5 ping 123 ms - waiting for host");
  updateLayout();
  const CellBox stall = boxOf(doc->GetElementById("stallbox"));
  EXPECT_GT(stall.width, 0.0f);
  EXPECT_LE(stall.left + stall.width, (float) CONTEXT_WIDTH);
  const CellBox status = boxOf(doc->GetElementById("netstatusline"));
  EXPECT_GT(status.width, 0.0f);
  EXPECT_LE(status.top + status.height, (float) CONTEXT_HEIGHT);
  EXPECT_GT(status.top, (float) CONTEXT_HEIGHT / 2) << "the status line is at the bottom";
  const CellBox toasts = boxOf(doc->GetElementById("toasts"));
  EXPECT_GT(toasts.height, 0.0f);
  EXPECT_EQ(doc->GetNumChildren(), 4);
}
} // namespace bitriot_test
