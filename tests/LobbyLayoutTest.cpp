// The online screens' layout and binders (brief 08), asserted headlessly on the game's real .rml/.rcss through the
// RmlLayoutHarness: the lobby's slot table lines up for 2 to 6 seats, long and UTF-8 names are cut without overflowing,
// the host's buttons exist only for the host, the error page and the host and join forms fit the window, and the model's
// rows reach the right cells. Geometry only: that it looks good is not something this can say.

#include "support/RmlLayoutHarness.h"

#include <cmath>
#include <string>
#include <vector>

#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "NetGame.h"
#include "RmlUI/MickRmlUIElementUtil.h"
#include "RmlUI/binders/NetLobbyBinder.h"
#include "RmlUI/events/EventHandlerNetLobby.h"
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
  for (const char* file : {"nethost.rml", "netjoin.rml", "nethostrelay.rml", "netjoinrelay.rml"})
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

TEST_F(LobbyLayoutTest, TheOnlineMenuListsTheFourWaysInAndFitsTheWindowWithEveryButtonOnItsOwnLine)
{
  Rml::ElementDocument* doc = loadDocument("netmenu.rml");
  ASSERT_NE(doc, nullptr);
  float lastBottom = -1.0f;
  for (const char* id : {"nethostrelay", "netjoinrelay", "nethost", "netjoin", "netback"})
  {
    Rml::Element* b = doc->GetElementById(id);
    ASSERT_NE(b, nullptr) << id;
    EXPECT_TRUE(shown(b)) << id;
    const CellBox box = boxOf(b);
    EXPECT_GE(box.top, lastBottom - TOLERANCE_PX) << id << " overlaps the button above it";
    lastBottom = box.top + box.height;
    EXPECT_LE(lastBottom, (float) CONTEXT_HEIGHT) << id << " is below the window";
    EXPECT_LE(box.left + box.width, (float) CONTEXT_WIDTH) << id;
  }
  // The tab order is the order on the page (Up and Down walk it).
  int previous = 0;
  for (const char* id : {"nethostrelay", "netjoinrelay", "nethost", "netjoin", "netback"})
  {
    const int tab = MickRmlUIElementUtil::getTabIndex(doc->GetElementById(id));
    EXPECT_GT(tab, previous) << id;
    previous = tab;
  }
  EXPECT_EQ(doc->GetElementById("netback")->GetAttribute<Rml::String>("escapeAction", ""), "1") << "Escape goes back";
}

TEST_F(LobbyLayoutTest, TheOverlayLoadsAndItsPiecesStayInsideTheWindow)
{
  Rml::ElementDocument* doc = loadDocument("netoverlay.rml");
  ASSERT_NE(doc, nullptr);
  doc->GetElementById("stallbox")->SetProperty("display", "block");
  doc->GetElementById("stallbox")->SetInnerRML("Waiting for a player with quite a long name...");
  doc->GetElementById("toasts")->SetInnerRML("<p>Bob left - now AI</p><br /><p>Carol left - now AI</p><br />");
  doc->GetElementById("netstatusline")->SetInnerRML("NET peer D=5 ping 123 ms - waiting for host");
  doc->GetElementById("joinbox")->SetProperty("display", "block");   // brief 13: a late joiner's banner
  doc->GetElementById("joinbox")->SetInnerRML("Joining... catching up 43%");
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
  const CellBox join = boxOf(doc->GetElementById("joinbox"));
  EXPECT_GT(join.width, 0.0f);
  EXPECT_GE(join.left, 0.0f);
  EXPECT_LE(join.left + join.width, (float) CONTEXT_WIDTH);
  EXPECT_LE(join.top + join.height, (float) CONTEXT_HEIGHT);
  EXPECT_EQ(doc->GetNumChildren(), 5);
}

// -- the buttons: what a press asks for ----------------------------------------------------------------------------

namespace
{
std::string scratchFile(const std::string& leaf)
{
  const char* dir = std::getenv("TMPDIR");
  return std::string(dir ? dir : "/tmp") + "/bitriot-lobbylayout-" + std::to_string((long) getpid()) + "-" + leaf;
}
}

class NetFormTest : public LobbyLayoutTest
{
  protected:
    void SetUp() override
    {
      LobbyLayoutTest::SetUp();
      EventHandlerNetLobby::requests() = NetUiRequests();
      EventHandlerNetForm::setPrefsPath("");   // no file unless a test names one
    }
    void TearDown() override
    {
      EventHandlerNetForm::setPrefsPath("");
      EventHandlerNetLobby::requests() = NetUiRequests();
      LobbyLayoutTest::TearDown();
    }
    static void type(Rml::ElementDocument* doc, const char* id, const std::string& value)
    {
      auto* input = dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById(id));
      ASSERT_NE(input, nullptr) << id;
      input->SetValue(value);
    }
    static std::string valueOf(Rml::ElementDocument* doc, const char* id)
    {
      auto* input = dynamic_cast<Rml::ElementFormControlInput*>(doc->GetElementById(id));
      return input ? input->GetValue() : std::string("<no such input>");
    }
    // Presses a button of the page the way the event manager would: the page's handler gets the command.
    void press(EventHandler& handler, Rml::ElementDocument* doc, const char* buttonId, const std::string& command)
    {
      Rml::Event event(doc->GetElementById(buttonId), Rml::EventId::Click, "click", Rml::Dictionary(), true);
      handler.ProcessEvent(event, command);
    }
};

TEST_F(NetFormTest, TheHostFormChecksThePortAndAsksForTheLobbyWithCleanedValues)
{
  Rml::ElementDocument* doc = loadDocument("nethost.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetForm handler;
  NetUiRequests& rq = EventHandlerNetLobby::requests();

  type(doc, "f-name", "  Venom\x01  ");
  type(doc, "f-port", "99999");
  press(handler, doc, "formgo", "nethostgo");
  EXPECT_FALSE(rq.hostGo) << "a port that is not 1..65535 must not open a lobby";
  EXPECT_NE(doc->GetElementById("formerror")->GetInnerRML(), "");

  type(doc, "f-port", "7123");
  press(handler, doc, "formgo", "nethostgo");
  EXPECT_TRUE(rq.hostGo);
  EXPECT_EQ(rq.form.port, 7123);
  EXPECT_EQ(rq.form.name, "Venom");
  EXPECT_EQ(rq.form.localPlayers, 1);
  EXPECT_FALSE(rq.joinGo);
}

TEST_F(NetFormTest, TheJoinFormNormalisesTheAddressAndExplainsABadOne)
{
  Rml::ElementDocument* doc = loadDocument("netjoin.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetForm handler;
  NetUiRequests& rq = EventHandlerNetLobby::requests();

  type(doc, "f-addr", "not an address");
  press(handler, doc, "formgo", "netjoingo");
  EXPECT_FALSE(rq.joinGo);
  EXPECT_NE(doc->GetElementById("formerror")->GetInnerRML().find("character"), std::string::npos) << doc->GetElementById("formerror")->GetInnerRML();

  type(doc, "f-addr", " Example.org ");
  type(doc, "f-name", "Bob");
  press(handler, doc, "formgo", "netjoingo");
  EXPECT_TRUE(rq.joinGo);
  EXPECT_EQ(rq.form.address, "Example.org:7777");
  EXPECT_EQ(rq.form.name, "Bob");
  EXPECT_FALSE(rq.hostGo);
}

TEST_F(NetFormTest, ThePlayersHereButtonCyclesOneToFourAndTheFormsFillFromTheSavedFile)
{
  const std::string path = scratchFile("prefs.txt");
  netplay::NetPrefs saved;
  saved.name = "Zed";
  saved.joinAddress = "10.0.0.5:7001";
  saved.hostPort = 7002;
  saved.localPlayers = 3;
  ASSERT_TRUE(netplay::saveNetPrefs(path, saved));
  EventHandlerNetForm::setPrefsPath(path);

  Rml::ElementDocument* doc = loadDocument("netjoin.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetForm handler;
  press(handler, doc, "f-name", "netforminit");
  EXPECT_EQ(valueOf(doc, "f-name"), "Zed");
  EXPECT_EQ(valueOf(doc, "f-addr"), "10.0.0.5:7001");
  EXPECT_EQ(doc->GetElementById("f-local")->GetInnerRML(), "3");
  Rml::ElementDocument* hostDoc = loadDocument("nethost.rml");
  ASSERT_NE(hostDoc, nullptr);
  press(handler, hostDoc, "f-name", "netforminit");
  EXPECT_EQ(valueOf(hostDoc, "f-port"), "7002");
  EXPECT_EQ(valueOf(hostDoc, "f-name"), "Zed");
  press(handler, doc, "f-local", "netlocal");
  EXPECT_EQ(doc->GetElementById("f-local")->GetInnerRML(), "4");
  press(handler, doc, "f-local", "netlocal");
  EXPECT_EQ(doc->GetElementById("f-local")->GetInnerRML(), "1") << "after the four keyboard sets it wraps";

  // What a successful host or join used is written back.
  EventHandlerNetForm::rememberJoin("Zed", "10.0.0.9:7000", 2);
  netplay::NetPrefs back;
  ASSERT_TRUE(netplay::loadNetPrefs(path, back));
  EXPECT_EQ(back.joinAddress, "10.0.0.9:7000");
  EXPECT_EQ(back.localPlayers, 2);
  EXPECT_EQ(back.hostPort, 7002) << "the host port is kept when a join is remembered";
  EventHandlerNetForm::rememberHost("Zed", 7100, 2);
  ASSERT_TRUE(netplay::loadNetPrefs(path, back));
  EXPECT_EQ(back.hostPort, 7100);
  EXPECT_EQ(back.joinAddress, "10.0.0.9:7000");
  std::remove(path.c_str());
}

TEST_F(NetFormTest, TheSavedFilesPathIsOnlyAskedForWhenAnOnlinePageNeedsIt)
{
  int asked = 0;
  const std::string path = scratchFile("lazy.txt");
  EventHandlerNetForm::setPrefsPathProvider([&]() { asked++; return path; });
  EXPECT_EQ(asked, 0) << "setting the provider must not look anything up";
  Rml::ElementDocument* doc = loadDocument("nethost.rml");
  ASSERT_NE(doc, nullptr);
  EXPECT_EQ(asked, 0) << "opening a page that has not asked for the file yet";
  EventHandlerNetForm handler;
  press(handler, doc, "f-name", "netforminit");
  EXPECT_EQ(asked, 1);
  EventHandlerNetForm::rememberHost("Lazy", 7300, 1);
  EXPECT_EQ(asked, 1) << "asked once";
  netplay::NetPrefs back;
  EXPECT_TRUE(netplay::loadNetPrefs(path, back));
  EXPECT_EQ(back.name, "Lazy");
  std::remove(path.c_str());
}

TEST_F(NetFormTest, TheHostFormsNoteNamesThePortInTheField)
{
  Rml::ElementDocument* doc = loadDocument("nethost.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetForm handler;
  press(handler, doc, "f-name", "netforminit");
  EXPECT_EQ(doc->GetElementById("portnote")->GetInnerRML(), "7777");
  type(doc, "f-port", "7123");
  press(handler, doc, "f-port", "netport");
  EXPECT_EQ(doc->GetElementById("portnote")->GetInnerRML(), "7123");
  type(doc, "f-port", "<b>9");
  press(handler, doc, "f-port", "netport");
  EXPECT_EQ(doc->GetElementById("portnote")->GetInnerRML(), "9") << "only digits are ever echoed";
  EXPECT_EQ(doc->GetElementById("f-port")->GetAttribute<Rml::String>("onchange", ""), "netport") << "the field tells the page when it changes";
}

TEST_F(NetFormTest, TheFormsOpenWithTheDefaultsWhenThereIsNoSavedFile)
{
  Rml::ElementDocument* doc = loadDocument("nethost.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetForm handler;
  press(handler, doc, "f-name", "netforminit");
  EXPECT_EQ(valueOf(doc, "f-name"), "");
  EXPECT_EQ(valueOf(doc, "f-port"), "7777");
  EXPECT_EQ(doc->GetElementById("f-local")->GetInnerRML(), "1");
}

TEST_F(NetFormTest, TheLobbyButtonsRaiseTheRequestsTheEngineActsOn)
{
  Rml::ElementDocument* doc = loadDocument("netlobby.rml");
  ASSERT_NE(doc, nullptr);
  EventHandlerNetLobby handler;
  NetUiRequests& rq = EventHandlerNetLobby::requests();
  press(handler, doc, "netstart", "netstart");
  EXPECT_TRUE(rq.startMatch);
  press(handler, doc, "netleave", "netleave");
  EXPECT_TRUE(rq.leave);
  press(handler, doc, "r2-team", "netteam 2");
  EXPECT_EQ(rq.teamSlot, 2);
  press(handler, doc, "r1-kick", "netkick 1");
  EXPECT_EQ(rq.kickSlot, 1);
  // The five settings, in the order of NetGame::HostSetting (the engine casts the number).
  const std::pair<const char*, int> settings[] = {{"mapw", NetGame::SET_MAP_WIDTH}, {"maph", NetGame::SET_MAP_HEIGHT}, {"blocks", NetGame::SET_BLOCKS},
                                                  {"spawner", NetGame::SET_SPAWNER}, {"slots", NetGame::SET_SLOTS}};
  for (const auto& s : settings)
  {
    rq.setting = -1;
    press(handler, doc, "set-mapw", std::string("netset ") + s.first);
    EXPECT_EQ(rq.setting, s.second) << s.first;
  }
  press(handler, doc, "set-mapw", "netset nonsense");
  EXPECT_EQ(rq.setting, -1);
}

TEST_F(NetFormTest, EveryButtonOfTheOnlinePagesNamesACommandTheirHandlerKnows)
{
  // A typo in an onclick would make a button do nothing at all. Every command the pages use is checked against the handlers.
  const std::pair<const char*, std::vector<std::string>> pages[] = {
      {"netlobby.rml", {"netstart", "netleave", "netcopy", "netteam 0", "netkick 0", "netset mapw", "netset maph", "netset blocks", "netset spawner", "netset slots"}},
      {"nethost.rml", {"netforminit", "netport", "netlocal", "nethostgo", "open netmenu.rml"}},
      {"netjoin.rml", {"netforminit", "netlocal", "netjoingo", "open netmenu.rml"}},
      {"nethostrelay.rml", {"netforminit", "netlocal", "netrelayhostgo", "open netmenu.rml"}},
      {"netjoinrelay.rml", {"netforminit", "netlocal", "netrelayjoingo", "open netmenu.rml"}},
      {"netmenu.rml", {"open nethostrelay.rml", "open netjoinrelay.rml", "open nethost.rml", "open netjoin.rml", "open mainmenu.rml"}},
  };
  for (const auto& page : pages)
  {
    Rml::ElementDocument* doc = loadDocument(page.first);
    ASSERT_NE(doc, nullptr);
    std::vector<std::string> used;
    std::vector<Rml::Element*> all;
    doc->GetElementsByTagName(all, "button");
    doc->GetElementsByTagName(all, "body");
    for (Rml::Element* e : all)
    {
      for (const char* attr : {"onclick", "onload"})
      {
        if (e->HasAttribute(attr)) used.push_back(e->GetAttribute<Rml::String>(attr, ""));
      }
    }
    EXPECT_GE(used.size(), 3u) << page.first;
    for (const std::string& command : used)
    {
      // "netteam 0" and "netkick 0" in the document are written per row ("netteam 3"): compare by the verb.
      const std::string verb = command.substr(0, command.find(' '));
      bool known = false;
      for (const std::string& c : page.second) known = known || c.substr(0, c.find(' ')) == verb;
      EXPECT_TRUE(known) << page.first << " uses '" << command << "' which the test (and so the handler) does not know";
    }
  }
}

// -- brief 12: the relay pages ------------------------------------------------------------------------------

TEST_F(LobbyLayoutTest, ARelayLobbyShowsTheRoomCodeBigAndACopyButtonAndAnEndedOneHidesBoth)
{
  LobbyView v = viewWithSeats(6, false);   // the fullest page: six seats under the big title
  Rml::ElementDocument* doc = loadDocument("netlobby.rml");
  ASSERT_NE(doc, nullptr);
  LobbyContext ctx;
  ctx.isHost = true;
  ctx.canStart = true;
  ctx.viaRelay = true;
  ctx.roomCode = "K7PQ3X";
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(v, ctx));
  updateLayout();
  EXPECT_EQ(doc->GetElementById("nettitle")->GetInnerRML(), "Room K7PQ3X");
  EXPECT_TRUE(doc->GetElementById("nettitle")->IsClassSet("roomcode"));
  EXPECT_TRUE(shown(doc->GetElementById("netcopy")));
  // Big: larger than the plain title, and the whole page still fits the window with its buttons.
  Rml::ElementDocument* plain = loadDocument("netlobby.rml");
  ASSERT_NE(plain, nullptr);
  ctx.viaRelay = false;
  ctx.roomCode.clear();
  NetLobbyBinder::applyLobby(plain, buildLobbyScreen(v, ctx));
  updateLayout();
  EXPECT_FALSE(shown(plain->GetElementById("netcopy"))) << "no room, nothing to copy";
  EXPECT_FALSE(plain->GetElementById("nettitle")->IsClassSet("roomcode"));
  EXPECT_GT(doc->GetElementById("nettitle")->GetProperty("font-size")->Get<float>(), plain->GetElementById("nettitle")->GetProperty("font-size")->Get<float>());
  const CellBox leave = boxOf(doc->GetElementById("netleave"));
  const CellBox copy = boxOf(doc->GetElementById("netcopy"));
  EXPECT_LE(leave.top + leave.height, (float) CONTEXT_HEIGHT);
  EXPECT_LE(copy.left + copy.width, (float) CONTEXT_WIDTH);
  EXPECT_LE(leave.left + leave.width, (float) CONTEXT_WIDTH);

  // The "Connecting" and error pages of a relay game have no room code and no Copy button.
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(v, [&] { LobbyContext c = ctx; c.viaRelay = true; c.roomCode = "K7PQ3X"; return c; }()));
  NetLobbyBinder::applyEnd(doc, describeRelayFailure(VNET_ENOROOM, "relay.example.org", "K7PQ3X", false), "Back");
  updateLayout();
  EXPECT_FALSE(shown(doc->GetElementById("netcopy")));
  EXPECT_FALSE(doc->GetElementById("nettitle")->IsClassSet("roomcode"));
  NetLobbyBinder::applyLobby(doc, buildLobbyScreen(v, [&] { LobbyContext c = ctx; c.viaRelay = true; c.roomCode = "K7PQ3X"; return c; }()));
  NetLobbyBinder::applyConnecting(doc, "Joining room K7PQ3X...", "Connecting to the relay...");
  updateLayout();
  EXPECT_FALSE(shown(doc->GetElementById("netcopy")));
}

TEST_F(LobbyLayoutTest, EveryRelayFailurePageFitsTheWindowWithItsButton)
{
  const int errors[] = {VNET_ENOROOM, VNET_EROOMFULL, VNET_ELIMIT, VNET_ETIMEDOUT, VNET_ECONNREFUSED, VNET_ECLOSED, VNET_EPROTO, VNET_EMSGSIZE, VNET_EIO};
  for (const int e : errors)
  {
    for (const bool hosting : {false, true})
    {
      const EndMessage m = describeRelayFailure(e, "wss://a-rather-long-relay-name.example.org/vnet", "K7PQ3X", hosting);
      SCOPED_TRACE(std::string("error ") + vnet_strerror(e) + ": " + m.title);
      EXPECT_FALSE(m.title.empty());
      EXPECT_GE(m.lines.size(), 1u);
      Rml::ElementDocument* doc = loadDocument("netlobby.rml");
      ASSERT_NE(doc, nullptr);
      NetLobbyBinder::applyEnd(doc, m, "Back");
      updateLayout();
      const CellBox panel = boxOf(doc->GetElementById("endview"));
      const CellBox button = boxOf(doc->GetElementById("netleave"));
      EXPECT_LE(button.top + button.height, (float) CONTEXT_HEIGHT) << "the Back button is below the window";
      EXPECT_LE(panel.top + panel.height, button.top + TOLERANCE_PX);
    }
  }
}

TEST_F(NetFormTest, TheRelayFormsCheckTheRelayAndTheRoomCodeAndAskForTheGameWithCleanedValues)
{
  EventHandlerNetForm handler;
  NetUiRequests& rq = EventHandlerNetLobby::requests();
  EventHandlerNetForm::setRelayDefaults("", "", false);

  Rml::ElementDocument* host = loadDocument("nethostrelay.rml");
  ASSERT_NE(host, nullptr);
  press(handler, host, "f-name", "netforminit");
  EXPECT_EQ(valueOf(host, "f-relay"), netplay::defaultRelayAddress()) << "the relay field starts at the build's default";
  type(host, "f-name", "  Venom\x01 ");
  type(host, "f-relay", "not a relay");
  press(handler, host, "formgo", "netrelayhostgo");
  EXPECT_FALSE(rq.relayHostGo);
  EXPECT_NE(host->GetElementById("formerror")->GetInnerRML(), "");
  type(host, "f-relay", "relay.example.org");
  press(handler, host, "formgo", "netrelayhostgo");
  ASSERT_TRUE(rq.relayHostGo);
  EXPECT_FALSE(rq.relayJoinGo);
  EXPECT_EQ(rq.form.name, "Venom");
  EXPECT_EQ(rq.form.relayUdp, "relay.example.org:7777");
  EXPECT_EQ(rq.form.relayWs, "ws://relay.example.org:8080/vnet") << "a page that is not https";
  EXPECT_EQ(rq.form.localPlayers, 1);

  rq = NetUiRequests();
  Rml::ElementDocument* join = loadDocument("netjoinrelay.rml");
  ASSERT_NE(join, nullptr);
  press(handler, join, "f-name", "netforminit");
  type(join, "f-relay", "wss://relay.example.org/vnet");
  type(join, "f-room", "k7p-q3x");
  press(handler, join, "formgo", "netrelayjoingo");
  ASSERT_TRUE(rq.relayJoinGo);
  EXPECT_FALSE(rq.relayHostGo);
  EXPECT_EQ(rq.form.room, "K7PQ3X");
  EXPECT_EQ(rq.form.relayWs, "wss://relay.example.org/vnet");
  EXPECT_EQ(rq.form.relayUdp, "relay.example.org:7777");

  rq = NetUiRequests();
  type(join, "f-room", "K7PQ30");   // 0 is not in the alphabet
  press(handler, join, "formgo", "netrelayjoingo");
  EXPECT_FALSE(rq.relayJoinGo);
  EXPECT_NE(join->GetElementById("formerror")->GetInnerRML().find("2 to 9"), std::string::npos);
  type(join, "f-room", "");
  press(handler, join, "formgo", "netrelayjoingo");
  EXPECT_FALSE(rq.relayJoinGo);

  // A page served over https means a typed host is a wss:// relay.
  EventHandlerNetForm::setRelayDefaults("", "", true);
  type(join, "f-room", "K7PQ3X");
  type(join, "f-relay", "relay.example.org");
  press(handler, join, "formgo", "netrelayjoingo");
  ASSERT_TRUE(rq.relayJoinGo);
  EXPECT_EQ(rq.form.relayWs, "wss://relay.example.org/vnet");
  EventHandlerNetForm::setRelayDefaults("", "", false);
}

TEST_F(NetFormTest, TheRelayFormsFillFromTheSavedFileThePagesDefaultsAndRememberWhatWasUsed)
{
  const std::string path = scratchFile("relayprefs.txt");
  netplay::NetPrefs saved;
  saved.name = "Zed";
  saved.relay = "wss://r.example.org/vnet";
  saved.room = "AB23CD";
  ASSERT_TRUE(netplay::saveNetPrefs(path, saved));
  EventHandlerNetForm::setPrefsPath(path);
  EventHandlerNetForm handler;

  Rml::ElementDocument* join = loadDocument("netjoinrelay.rml");
  ASSERT_NE(join, nullptr);
  press(handler, join, "f-name", "netforminit");
  EXPECT_EQ(valueOf(join, "f-relay"), "wss://r.example.org/vnet");
  EXPECT_EQ(valueOf(join, "f-room"), "AB23CD");
  EXPECT_EQ(valueOf(join, "f-name"), "Zed");

  // A room in the page's address (?room=) wins over the last one, and the saved relay wins over the page's default.
  EventHandlerNetForm::setRelayDefaults("ws://localhost:8080/vnet", "ZZ99XX", false);
  Rml::ElementDocument* again = loadDocument("netjoinrelay.rml");
  ASSERT_NE(again, nullptr);
  press(handler, again, "f-name", "netforminit");
  EXPECT_EQ(valueOf(again, "f-room"), "ZZ99XX");
  EXPECT_EQ(valueOf(again, "f-relay"), "wss://r.example.org/vnet");

  // An explicit ?relay= (an invite link) wins over the saved relay: the friend must land on the host's relay.
  EventHandlerNetForm::setRelayDefaults("ws://invited.example.org/vnet", "ZZ99XX", false, true);
  Rml::ElementDocument* invited = loadDocument("netjoinrelay.rml");
  ASSERT_NE(invited, nullptr);
  press(handler, invited, "f-name", "netforminit");
  EXPECT_EQ(valueOf(invited, "f-relay"), "ws://invited.example.org/vnet");
  // The page's own default is not a choice and is not saved, so a site that moves its relay moves returning visitors too.
  EventHandlerNetForm::rememberRelay("Zed", "ws://invited.example.org/vnet", "", 1);
  netplay::NetPrefs def;
  ASSERT_TRUE(netplay::loadNetPrefs(path, def));
  EXPECT_EQ(def.relay, "") << "equal to the page default: not saved";
  EventHandlerNetForm::setRelayDefaults("ws://localhost:8080/vnet", "ZZ99XX", false);
  EventHandlerNetForm::rememberRelay("Zed", "relay.other.org", "QQ22RR", 2);
  netplay::NetPrefs back;
  ASSERT_TRUE(netplay::loadNetPrefs(path, back));
  EXPECT_EQ(back.relay, "relay.other.org");
  EXPECT_EQ(back.room, "QQ22RR");
  EXPECT_EQ(back.localPlayers, 2);
  // Hosting remembers the relay but has no room to remember: the last room joined stays.
  EventHandlerNetForm::rememberRelay("Zed", "relay.third.org", "", 1);
  ASSERT_TRUE(netplay::loadNetPrefs(path, back));
  EXPECT_EQ(back.relay, "relay.third.org");
  EXPECT_EQ(back.room, "QQ22RR");
  std::remove(path.c_str());
  EventHandlerNetForm::setRelayDefaults("", "", false);
}

TEST_F(NetFormTest, TheSettingsFileCanLiveSomewhereElseThroughTheSyncHooks)
{
  // The web build keeps the file's text in localStorage: it is copied into the file before the file is read and out of it
  // after a write.
  const std::string path = scratchFile("synced.txt");
  std::remove(path.c_str());
  std::string storage = "name=Fromstorage\nrelay=r.example.org\n";
  std::string loadedPath, savedPath;
  EventHandlerNetForm::setPrefsPath(path);
  EventHandlerNetForm::setPrefsSyncHooks(
      [&](const std::string& p) {
        loadedPath = p;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << storage;
      },
      [&](const std::string& p) {
        savedPath = p;
        std::ifstream f(p, std::ios::binary);
        std::stringstream b;
        b << f.rdbuf();
        storage = b.str();
      });
  Rml::ElementDocument* host = loadDocument("nethostrelay.rml");
  ASSERT_NE(host, nullptr);
  EventHandlerNetForm handler;
  press(handler, host, "f-name", "netforminit");
  EXPECT_EQ(loadedPath, path);
  EXPECT_EQ(valueOf(host, "f-name"), "Fromstorage");
  EXPECT_EQ(valueOf(host, "f-relay"), "r.example.org");
  EventHandlerNetForm::rememberRelay("Newname", "r2.example.org", "", 3);
  EXPECT_EQ(savedPath, path);
  EXPECT_NE(storage.find("name=Newname"), std::string::npos);
  EXPECT_NE(storage.find("relay=r2.example.org"), std::string::npos);
  EventHandlerNetForm::setPrefsSyncHooks(nullptr, nullptr);
  std::remove(path.c_str());
}
} // namespace bitriot_test
