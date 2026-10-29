// The build id (brief 05): a hash of the simulation's source, generated at build time
// by cmake/GenerateBuildId.cmake. Two kinds of test: the id this binary was built with
// (is it the generated one, is it made of the right files), and the generator itself,
// run on small trees made for the purpose.

#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "BuildId.h"
#include "Simulation.h"
#include "support/SimSources.h"

namespace fs = std::filesystem;

namespace
{
std::string slurp(const fs::path& path)
{
  std::ifstream in(path.string().c_str(), std::ios::binary);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

void spit(const fs::path& path, const std::string& text)
{
  fs::create_directories(path.parent_path());
  std::ofstream out(path.string().c_str(), std::ios::binary | std::ios::trunc);
  out << text;
}

std::set<std::string> lines(const std::string& text)
{
  std::set<std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
  {
    if (!line.empty())
    {
      out.insert(line);
    }
  }
  return out;
}

// A throwaway source tree and the generator run over it.
class Tree
{
  public:
    Tree()
    {
      static int counter = 0;
      m_root = fs::temp_directory_path() / ("bitriot_buildid_" + std::to_string(getpid()) + "_" + std::to_string(counter++));
      fs::remove_all(m_root);
      fs::create_directories(m_root / "src");
      write("src/a.cpp", "#include \"a.h\"\n#include <vector>\nint a() { return 1; }\n");
      write("src/a.h", "#ifndef A_H\n#define A_H\n#include \"b.h\"\n#endif\n");
      write("src/b.h", "#ifndef B_H\n#define B_H\n#include \"deep/c.h\"\n#endif\n");
      write("src/deep/c.h", "#ifndef C_H\n#define C_H\n#define C 1\n#endif\n");
      write("src/render.h", "// not included by the simulation\n");
      write("src/render.cpp", "#include \"render.h\"\n");
      write("roots.txt", "./src/a.cpp\n");
    }
    ~Tree()
    {
      std::error_code ignored;
      fs::remove_all(m_root, ignored);
    }

    void write(const std::string& rel, const std::string& text) { spit(m_root / rel, text); }
    std::string read(const std::string& rel) const { return slurp(m_root / rel); }
    fs::path path(const std::string& rel) const { return m_root / rel; }

    // Runs the generator; returns its exit status.
    int generate()
    {
      const std::string cmd = std::string("\"") + BITRIOT_CMAKE_COMMAND + "\" -DBITRIOT_SOURCE_DIR=\"" + m_root.string()
          + "\" -DBITRIOT_ROOTS=\"" + (m_root / "roots.txt").string() + "\" -DBITRIOT_OUTPUT=\"" + (m_root / "out" / "BuildIdValue.cpp").string()
          + "\" -P \"" + BITRIOT_BUILD_ID_SCRIPT + "\" > /dev/null 2>&1";
      return std::system(cmd.c_str());
    }

    // "0123456789abcdef" from the generated file.
    std::string id()
    {
      const std::string cpp = read("out/BuildIdValue.cpp");
      const size_t at = cpp.find("return 0x");
      return at == std::string::npos ? "" : cpp.substr(at + 9, 16);
    }
    std::string generatedId()
    {
      EXPECT_EQ(generate(), 0);
      return id();
    }

  private:
    fs::path m_root;
};
} // namespace

// ---------------------------------------------------------------------------
// This binary's id
// ---------------------------------------------------------------------------

TEST(BuildIdTest, TheBinaryHasTheIdTheGeneratorWrote)
{
  EXPECT_NE(bitriotBuildId(), 0u);
  EXPECT_EQ(Simulation::buildId(), bitriotBuildId());
  EXPECT_EQ(bitriotBuildIdString().size(), 16u);
  EXPECT_EQ(bitriotHex64(0x0123456789abcdefULL), "0123456789abcdef");
  EXPECT_EQ(bitriotHex64(0), "0000000000000000");
  EXPECT_EQ(bitriotHex64(0xffffffffffffffffULL), "ffffffffffffffff");

  const std::string cpp = slurp(fs::path(BITRIOT_BUILD_ID_DIR) / "BuildIdValue.cpp");
  EXPECT_NE(cpp.find("return 0x" + bitriotBuildIdString() + "ULL"), std::string::npos)
      << "the compiled id is not the one in the generated file: the build id is stale";
}

TEST(BuildIdTest, TheIdIsMadeOfTheSimulationsFilesAndNothingElse)
{
  const std::set<std::string> inputs = lines(slurp(fs::path(BITRIOT_BUILD_ID_DIR) / "BuildIdValue.inputs.txt"));
  for (const char* wanted : { "src/Simulation.cpp", "src/Simulation.h", "src/TickInputs.h", "src/Fixed.h", "src/DataHash.cpp",
                              "src/PlayerCharacterEntity.cpp", "src/MatchRecord.h", "src/StateHash.h", "src/BuildId.h" })
  {
    EXPECT_EQ(inputs.count(wanted), 1u) << wanted;
  }
  for (const char* unwanted : { "src/GameEngine.cpp", "src/GameEngine.h", "src/InfoPanel.h", "src/main.cpp", "src/InputHandler.h",
                                "src/WorldRenderer.cpp", "src/FrameLoop.h", "src/headless/HeadlessCli.cpp" })
  {
    EXPECT_EQ(inputs.count(unwanted), 0u) << unwanted << " is not simulation code";
  }
  // Every file of the bitriot_sim target (but the generated one) is in it.
  namespace fs = std::filesystem;
  for (const fs::path& file : bitriot_test::simFileClosure())
  {
    if (file.generic_string().rfind(fs::path(BITRIOT_BUILD_ID_DIR).generic_string(), 0) == 0)
    {
      continue; // the generated source itself
    }
    const std::string rel = fs::relative(file, BITRIOT_SOURCE_DIR).generic_string();
    EXPECT_EQ(inputs.count(rel), 1u) << rel << " is simulation code but is not in the build id";
  }
}

// ---------------------------------------------------------------------------
// The generator
// ---------------------------------------------------------------------------

TEST(BuildIdTest, TheGeneratorFollowsIncludesThroughHeadersAndDirectories)
{
  Tree tree;
  ASSERT_EQ(tree.generate(), 0);
  EXPECT_EQ(tree.id().size(), 16u);
  const std::set<std::string> inputs = lines(tree.read("out/BuildIdValue.inputs.txt"));
  EXPECT_EQ(inputs, (std::set<std::string>{ "src/a.cpp", "src/a.h", "src/b.h", "src/deep/c.h" }));
}

TEST(BuildIdTest, TheSameSourceGivesTheSameId)
{
  Tree one;
  Tree two;
  EXPECT_EQ(one.generatedId(), two.generatedId());
  EXPECT_EQ(one.generatedId(), one.generatedId());
}

TEST(BuildIdTest, EditingAnyFileTheSimulationReachesChangesTheId)
{
  Tree tree;
  const std::string base = tree.generatedId();

  tree.write("src/a.cpp", tree.read("src/a.cpp") + "// edit\n");
  const std::string afterSource = tree.generatedId();
  EXPECT_NE(afterSource, base);

  tree.write("src/deep/c.h", tree.read("src/deep/c.h") + "// edit two levels down\n");
  const std::string afterHeader = tree.generatedId();
  EXPECT_NE(afterHeader, afterSource);

  tree.write("src/b.h", "#ifndef B_H\n#define B_H\n#include \"deep/c.h\"\n#include \"extra.h\"\n#endif\n");
  tree.write("src/extra.h", "// a new header\n");
  const std::string afterNewInclude = tree.generatedId();
  EXPECT_NE(afterNewInclude, afterHeader);
  EXPECT_EQ(lines(tree.read("out/BuildIdValue.inputs.txt")).count("src/extra.h"), 1u);

  tree.write("src/extra.h", "// a new header, changed\n");
  EXPECT_NE(tree.generatedId(), afterNewInclude);
}

TEST(BuildIdTest, AFileTheSimulationDoesNotReachDoesNotMoveTheId)
{
  Tree tree;
  const std::string base = tree.generatedId();
  tree.write("src/render.h", "// changed\n");
  tree.write("src/render.cpp", "#include \"render.h\"\nint draw() { return 2; }\n");
  tree.write("src/another.cpp", "int z;\n");
  EXPECT_EQ(tree.generatedId(), base);
}

TEST(BuildIdTest, LineEndingsDoNotMoveTheId)
{
  Tree tree;
  const std::string base = tree.generatedId();
  for (const char* rel : { "src/a.cpp", "src/a.h", "src/b.h", "src/deep/c.h" })
  {
    std::string crlf;
    for (char c : tree.read(rel))
    {
      if (c == '\n') { crlf += "\r\n"; } else { crlf += c; }
    }
    tree.write(rel, crlf);
  }
  EXPECT_EQ(tree.generatedId(), base) << "git autocrlf on Windows must not make two builds of one commit disagree";
}

TEST(BuildIdTest, AnUnchangedIdDoesNotRewriteTheFile)
{
  Tree tree;
  ASSERT_EQ(tree.generate(), 0);
  const fs::path out = tree.path("out/BuildIdValue.cpp");
  const fs::file_time_type stamp = fs::last_write_time(out);
  fs::last_write_time(out, stamp - std::chrono::hours(1));
  const fs::file_time_type old = fs::last_write_time(out);
  ASSERT_EQ(tree.generate(), 0);
  EXPECT_EQ(fs::last_write_time(out), old) << "rewriting an unchanged file would rebuild the simulation on every build";
}

TEST(BuildIdTest, TheGeneratorFailsLoudlyOnAMissingFileOrNoRoots)
{
  {
    Tree tree;
    tree.write("roots.txt", "./src/missing.cpp\n");
    EXPECT_NE(tree.generate(), 0);
  }
  {
    Tree tree;
    tree.write("roots.txt", "");
    EXPECT_NE(tree.generate(), 0);
  }
}
