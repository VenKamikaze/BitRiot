// Helpers for the tests that scan src/ for things the simulation must not use
// (libc random numbers, floating point). Crude on purpose: enough for a guard,
// not a C++ parser.

#ifndef BITRIOT_TESTS_SOURCESCAN_H_
#define BITRIOT_TESTS_SOURCESCAN_H_

#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace bitriot_test
{

// Strips // and /* */ comments and string literals (crudely, which is enough
// for a guard) so commentary about rand() or float does not trip it. Line breaks
// inside // comments and between statements are kept.
inline std::string stripCommentsAndStrings(const std::string& in)
{
  std::string out;
  size_t i = 0;
  while (i < in.size())
  {
    if (in.compare(i, 2, "//") == 0)
    {
      while (i < in.size() && in[i] != '\n') i++;
    }
    else if (in.compare(i, 2, "/*") == 0)
    {
      const size_t end = in.find("*/", i + 2);
      // keep the line breaks, so a line-by-line scan still sees separate lines
      const size_t stop = (end == std::string::npos) ? in.size() : end + 2;
      for (size_t k = i; k < stop; ++k)
      {
        if (in[k] == '\n') out += '\n';
      }
      i = stop;
    }
    else if (in[i] == '"')
    {
      i++;
      while (i < in.size() && in[i] != '"')
      {
        if (in[i] == '\\') i++;
        i++;
      }
      i++;
    }
    else
    {
      out += in[i++];
    }
  }
  return out;
}

inline std::string trimmed(const std::string& s)
{
  const size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return std::string();
  const size_t b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

// Every line of `source` (after stripping comments and strings) that uses
// floating point: the float / double types, a floating-point literal (1.5,
// 2.0f, .5, 1e3, 3f), <cmath> or <math.h>, or a libm function. Each is returned
// trimmed, so a caller can match it against an allowlist.
inline std::vector<std::string> floatingPointLines(const std::string& source)
{
  static const std::regex floating(
    R"((^|[^A-Za-z0-9_])(float|double)([^A-Za-z0-9_]|$))"                    // the types
    R"(|(^|[^A-Za-z0-9_.])([0-9]+\.[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?[fFlL]?([^A-Za-z0-9_]|$))" // 1.5  2.  .5f
    R"(|(^|[^A-Za-z0-9_.])[0-9]+[eE][+-]?[0-9]+[fFlL]?([^A-Za-z0-9_]|$))"   // 1e3
    R"(|(^|[^A-Za-z0-9_.])[0-9]+[fF]([^A-Za-z0-9_]|$))"                     // 3f
    R"(|#\s*include\s*<(cmath|math\.h)>)"
    R"(|(^|[^A-Za-z0-9_])(fabs|sqrt|pow|floor|ceil|round|lround|fmod|sin|cos|tan|atan2?|exp|log10?)f?\s*\()");

  std::vector<std::string> hits;
  std::istringstream lines(stripCommentsAndStrings(source));
  std::string line;
  while (std::getline(lines, line))
  {
    if (std::regex_search(line, floating))
    {
      hits.push_back(trimmed(line));
    }
  }
  return hits;
}

} // namespace bitriot_test

#endif // BITRIOT_TESTS_SOURCESCAN_H_
