// The simulation uses no floating point (brief 02b, README D6). This scans the
// text of the simulation for float, double, floating-point literals and libm
// calls.
//
// "The simulation" is the bitriot_sim target (brief 03b): its sources, read from
// the list CMake generates from the target, plus every project header they
// include. There is no allowlist any more - a file that needs a float belongs in
// bitriot_core (rendering, the HUD, the menus), which this does not scan - and a
// new file that is added to bitriot_sim is held to the rule by default.
//
// A text scan cannot see an `auto` carrying a double or an int converted to a
// float parameter; SimLibraryTest's object-level check on the compiled archive
// covers those.

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "support/SimSources.h"
#include "support/SourceScan.h"

namespace bitriot_test
{
namespace
{

TEST(FixedPointSourceGuardTest, NoFloatingPointInSimulationSources)
{
  namespace fs = std::filesystem;
  const std::vector<fs::path> sources = simSourceFiles();
  const std::set<fs::path> files = simFileClosure();
  ASSERT_GT(sources.size(), 35u) << "the bitriot_sim source list is missing: " << BITRIOT_SIM_SOURCES_FILE;
  EXPECT_GT(files.size(), sources.size()) << "no headers found; the include scan is broken";

  const fs::path srcDir = fs::path(BITRIOT_SOURCE_DIR) / "src";
  std::vector<std::string> offenders;
  for (const fs::path& file : files)
  {
    ASSERT_TRUE(fs::exists(file)) << file;
    for (const std::string& line : floatingPointLines(readFile(file)))
    {
      offenders.push_back(fs::relative(file, srcDir).generic_string() + ": " + line);
    }
  }
  for (const std::string& o : offenders)
  {
    ADD_FAILURE() << "floating point in a simulation file: " << o
                  << "\n  Use fixed_t (src/Fixed.h). If this is presentation code, it belongs in "
                     "bitriot_core, not bitriot_sim (CMakeLists.txt).";
  }
}

// The files the guard reads are the ones that are really the simulation: a
// presentation file does not sneak in, and the entity headers do.
TEST(FixedPointSourceGuardTest, TheGuardReadsTheSimulationAndNotTheRenderer)
{
  namespace fs = std::filesystem;
  const fs::path srcDir = fs::path(BITRIOT_SOURCE_DIR) / "src";
  std::set<std::string> names;
  for (const fs::path& file : simFileClosure())
  {
    names.insert(fs::relative(file, srcDir).generic_string());
  }

  for (const char* sim : { "Simulation.cpp", "Simulation.h", "GameEntity.h", "PlayerCharacterEntity.cpp",
                           "Fixed.h", "Map.h", "SimRandom.cpp", "GameSettings.cpp", "EntityAnim.cpp" })
  {
    EXPECT_TRUE(names.count(sim)) << sim << " is simulation code but the guard does not read it";
  }
  for (const char* notSim : { "InfoPanel.cpp", "EntityRenderer.cpp", "MickSDLRenderer.cpp", "WorldRenderer.cpp",
                              "MapRenderer.cpp", "GameEngine.cpp", "FrameLoop.cpp", "MickSDLUtil.h",
                              "RmlUI/binders/ScoreBoardBinder.cpp" })
  {
    EXPECT_FALSE(names.count(notSim)) << notSim << " is not simulation code but the guard reads it";
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
  // libm beyond the obvious, and string-to-float
  EXPECT_EQ(floatingPointLines("n = (int)hypot(3, 4);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = std::exp2(3);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = log2(8);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = std::log(8);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = ::log(8);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = cbrt(27);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = trunc(v);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = fmin(a, b) + fmax(a, b);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = asin(a) + acos(a) + sinh(a) + cosh(a) + tanh(a);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = atof(text);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = std::stod(text);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = strtod(text, 0);").size(), 1u);
  EXPECT_EQ(floatingPointLines("n = sqrtf(v);").size(), 1u);
  // type aliases
  EXPECT_EQ(floatingPointLines("float_t x;").size(), 1u);
  EXPECT_EQ(floatingPointLines("double_t x;").size(), 1u);
  EXPECT_EQ(floatingPointLines("GLfloat x;").size(), 1u);
  EXPECT_EQ(floatingPointLines("SDL_FPoint p;").size(), 1u);
  EXPECT_EQ(floatingPointLines("long double x;").size(), 1u);
  // hex floats
  EXPECT_EQ(floatingPointLines("x = 0x1p-1;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 0x1.8p3;").size(), 1u);
  EXPECT_EQ(floatingPointLines("x = 0X.8P1f;").size(), 1u);
  // a quote inside a character literal does not hide what follows it
  EXPECT_EQ(floatingPointLines("char q = '\"'; float x;").size(), 1u);
  EXPECT_EQ(floatingPointLines("char q = '\''; double x;").size(), 1u);

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
  EXPECT_EQ(floatingPointLines("warn(\"1.5 float\");").size(), 0u);
  EXPECT_EQ(floatingPointLines("int x; // 0.5f").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = 0x1f;").size(), 0u);
  EXPECT_EQ(floatingPointLines("x = 0xABCDEF;").size(), 0u);
  EXPECT_EQ(floatingPointLines("int hypotenuse = 1;").size(), 0u);
  EXPECT_EQ(floatingPointLines("log(callingClass, message, DEBUG);").size(), 0u);
  EXPECT_EQ(floatingPointLines("logger->log(a, b);").size(), 0u);
  EXPECT_EQ(floatingPointLines("void MickLogger::log(void* c, const char *m);").size(), 0u);
  EXPECT_EQ(floatingPointLines("int logged = 1; int expected = 2;").size(), 0u);
  EXPECT_EQ(floatingPointLines("char q = '\"'; int x = 1;").size(), 0u);
  EXPECT_EQ(floatingPointLines("int big = 1'000'000;").size(), 0u);
}

} // namespace
} // namespace bitriot_test
