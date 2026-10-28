// The simulation uses no floating point (brief 02b, README D6). This scans every
// source file under src/ for float, double, floating-point literals and libm
// calls, and fails on any that is not in the allowlist below.
//
// New files are treated as simulation code until listed here, so the way to add
// presentation code that needs a float is to put it in a file listed with a
// reason, not to loosen the scan. Brief 03b creates the bitriot_sim library;
// this check can then be pointed at that target's sources instead of at src/.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "support/SourceScan.h"

namespace bitriot_test
{
namespace
{

// Whole files that are not simulation, keyed by path relative to src/.
const std::map<std::string, std::string> & nonSimFiles()
{
  static const std::map<std::string, std::string> files =
  {
    { "main.cpp", "program entry and window setup (includes <math.h>, uses none of it)" },
    { "InfoPanel.cpp", "HUD drawing: health-bar fill and portrait scaling" },
    { "MickSDLRenderer.cpp", "window scaling and letterboxing" },
    { "MickSDLUtil.h", "haptic rumble strength" },
    { "MickSDLUtil.cpp", "haptic rumble strength" },
    { "GameRunningSDLInput.h", "input layer: rumble strength passed through to SDL" },
    { "GameRunningSDLInput.cpp", "input layer: rumble strength passed through to SDL" },
  };
  return files;
}

// Directories that are not simulation, keyed by path relative to src/.
const std::map<std::string, std::string> & nonSimDirectories()
{
  static const std::map<std::string, std::string> dirs =
  {
    { "RmlUI/", "menus and the scoreboard: RmlUi's API is floats throughout" },
  };
  return dirs;
}

// Single lines allowed inside otherwise-simulation files (trimmed text, after
// comments are removed), each with the brief that removes it.
const std::map<std::string, std::map<std::string, std::string> > & allowedLines()
{
  static const std::map<std::string, std::map<std::string, std::string> > lines =
  {
    { "PlayerCharacterEntity.cpp", {
        { "MickSDLUtil::rumbleController(attachedController, 0.5f, 200);",
          "rumble strength from onCollision; brief 04b moves rumble out of the sim" },
      } },
  };
  return lines;
}

bool isUnderNonSimDirectory(const std::string & relative)
{
  for (const auto & d : nonSimDirectories())
  {
    if (relative.compare(0, d.first.size(), d.first) == 0)
    {
      return true;
    }
  }
  return false;
}

// Floating-point lines in `source` that the allowlist does not excuse.
std::vector<std::string> unexcusedFloatingPoint(const std::string & relative, const std::string & source)
{
  std::vector<std::string> bad;
  const auto lines = allowedLines().find(relative);
  for (const std::string & line : floatingPointLines(source))
  {
    if (lines != allowedLines().end() && lines->second.count(line))
    {
      continue;
    }
    bad.push_back(line);
  }
  return bad;
}

TEST(FixedPointSourceGuardTest, NoFloatingPointInSimulationSources)
{
  namespace fs = std::filesystem;
  const fs::path srcDir = fs::path(BITRIOT_SOURCE_DIR) / "src";
  ASSERT_TRUE(fs::exists(srcDir)) << srcDir;

  int scanned = 0;
  std::set<std::string> filesSeen;
  std::vector<std::string> offenders;
  for (const auto & entry : fs::recursive_directory_iterator(srcDir))
  {
    const std::string ext = entry.path().extension().string();
    if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".hpp"))
    {
      continue;
    }
    const std::string relative = fs::relative(entry.path(), srcDir).generic_string();
    filesSeen.insert(relative);
    if (isUnderNonSimDirectory(relative) || nonSimFiles().count(relative))
    {
      continue;
    }
    scanned++;
    std::ifstream in(entry.path());
    std::stringstream buf;
    buf << in.rdbuf();
    for (const std::string & line : unexcusedFloatingPoint(relative, buf.str()))
    {
      offenders.push_back(relative + ": " + line);
    }
  }
  EXPECT_GT(scanned, 50);
  for (const std::string & o : offenders)
  {
    ADD_FAILURE() << "floating point in a simulation file: " << o
                  << "\n  Use fixed_t (src/Fixed.h). If this is presentation code, move it to a file "
                     "listed in tests/FixedPointGuardTest.cpp with a reason.";
  }

  // An allowlist entry for a file or line that no longer exists is stale.
  for (const auto & f : nonSimFiles())
  {
    EXPECT_TRUE(filesSeen.count(f.first)) << "allowlisted file is gone: " << f.first;
  }
  for (const auto & f : allowedLines())
  {
    ASSERT_TRUE(filesSeen.count(f.first)) << "allowlisted file is gone: " << f.first;
    std::ifstream in(srcDir / f.first);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::vector<std::string> present = floatingPointLines(buf.str());
    for (const auto & line : f.second)
    {
      EXPECT_NE(std::find(present.begin(), present.end(), line.first), present.end())
          << "allowlisted line is gone from " << f.first << ": " << line.first;
    }
  }
}

// The scan itself: what it catches and what it leaves alone.
TEST(FixedPointSourceGuardTest, TheScanCatchesFloatingPointAndIgnoresLookalikes)
{
  EXPECT_EQ(floatingPointLines("float x;").size(), 1u);
  EXPECT_EQ(floatingPointLines("double d = 3;").size(), 1u);
  EXPECT_EQ(floatingPointLines("const float & r;").size(), 1u);
  EXPECT_EQ(floatingPointLines("(float)n").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 0.5f;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 0.0F;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 2.;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = .25;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 1e3;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 1.5e-3;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 3f;").size(), 1u);
  EXPECT_EQ(floatingPointLines("#include <cmath>").size(), 1u);
  EXPECT_EQ(floatingPointLines("#include <math.h>").size(), 1u);
  EXPECT_EQ(floatingPointLines("y = std::sqrt(v);").size(), 1u);
  EXPECT_EQ(floatingPointLines("y = floorf(v);").size(), 1u);

  EXPECT_EQ(floatingPointLines("int x = 5;").size(), 0u);
  EXPECT_EQ(floatingPointLines("int floating = 1;").size(), 0u);
  EXPECT_EQ(floatingPointLines("int doubled = 1;").size(), 0u);
  EXPECT_EQ(floatingPointLines("int m_float2;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = a.b;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = player1.field;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = v[2].y;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = 0x1f;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = 5e;").size(), 0u);
  EXPECT_EQ(floatingPointLines("fixed_t v = 0;").size(), 0u);
  EXPECT_EQ(floatingPointLines("round_robin();").size(), 0u);
  EXPECT_EQ(floatingPointLines("// float x = 1.5f;").size(), 0u);
  EXPECT_EQ(floatingPointLines("/* double */ int x;").size(), 0u);
  EXPECT_EQ(floatingPointLines("log(\"1.5 float\");").size(), 0u);
  EXPECT_EQ(floatingPointLines("int x; // 0.5f").size(), 0u);
}

TEST(FixedPointSourceGuardTest, AnAllowlistedLineIsExcusedOnlyInItsOwnFile)
{
  const std::string rumble = "MickSDLUtil::rumbleController(attachedController, 0.5f, 200);";
  EXPECT_TRUE(unexcusedFloatingPoint("PlayerCharacterEntity.cpp", rumble).empty());
  EXPECT_EQ(unexcusedFloatingPoint("RobotEntity.cpp", rumble).size(), 1u);
  EXPECT_EQ(unexcusedFloatingPoint("PlayerCharacterEntity.cpp", "float x;").size(), 1u);
  EXPECT_EQ(unexcusedFloatingPoint("PlayerCharacterEntity.cpp",
                                   "MickSDLUtil::rumbleController(attachedController, 0.9f, 200);").size(), 1u);
}

} // namespace
} // namespace bitriot_test
