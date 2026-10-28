// Scoreboard layout tests.
//
// Verifies the end-of-game scoreboard fix: "text needs to be centred and column
// headings should be made". These assert on real computed geometry from the
// game's actual scoreboard.rml/.rcss, loaded through RmlUi with layout resolved
// but nothing rasterised.
//
// Scope, stated plainly: this verifies *geometry*. "Every data cell starts at
// the same x as its column heading, to within a pixel" is a fact these tests
// establish. Whether the result looks good is not.

#include "support/RmlLayoutHarness.h"

#include <memory>
#include <vector>

#include "EntityManager.h"
#include "EntityMessageQueue.h"
#include "GameSettings.h"
#include "PlayerCharacterEntity.h"
#include "RmlUI/binders/ScoreBoardBinder.h"

namespace bitriot_test
{
namespace
{

// Columns are percentage-width and the header and data rows use identical
// classes, so offsets should agree almost exactly. Allow a pixel for RmlUi's
// sub-pixel rounding of percentage widths.
const float ALIGNMENT_TOLERANCE_PX = 1.0f;

const int EXPECTED_COLUMNS = 5;

class ScoreboardLayoutTest : public RmlLayoutTest
{
  protected:
    void SetUp() override
    {
      RmlLayoutTest::SetUp();
      if (::testing::Test::HasFatalFailure())
      {
        return;
      }
      // getTeamNumber() reads GameSettings, so the roster must be configured
      // before any player entity is asked for its team.
      std::GameSettings::getInstance()->setNumberOfPlayers(4);
      for (int i = 0; i < 4; i++)
      {
        std::GameSettings::getInstance()->setPlayerTeam(i, i);
      }
    }

    void TearDown() override
    {
      if (m_binder)
      {
        m_binder->RemoveDataBinding(context());
        m_binder.reset();
      }
      m_players.clear();
      RmlLayoutTest::TearDown();
    }

    // Builds a player with plausible stats. playerNumber is 1-based.
    std::shared_ptr<PlayerCharacterEntity> makePlayer(int playerNumber, bool alive, int eggs,
                                                      int bombs)
    {
      std::shared_ptr<PlayerCharacterEntity> p =
          std::make_shared<PlayerCharacterEntity>(playerNumber, playerNumber, 1, 1, 0);
      p->setAlive(alive);
      for (int i = 0; i < eggs; i++)
      {
        p->incNumberEggsPlaced();
      }
      for (int i = 0; i < bombs; i++)
      {
        p->incNumberBombsPlaced();
      }
      return p;
    }

    // Creates the data model then loads the document, in that order - the
    // document binds to "scoreBoard" at load time.
    Rml::ElementDocument* loadScoreboard(std::shared_ptr<PlayerCharacterEntity> winner,
                                         std::vector<std::shared_ptr<PlayerCharacterEntity>> players,
                                         int gameLengthSeconds)
    {
      m_players = players;
      m_binder = std::unique_ptr<std::ScoreBoardBinder>(
          new std::ScoreBoardBinder(context(), winner, players, gameLengthSeconds));
      return loadDocument("scoreboard.rml");
    }

    // The header row and the per-player rows, separated.
    struct ScoreTable
    {
      Rml::Element* header = nullptr;
      std::vector<Rml::Element*> dataRows;
    };

    ScoreTable readTable(Rml::ElementDocument* document)
    {
      ScoreTable table;
      Rml::Element* container = document->GetElementById("teamStats");
      EXPECT_NE(container, nullptr) << "#teamStats missing from scoreboard.rml";
      if (container == nullptr)
      {
        return table;
      }

      for (Rml::Element* row : findAllByClass(container, "scoreRow"))
      {
        if (row->IsClassSet("scoreHeader"))
        {
          table.header = row;
        }
        else if (row->GetBox().GetSize(Rml::Box::BORDER).y > 0.0f)
        {
          // Skip the zero-height data-for template element.
          table.dataRows.push_back(row);
        }
      }
      return table;
    }

    std::vector<CellBox> cellsOf(Rml::Element* row)
    {
      std::vector<CellBox> boxes;
      for (Rml::Element* cell : visibleChildren(row))
      {
        boxes.push_back(boxOf(cell));
      }
      return boxes;
    }

  private:
    std::unique_ptr<std::ScoreBoardBinder> m_binder;
    std::vector<std::shared_ptr<PlayerCharacterEntity>> m_players;
};

// -- the document loads at all ----------------------------------------------

TEST_F(ScoreboardLayoutTest, ScoreboardDocumentLoadsAndLaysOut)
{
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 3, 2));
  players.push_back(makePlayer(2, false, 1, 4));

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 91);

  ASSERT_NE(document, nullptr) << "scoreboard.rml failed to load";
  EXPECT_GT(document->GetBox().GetSize(Rml::Box::BORDER).x, 0.0f) << "document has no layout";
}

// -- the column headings the TODO asked for ---------------------------------

TEST_F(ScoreboardLayoutTest, HeaderRowExistsWithAllFiveColumnTitles)
{
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 3, 2));
  players.push_back(makePlayer(2, false, 1, 4));

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 91);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr) << "no .scoreHeader row - column headings are missing";

  std::vector<CellBox> headerCells = cellsOf(table.header);
  ASSERT_EQ((int) headerCells.size(), EXPECTED_COLUMNS);

  const char* expected[] = {"Team", "Player", "Eggs", "Explosives", "Result"};
  for (int i = 0; i < EXPECTED_COLUMNS; i++)
  {
    EXPECT_EQ(headerCells[i].text, expected[i]) << "column " << i << " heading";
  }
}

TEST_F(ScoreboardLayoutTest, HeaderRowSitsAboveTheDataRows)
{
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 3, 2));
  players.push_back(makePlayer(2, false, 1, 4));

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 91);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr);
  ASSERT_FALSE(table.dataRows.empty());

  const CellBox header = boxOf(table.header);
  for (size_t i = 0; i < table.dataRows.size(); i++)
  {
    const CellBox row = boxOf(table.dataRows[i]);
    EXPECT_LT(header.top, row.top) << "header must sit above data row " << i;
  }
}

// -- the alignment fix ------------------------------------------------------

TEST_F(ScoreboardLayoutTest, EveryDataCellAlignsWithItsColumnHeading)
{
  // This is the reported bug: "Team scoreboard does not align very well".
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 12, 7));
  players.push_back(makePlayer(2, false, 3, 21));
  players.push_back(makePlayer(3, true, 0, 0));
  players.push_back(makePlayer(4, false, 105, 99)); // wide values must not shift the column

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 245);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr);
  ASSERT_EQ((int) table.dataRows.size(), 4) << "one row per player";

  const std::vector<CellBox> headerCells = cellsOf(table.header);
  ASSERT_EQ((int) headerCells.size(), EXPECTED_COLUMNS);

  for (size_t r = 0; r < table.dataRows.size(); r++)
  {
    const std::vector<CellBox> cells = cellsOf(table.dataRows[r]);
    ASSERT_EQ((int) cells.size(), EXPECTED_COLUMNS)
        << "row " << r << " should show exactly one Result cell (survived XOR died)";

    for (int c = 0; c < EXPECTED_COLUMNS; c++)
    {
      EXPECT_NEAR(cells[c].left, headerCells[c].left, ALIGNMENT_TOLERANCE_PX)
          << "row " << r << " column " << c << " left edge (\"" << cells[c].text
          << "\" under \"" << headerCells[c].text << "\")";
      EXPECT_NEAR(cells[c].width, headerCells[c].width, ALIGNMENT_TOLERANCE_PX)
          << "row " << r << " column " << c << " width";
    }
  }
}

TEST_F(ScoreboardLayoutTest, ColumnsDoNotOverlapOrLeaveTheRow)
{
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 12, 7));
  players.push_back(makePlayer(2, false, 3, 21));

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 100);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr);

  const CellBox rowBox = boxOf(table.header);
  const std::vector<CellBox> cells = cellsOf(table.header);
  ASSERT_EQ((int) cells.size(), EXPECTED_COLUMNS);

  for (int c = 0; c + 1 < EXPECTED_COLUMNS; c++)
  {
    EXPECT_LE(cells[c].right(), cells[c + 1].left + ALIGNMENT_TOLERANCE_PX)
        << "column " << c << " (\"" << cells[c].text << "\") overlaps column " << (c + 1);
  }

  EXPECT_GE(cells.front().left, rowBox.left - ALIGNMENT_TOLERANCE_PX)
      << "first column starts outside the row";
  EXPECT_LE(cells.back().right(), rowBox.right() + ALIGNMENT_TOLERANCE_PX)
      << "last column runs past the end of the row";
}

TEST_F(ScoreboardLayoutTest, AllRowsAreTheSameHeightSoNoHeadingWrapped)
{
  // "Explosives" is the longest heading and the RCSS comment notes the widths
  // were chosen to leave it room. A wrapped cell makes its row taller.
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  for (int i = 1; i <= 6; i++)
  {
    players.push_back(makePlayer(i, i % 2 == 0, i * 3, i * 2));
  }
  std::GameSettings::getInstance()->setNumberOfPlayers(6);
  for (int i = 0; i < 6; i++)
  {
    std::GameSettings::getInstance()->setPlayerTeam(i, i);
  }

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 300);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr);
  ASSERT_EQ((int) table.dataRows.size(), 6);

  const float headerHeight = boxOf(table.header).height;
  EXPECT_GT(headerHeight, 0.0f);

  const float firstRowHeight = boxOf(table.dataRows[0]).height;
  for (size_t r = 1; r < table.dataRows.size(); r++)
  {
    EXPECT_NEAR(boxOf(table.dataRows[r]).height, firstRowHeight, ALIGNMENT_TOLERANCE_PX)
        << "row " << r << " is a different height - a cell probably wrapped";
  }
}

// -- team grouping and the winner highlight ---------------------------------

TEST_F(ScoreboardLayoutTest, SixPlayerTeamScoreboardAlignsAndGroupsByTeam)
{
  // Two teams of three - the case the team-play feature added.
  std::GameSettings::getInstance()->setNumberOfPlayers(6);
  const int teams[] = {0, 1, 0, 1, 0, 1};
  for (int i = 0; i < 6; i++)
  {
    std::GameSettings::getInstance()->setPlayerTeam(i, teams[i]);
  }

  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  for (int i = 1; i <= 6; i++)
  {
    // Team 0 (players 1, 3, 5) survives.
    const bool alive = (teams[i - 1] == 0);
    players.push_back(makePlayer(i, alive, i, i + 1));
  }

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 412);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_NE(table.header, nullptr);
  ASSERT_EQ((int) table.dataRows.size(), 6);

  const std::vector<CellBox> headerCells = cellsOf(table.header);
  ASSERT_EQ((int) headerCells.size(), EXPECTED_COLUMNS);

  // Alignment must hold at six rows too.
  for (size_t r = 0; r < table.dataRows.size(); r++)
  {
    const std::vector<CellBox> cells = cellsOf(table.dataRows[r]);
    ASSERT_EQ((int) cells.size(), EXPECTED_COLUMNS) << "row " << r;
    for (int c = 0; c < EXPECTED_COLUMNS; c++)
    {
      EXPECT_NEAR(cells[c].left, headerCells[c].left, ALIGNMENT_TOLERANCE_PX)
          << "row " << r << " column " << c;
    }
  }

  // Rows are sorted by team, so the Team column must be non-decreasing.
  int previousTeam = -1;
  for (size_t r = 0; r < table.dataRows.size(); r++)
  {
    const std::vector<CellBox> cells = cellsOf(table.dataRows[r]);
    const int team = std::atoi(cells[0].text.c_str());
    EXPECT_GE(team, previousTeam) << "row " << r << " breaks the team grouping";
    previousTeam = team;
  }

  // The winning team's rows are marked for the brighter/bold styling.
  int winnerRows = 0;
  for (Rml::Element* row : table.dataRows)
  {
    if (row->IsClassSet("winnerRow"))
    {
      winnerRows++;
    }
  }
  EXPECT_EQ(winnerRows, 3) << "all three members of the winning team should be highlighted";
}

TEST_F(ScoreboardLayoutTest, ResultColumnShowsSurvivedOrDiedButNeverBoth)
{
  std::vector<std::shared_ptr<PlayerCharacterEntity>> players;
  players.push_back(makePlayer(1, true, 5, 5));
  players.push_back(makePlayer(2, false, 5, 5));

  Rml::ElementDocument* document = loadScoreboard(players[0], players, 60);
  ASSERT_NE(document, nullptr);

  ScoreTable table = readTable(document);
  ASSERT_EQ((int) table.dataRows.size(), 2);

  const std::vector<CellBox> aliveRow = cellsOf(table.dataRows[0]);
  const std::vector<CellBox> deadRow = cellsOf(table.dataRows[1]);
  ASSERT_EQ((int) aliveRow.size(), EXPECTED_COLUMNS);
  ASSERT_EQ((int) deadRow.size(), EXPECTED_COLUMNS);

  EXPECT_EQ(aliveRow.back().text, "survived");
  EXPECT_EQ(deadRow.back().text, "died");

  // Both must land in the same column despite being different elements.
  EXPECT_NEAR(aliveRow.back().left, deadRow.back().left, ALIGNMENT_TOLERANCE_PX);
}

// -- every player who played appears, dead or alive -------------------------

TEST_F(ScoreboardLayoutTest, ScoreboardFromRealRoundListsPlayersWhoDiedAsWellAsSurvivors)
{
  // The reported bug: dead players were erased from the entity list, so a
  // scoreboard fed from getAllPlayerCharacters() only ever showed survivors.
  // This drives the real EntityManager (create + DESTROY_ENTITY messages) and
  // feeds the scoreboard from the round roster, as GameEngine does.
  std::GameSettings* settings = std::GameSettings::getInstance();
  settings->setMapWidth(17);
  settings->setMapHeight(13);

  EntityMessageQueue* emq = EntityMessageQueue::getInstance();
  {
    EntityManager entityManager;
    for (int i = 1; i <= 4; i++)
    {
      emq->postCreateMessage(PLAYER_CHARACTER, i, 1 + (2 * i), 1, 0);
      entityManager.processMessageQueue();
    }
    for (int i = 1; i <= 4; i++)
    {
      std::shared_ptr<PlayerCharacterEntity> p = entityManager.getPlayerPointer(i);
      ASSERT_NE(p, nullptr);
      for (int e = 0; e < i; e++)
      {
        p->incNumberEggsPlaced();
      }
    }
    // Players 1, 2 and 4 die for real: flagged dead and erased from the list.
    const int dead[] = {1, 2, 4};
    for (int i : dead)
    {
      emq->postDestroyMessage(entityManager.getPlayerPointer(i)->getID());
      entityManager.processMessageQueue();
    }
    ASSERT_EQ(entityManager.getAllPlayerCharacters()->size(), 1u) << "precondition: only a survivor is live";

    std::shared_ptr<PlayerCharacterEntity> winner = entityManager.getWinningPlayer(false);
    ASSERT_NE(winner, nullptr);
    Rml::ElementDocument* document =
        loadScoreboard(winner, entityManager.getRoundPlayerRoster(), 120);
    ASSERT_NE(document, nullptr);

    ScoreTable table = readTable(document);
    ASSERT_NE(table.header, nullptr);
    ASSERT_EQ((int) table.dataRows.size(), 4) << "one row per player, including the dead";

    // Rows are sorted by team then player, so row r is player r + 1.
    int survivedRows = 0;
    int diedRows = 0;
    for (size_t r = 0; r < table.dataRows.size(); r++)
    {
      const std::vector<CellBox> cells = cellsOf(table.dataRows[r]);
      ASSERT_EQ((int) cells.size(), EXPECTED_COLUMNS) << "row " << r;
      EXPECT_EQ(std::atoi(cells[1].text.c_str()), (int) r + 1) << "row " << r << " player";
      EXPECT_EQ(std::atoi(cells[2].text.c_str()), (int) r + 1) << "row " << r << " eggs";
      if (cells.back().text == "survived")
      {
        survivedRows++;
        EXPECT_EQ(r, 2u) << "only player 3 survived";
      }
      else
      {
        EXPECT_EQ(cells.back().text, "died") << "row " << r;
        diedRows++;
      }
    }
    EXPECT_EQ(survivedRows, 1);
    EXPECT_EQ(diedRows, 3);

  }
}

} // namespace
} // namespace bitriot_test
