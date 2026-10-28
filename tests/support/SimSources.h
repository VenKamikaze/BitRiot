// Which files are the simulation, for the tests that scan them.
//
// The list comes from the build: CMake writes the bitriot_sim target's SOURCES to
// a file (BITRIOT_SIM_SOURCES_FILE) at generate time, so "the simulation" cannot
// drift from what is actually compiled into bitriot_sim. The closure adds every
// project header those sources include, directly or not, because a header is sim
// code too.

#ifndef BITRIOT_TESTS_SIMSOURCES_H_
#define BITRIOT_TESTS_SIMSOURCES_H_

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bitriot_test
{

inline std::string readFile(const std::filesystem::path& path)
{
  std::ifstream in(path);
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

// The bitriot_sim sources as absolute, normalised paths. Empty if the list file is
// missing (a test then fails on its own EXPECT_GT).
inline std::vector<std::filesystem::path> simSourceFiles()
{
  namespace fs = std::filesystem;
  std::vector<fs::path> files;
  std::istringstream list(readFile(BITRIOT_SIM_SOURCES_FILE));
  std::string line;
  while (std::getline(list, line))
  {
    if (line.empty())
    {
      continue;
    }
    fs::path p(line);
    if (p.is_relative())
    {
      p = fs::path(BITRIOT_SOURCE_DIR) / p;
    }
    files.push_back(fs::weakly_canonical(p));
  }
  return files;
}

// Every `#include "..."` / `#include <...>` target in `source` (as written).
inline std::vector<std::string> includeTargets(const std::string& source)
{
  static const std::regex include(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
  std::vector<std::string> targets;
  std::istringstream lines(source);
  std::string line;
  while (std::getline(lines, line))
  {
    std::smatch m;
    if (std::regex_search(line, m, include))
    {
      targets.push_back(m[1].str());
    }
  }
  return targets;
}

// The sources and every project header reachable from them, absolute and
// normalised. An include resolves next to the including file, then under src/,
// then under the repository root, like the compiler's include path does; one that
// resolves nowhere (a standard or system header) is not part of the closure.
inline std::set<std::filesystem::path> simFileClosure()
{
  namespace fs = std::filesystem;
  std::set<fs::path> seen;
  std::vector<fs::path> pending = simSourceFiles();
  const fs::path root(BITRIOT_SOURCE_DIR);
  while (!pending.empty())
  {
    const fs::path file = pending.back();
    pending.pop_back();
    if (!seen.insert(file).second)
    {
      continue;
    }
    for (const std::string& target : includeTargets(readFile(file)))
    {
      for (const fs::path& base : { file.parent_path(), root / "src", root })
      {
        const fs::path candidate = base / target;
        if (fs::is_regular_file(candidate))
        {
          pending.push_back(fs::weakly_canonical(candidate));
          break;
        }
      }
    }
  }
  return seen;
}

} // namespace bitriot_test

#endif // BITRIOT_TESTS_SIMSOURCES_H_
