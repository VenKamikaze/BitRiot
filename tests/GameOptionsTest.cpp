// The game's command line (brief 05: getopt_long, --record <dir>).

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "GameOptions.h"

namespace
{
struct Parsed
{
  GameOptions options;
  std::string err;
};

Parsed parse(std::vector<std::string> words)
{
  std::vector<std::string> storage = { "BitRiot" };
  storage.insert(storage.end(), words.begin(), words.end());
  std::vector<char*> argv;
  for (std::string& s : storage) { argv.push_back(&s[0]); }
  argv.push_back(nullptr);
  std::ostringstream err;
  Parsed p;
  p.options = parseGameOptions((int) storage.size(), argv.data(), err);
  p.err = err.str();
  return p;
}
} // namespace

TEST(GameOptionsTest, NoArgumentsMeansTheDefaults)
{
  const Parsed p = parse({});
  EXPECT_FALSE(p.options.fullscreen);
  EXPECT_FALSE(p.options.bigMap);
  EXPECT_FALSE(p.options.help);
  EXPECT_FALSE(p.options.error);
  EXPECT_TRUE(p.options.recordDir.empty());
}

TEST(GameOptionsTest, TheOldShortOptionsStillWork)
{
  const Parsed p = parse({ "-f", "-b" });
  EXPECT_TRUE(p.options.fullscreen);
  EXPECT_TRUE(p.options.bigMap);
  const Parsed together = parse({ "-fb" });
  EXPECT_TRUE(together.options.fullscreen);
  EXPECT_TRUE(together.options.bigMap);
  EXPECT_TRUE(parse({ "-h" }).options.help);
}

TEST(GameOptionsTest, RecordTakesADirectoryInEitherSpelling)
{
  EXPECT_EQ(parse({ "--record", "/tmp/rounds" }).options.recordDir, "/tmp/rounds");
  EXPECT_EQ(parse({ "--record=/tmp/rounds" }).options.recordDir, "/tmp/rounds");
  const Parsed p = parse({ "-b", "--record", "out", "--fullscreen" });
  EXPECT_EQ(p.options.recordDir, "out");
  EXPECT_TRUE(p.options.bigMap);
  EXPECT_TRUE(p.options.fullscreen);
  EXPECT_FALSE(p.options.error);
}

TEST(GameOptionsTest, LongNamesAreAbbreviableAsGetoptLongAllows)
{
  EXPECT_EQ(parse({ "--rec", "x" }).options.recordDir, "x");
  EXPECT_TRUE(parse({ "--help" }).options.help);
  EXPECT_TRUE(parse({ "--big-map" }).options.bigMap);
}

TEST(GameOptionsTest, ProblemsAreReportedAndFlagged)
{
  {
    const Parsed p = parse({ "--record" });
    EXPECT_TRUE(p.options.error);
    EXPECT_NE(p.err.find("--record needs a directory"), std::string::npos) << p.err;
  }
  {
    const Parsed p = parse({ "--record=" });
    EXPECT_TRUE(p.options.error);
  }
  {
    const Parsed p = parse({ "--frobnicate" });
    EXPECT_TRUE(p.options.error);
    EXPECT_NE(p.err.find("unknown option --frobnicate"), std::string::npos) << p.err;
  }
  {
    const Parsed p = parse({ "-x" });
    EXPECT_TRUE(p.options.error);
    EXPECT_NE(p.err.find("unknown option -x"), std::string::npos) << p.err;
  }
  {
    const Parsed p = parse({ "stray" });
    EXPECT_TRUE(p.options.error);
    EXPECT_NE(p.err.find("unexpected argument stray"), std::string::npos) << p.err;
  }
}

TEST(GameOptionsTest, ParsingTwiceInOneProcessGivesTheSameAnswer)
{
  for (int i = 0; i < 3; i++)
  {
    EXPECT_EQ(parse({ "--record", "a", "-b" }).options.recordDir, "a");
    EXPECT_FALSE(parse({}).options.bigMap);
  }
}
