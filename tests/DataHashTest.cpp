// The data hash and eager data loading (brief 05). The hash is over the PARSED
// constants of data/entity.txt and data/player.txt plus the named sim constants, so
// two files that parse the same hash the same (comments, order, spacing, line endings)
// and any change to a value, a name or the set of names changes it.

#include "support/HeadlessGame.h"

#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "DataHash.h"
#include "DataReader.h"
#include "MatchConfig.h"

namespace fs = std::filesystem;
using bitriot_test::HeadlessGameTest;

namespace
{
std::string slurp(const std::string& path)
{
  std::ifstream in(path.c_str(), std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

void spit(const fs::path& path, const std::string& text)
{
  std::ofstream out(path.string().c_str(), std::ios::binary | std::ios::trunc);
  out << text;
}

// An independent FNV-1a 64 over "name=value\n" lines, from the specification in DataHash.h.
uint64_t referenceHash(const std::vector<std::string>& items)
{
  uint64_t h = 0xcbf29ce484222325ULL;
  for (const std::string& item : items)
  {
    for (char c : item + "\n")
    {
      h ^= (unsigned char) c;
      h *= 0x100000001b3ULL;
    }
  }
  return h;
}
} // namespace

// Runs in a scratch directory that holds its own data/ (so a test can change the
// files), then puts the working directory and the reader's cache back.
class DataHashTest : public HeadlessGameTest
{
  protected:
    void SetUp() override
    {
      HeadlessGameTest::SetUp();
      m_original = fs::current_path();
      m_scratch = fs::temp_directory_path() / ("bitriot_datahash_" + std::to_string(getpid()) + "_" + std::to_string(std::rand()));
      fs::create_directories(m_scratch / "data");
      for (const char* f : { "entity.txt", "player.txt" })
      {
        spit(m_scratch / "data" / f, slurp((m_original / "data" / f).string()));
      }
    }

    void TearDown() override
    {
      fs::current_path(m_original);
      DataReader::getInstance()->clearCache();
      std::error_code ignored;
      fs::remove_all(m_scratch, ignored);
      HeadlessGameTest::TearDown();
    }

    // The hash of the scratch directory's current files.
    uint64_t hashHere()
    {
      fs::current_path(m_scratch);
      DataReader::getInstance()->clearCache();
      const uint64_t h = DataHash::hash();
      return h;
    }

    std::string entityText() { return slurp((m_scratch / "data" / "entity.txt").string()); }
    void setEntity(const std::string& text) { spit(m_scratch / "data" / "entity.txt", text); }
    std::string playerText() { return slurp((m_scratch / "data" / "player.txt").string()); }
    void setPlayer(const std::string& text) { spit(m_scratch / "data" / "player.txt", text); }

    fs::path m_original;
    fs::path m_scratch;
};

TEST_F(DataHashTest, TheHashIsTheDocumentedFnvOverTheSortedItems)
{
  const std::vector<std::string> items = DataHash::items();
  EXPECT_TRUE(std::is_sorted(items.begin(), items.end()));
  EXPECT_EQ(DataHash::hash(), referenceHash(items));
  EXPECT_EQ(Simulation::dataHash(), DataHash::hash());
  // a known item from each file, written the documented way
  EXPECT_NE(std::find(items.begin(), items.end(), "data/entity.txt:BOMB_FUSE=45"), items.end());
  EXPECT_NE(std::find(items.begin(), items.end(), "data/player.txt:PLAYER_INITIAL_HEALTH=100"), items.end());
}

TEST_F(DataHashTest, TheItemsAreSortedByNameAlone)
{
  // "A" < "A-B" by name, but "A-B=1" < "A=1" as finished text ('-' sorts before '=').
  // The spec sorts by name.
  EXPECT_EQ(DataHash::hashOfItems({ "x:A=1", "x:A-B=1" }), referenceHash({ "x:A=1", "x:A-B=1" }));
  const std::vector<std::string> items = DataHash::items();
  std::vector<std::string> names;
  for (const std::string& item : items) { names.push_back(item.substr(0, item.find('='))); }
  EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
}

TEST_F(DataHashTest, ANameThatIsAPrefixOfAnotherSortsFirstAndTheSortIsOnTheNameAlone)
{
  // "BOMB_FUSE" < "BOMB_FUSE-X" by name, but as finished text "BOMB_FUSE-X=1" sorts
  // before "BOMB_FUSE=45" ('-' is below '='). The specification sorts by name.
  fs::current_path(m_scratch);
  setEntity(entityText() + "BOMB_FUSE-X 1\n");
  DataReader::getInstance()->clearCache();
  const std::vector<std::string> items = DataHash::items();
  const auto fuse = std::find(items.begin(), items.end(), "data/entity.txt:BOMB_FUSE=45");
  const auto fuseX = std::find(items.begin(), items.end(), "data/entity.txt:BOMB_FUSE-X=1");
  ASSERT_NE(fuse, items.end());
  ASSERT_NE(fuseX, items.end());
  EXPECT_LT(fuse, fuseX);
  EXPECT_EQ(DataHash::hash(), referenceHash(items));
}

TEST_F(DataHashTest, TheSimulationsNamedConstantsAreInIt)
{
  const std::vector<std::string> items = DataHash::items();
  for (const char* wanted : { "sim:Simulation.STATE_HASH_VERSION=2", "sim:Simulation.POOL_POINTS_PER_PLAYER=100",
                              "sim:GameOverTimer.GAME_OVER_COUNTDOWN_TICKS=60", "sim:GameSettings.MAX_PLAYERS=6",
                              "sim:Fixed.FIXED_SHIFT=16", "sim:SimpleBrain.INITIAL_MIN_MOVE_TIMER=5",
                              "sim:PlayerCharacterEntity.MIN_HEALTH_TO_LAY=15", "sim:PlayerCharacterEntity.MAX_PICKUP_ATTEMPTS=3",
                              "sim:PlayerCharacterEntity.PICKUP_SEARCH_RANGE=4" })
  {
    EXPECT_NE(std::find(items.begin(), items.end(), wanted), items.end()) << wanted;
  }
}

TEST_F(DataHashTest, TheSameConstantsHashTheSameWhateverTheFileLooksLike)
{
  const uint64_t plain = hashHere();
  EXPECT_EQ(plain, Simulation::dataHash()) << "the scratch copy is the real file";

  // Comments, blank lines, tabs for spaces, a leading zero, trailing spaces.
  std::string noisy = "# a different comment\n\n\n" + entityText();
  const size_t at = noisy.find("BOMB_FUSE 45");
  ASSERT_NE(at, std::string::npos);
  noisy.replace(at, 12, "BOMB_FUSE\t\t045");
  noisy += "\n# and a comment at the end\n";
  setEntity(noisy);
  EXPECT_EQ(hashHere(), plain);

  // Windows line endings (what git autocrlf makes of it).
  std::string crlf;
  for (char c : entityText()) { if (c == '\n') { crlf += "\r\n"; } else { crlf += c; } }
  setEntity(crlf);
  EXPECT_EQ(hashHere(), plain) << "CRLF must not change the hash";
  std::string crlfPlayer;
  for (char c : playerText()) { if (c == '\n') { crlfPlayer += "\r\n"; } else { crlfPlayer += c; } }
  setPlayer(crlfPlayer);
  EXPECT_EQ(hashHere(), plain);
}

TEST_F(DataHashTest, TheOrderOfTheLinesDoesNotMatter)
{
  const uint64_t plain = hashHere();
  std::istringstream in(entityText());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) { lines.push_back(line); }
  std::reverse(lines.begin(), lines.end());
  std::string reversed;
  for (const std::string& l : lines) { reversed += l + "\n"; }
  setEntity(reversed);
  EXPECT_EQ(hashHere(), plain);
}

TEST_F(DataHashTest, AnyChangeToAValueANameOrTheSetOfNamesMovesTheHash)
{
  const uint64_t plain = hashHere();
  const std::string entity = entityText();
  const std::string player = playerText();

  std::string changed = entity;
  changed.replace(changed.find("BOMB_FUSE 45"), 12, "BOMB_FUSE 46");
  setEntity(changed);
  const uint64_t value = hashHere();
  EXPECT_NE(value, plain) << "a value";

  changed = entity;
  changed.replace(changed.find("BOMB_FUSE"), 9, "BOMB_FUSF");
  setEntity(changed);
  const uint64_t name = hashHere();
  EXPECT_NE(name, plain) << "a name";
  EXPECT_NE(name, value);

  setEntity(entity + "A_NEW_CONSTANT 7\n");
  EXPECT_NE(hashHere(), plain) << "an added constant";

  changed = entity;
  changed.erase(changed.find("BOMB_FUSE 45"), 13);
  setEntity(changed);
  EXPECT_NE(hashHere(), plain) << "a removed constant";

  setEntity(entity);
  changed = player;
  changed.replace(changed.find("BLOCK_COST 	1"), 13, "BLOCK_COST 	2");
  setPlayer(changed);
  EXPECT_NE(hashHere(), plain) << "a value in the other file";

  // The same constant in the other file is another constant.
  setPlayer(player);
  setEntity(entity + "BLOCK_COST 1\n");
  EXPECT_NE(hashHere(), plain);
}

TEST_F(DataHashTest, AFileThatCannotBeReadIsAnErrorNotAHashOfNothing)
{
  fs::current_path(m_scratch);
  DataReader::getInstance()->clearCache();
  fs::remove(m_scratch / "data" / "entity.txt");
  EXPECT_THROW(DataHash::hash(), std::runtime_error);
  EXPECT_THROW(Simulation::dataHash(), std::runtime_error);

  setEntity("BOMB_FUSE\n"); // a line with no value does not parse
  DataReader::getInstance()->clearCache();
  EXPECT_THROW(DataHash::loadSimData(), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Eager loading
// ---------------------------------------------------------------------------

TEST_F(DataHashTest, StartLoadsTheDataFilesNowAndRefusesToPlayOnFilesItCannotRead)
{
  // Start a good round first, so there is something that must survive a refusal.
  MatchConfig good;
  good.seed = 1;
  good.mapWidth = 13;
  good.mapHeight = 11;
  good.slots.resize(2);
  good.slots[0].team = 0;
  good.slots[1].team = 1;
  sim().start(good);
  sim().step();
  sim().step();
  const uint64_t before = sim().stateHash();

  fs::current_path(m_scratch);
  DataReader::getInstance()->clearCache();
  fs::remove(m_scratch / "data" / "player.txt");
  EXPECT_THROW(sim().start(good), std::runtime_error);
  EXPECT_EQ(sim().tick(), 2u) << "a refused start leaves the running round alone";
  EXPECT_EQ(sim().stateHash(), before);
}

TEST_F(DataHashTest, AfterStartTheFilesAreAlreadyParsedAndDoNotNeedToBeReadAgain)
{
  MatchConfig c;
  c.seed = 2;
  c.mapWidth = 13;
  c.mapHeight = 11;
  c.slots.resize(2);
  c.slots[0].team = 0;
  c.slots[1].team = 1;
  fs::current_path(m_scratch);
  DataReader::getInstance()->clearCache();
  sim().start(c);
  // Take the files away: nothing the round needs is read from disk now.
  fs::remove_all(m_scratch / "data");
  const int errorsBefore = DataReader::getInstance()->errorCount();
  for (int i = 0; i < 400; i++) { sim().step(); }
  EXPECT_EQ(DataReader::getInstance()->errorCount(), errorsBefore);
}

TEST_F(DataHashTest, ALineEndingOfWindowsReadsLikeOneOfUnix)
{
  // DataReader strips carriage returns, so CRLF data files load on Linux too (before,
  // the "\r" ended up in the value and the file did not parse).
  fs::current_path(m_scratch);
  spit(m_scratch / "data" / "entity.txt", "BOMB_FUSE 45\r\nMINE_FUSE 450\r\n");
  DataReader::getInstance()->clearCache();
  DataReader* reader = DataReader::getInstance();
  EXPECT_EQ(reader->getIntFromFile("BOMB_FUSE", "data/entity.txt"), 45);
  EXPECT_EQ(reader->getIntFromFile("MINE_FUSE", "data/entity.txt"), 450);
  EXPECT_EQ(reader->errorCount(), 0);
}
