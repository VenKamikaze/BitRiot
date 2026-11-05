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


// -- network play (brief 07) -----------------------------------------------------------

TEST(GameOptionsTest, NoNetworkOptionsMeansLocalPlay)
{
  const Parsed p = parse({ "-b" });
  EXPECT_FALSE(p.options.networked());
  EXPECT_FALSE(p.options.host);
  EXPECT_TRUE(p.options.join.empty());
  EXPECT_EQ(p.options.localPlayers, 1);
}

TEST(GameOptionsTest, HostTakesAnOptionalPortInEitherSpelling)
{
  Parsed p = parse({ "--host" });
  EXPECT_FALSE(p.options.error) << p.err;
  EXPECT_TRUE(p.options.host);
  EXPECT_EQ(p.options.hostPort, 7777);
  EXPECT_TRUE(p.options.networked());

  p = parse({ "--host", "9000" });
  EXPECT_FALSE(p.options.error) << p.err;
  EXPECT_EQ(p.options.hostPort, 9000);

  p = parse({ "--host=9001", "-b" });
  EXPECT_FALSE(p.options.error) << p.err;
  EXPECT_EQ(p.options.hostPort, 9001);
  EXPECT_TRUE(p.options.bigMap);

  // A word that is not a port is not swallowed: it is an unexpected argument.
  p = parse({ "--host", "extra" });
  EXPECT_TRUE(p.options.error);
  EXPECT_TRUE(parse({ "--host=0" }).options.error);
  EXPECT_TRUE(parse({ "--host=70000" }).options.error);
  EXPECT_TRUE(parse({ "--host=abc" }).options.error);
}

TEST(GameOptionsTest, JoinNeedsHostAndPort)
{
  Parsed p = parse({ "--join", "127.0.0.1:7777", "--name", "Ann", "--local-players", "2" });
  EXPECT_FALSE(p.options.error) << p.err;
  EXPECT_EQ(p.options.join, "127.0.0.1:7777");
  EXPECT_EQ(p.options.name, "Ann");
  EXPECT_EQ(p.options.localPlayers, 2);
  EXPECT_TRUE(p.options.networked());
  EXPECT_FALSE(p.options.host);

  EXPECT_TRUE(parse({ "--join", "noport" }).options.error);
  EXPECT_TRUE(parse({ "--join" }).options.error);
}

TEST(GameOptionsTest, SpectateIsForJoiningOnly)
{
  // Brief 13.
  const Parsed p = parse({ "--join", "1.2.3.4:5", "--spectate" });
  EXPECT_FALSE(p.options.error) << p.err;
  EXPECT_TRUE(p.options.spectate);
  EXPECT_FALSE(parse({ "--join", "1.2.3.4:5" }).options.spectate);
  EXPECT_TRUE(parse({ "--spectate" }).options.error);
  EXPECT_TRUE(parse({ "--host", "--spectate" }).options.error);
}

TEST(GameOptionsTest, NetworkOptionsAreRefusedWhenTheyMakeNoSense)
{
  EXPECT_TRUE(parse({ "--host", "--join", "1.2.3.4:5" }).options.error);          // both
  EXPECT_TRUE(parse({ "--name", "Ann" }).options.error);                           // no network
  EXPECT_TRUE(parse({ "--local-players", "2" }).options.error);
  EXPECT_TRUE(parse({ "--join", "1.2.3.4:5", "--players", "4" }).options.error);   // the host decides
  EXPECT_TRUE(parse({ "--host", "--local-players", "0" }).options.error);
  EXPECT_TRUE(parse({ "--host", "--local-players", "7" }).options.error);
  EXPECT_TRUE(parse({ "--host", "--players", "1" }).options.error);
  EXPECT_FALSE(parse({ "--host", "--players", "4", "--name", "H" }).options.error);
  EXPECT_EQ(parse({ "--host", "--players", "4" }).options.players, 4);
}

#ifdef BITRIOT_NETPLAY
// Brief 12: through a relay.
TEST(GameOptionsTest, HostViaARelayAndJoinByRoomCode)
{
  const Parsed host = parse({ "--host", "--relay", "relay.example.org", "--name", "Alice", "--players", "4" });
  EXPECT_FALSE(host.options.error) << host.err;
  EXPECT_TRUE(host.options.host);
  EXPECT_TRUE(host.options.viaRelay());
  EXPECT_EQ(host.options.relay, "relay.example.org");
  EXPECT_TRUE(host.options.networked());

  const Parsed join = parse({ "--relay=wss://relay.example.org/vnet", "--room", "k7pq3x" });
  EXPECT_FALSE(join.options.error) << join.err;
  EXPECT_FALSE(join.options.host);
  EXPECT_EQ(join.options.room, "k7pq3x");
  EXPECT_TRUE(join.options.networked()) << "a room is a join";
  EXPECT_TRUE(join.options.viaRelay());

  const Parsed direct = parse({ "--join", "10.0.0.2:7777" });
  EXPECT_FALSE(direct.options.viaRelay()) << "direct UDP is as before";
}

TEST(GameOptionsTest, TheRelayOptionsRefuseTheCombinationsThatMeanNothing)
{
  EXPECT_TRUE(parse({ "--relay", "r.example.org" }).options.error) << "a relay with nothing to do on it";
  EXPECT_TRUE(parse({ "--room", "K7PQ3X" }).options.error) << "a room needs a relay";
  EXPECT_TRUE(parse({ "--host", "--relay", "r", "--room", "K7PQ3X" }).options.error) << "host OR join";
  EXPECT_TRUE(parse({ "--join", "10.0.0.2:7777", "--relay", "r" }).options.error) << "--join is direct UDP";
  EXPECT_TRUE(parse({ "--relay", "" }).options.error);
  EXPECT_TRUE(parse({ "--relay", "r", "--room", "" }).options.error);
  const Parsed p = parse({ "--relay", "r.example.org", "--room", "K7PQ3X", "--name", "Bob" });
  EXPECT_FALSE(p.options.error) << p.err;
}
#endif
