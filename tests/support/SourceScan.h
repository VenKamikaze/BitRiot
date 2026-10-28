// Helpers for the tests that scan src/ for things the simulation must not use
// (libc random numbers, floating point). Crude on purpose: enough for a guard,
// not a C++ parser.

#ifndef BITRIOT_TESTS_SOURCESCAN_H_
#define BITRIOT_TESTS_SOURCESCAN_H_

#include <cctype>
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
    else if (in[i] == '\'' && (out.empty() || !std::isalnum((unsigned char)out.back())))
    {
      // a character literal such as '"' or '\'' (not a digit separator, which
      // follows a digit): skip it, so a quote inside it does not start a string
      i++;
      if (i < in.size() && in[i] == '\\') i++;
      i++;
      if (i < in.size() && in[i] == '\'') i++;
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
// floating point: the float / double types and their aliases (float_t, GLfloat,
// SDL_FPoint, ...), a floating-point literal (1.5, 2.0f, .5, 1e3, 3f, 0x1p-1),
// <cmath> or <math.h>, or a libm / string-to-float function. Each is returned
// trimmed, so a caller can match it against an allowlist.
//
// What a text scan cannot see: `auto` initialised from a function that returns a
// double, and implicit int <-> float conversions at a call (an int passed to a
// float parameter). Those need an object-level check; see brief 03b's follow-up.
inline std::vector<std::string> floatingPointLines(const std::string& source)
{
  static const std::regex floating(
    // the types and their aliases
    R"((^|[^A-Za-z0-9_])(float|double|float_t|double_t|GLfloat|GLdouble|SDL_FPoint|SDL_FRect)([^A-Za-z0-9_]|$))"
    // 1.5  2.  .5f  1.5e-3
    R"(|(^|[^A-Za-z0-9_.])([0-9]+\.[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?[fFlL]?([^A-Za-z0-9_]|$))"
    // 1e3
    R"(|(^|[^A-Za-z0-9_.])[0-9]+[eE][+-]?[0-9]+[fFlL]?([^A-Za-z0-9_]|$))"
    // 3f
    R"(|(^|[^A-Za-z0-9_.])[0-9]+[fF]([^A-Za-z0-9_]|$))"
    // hex floats: 0x1p-1, 0x1.8p3
    R"(|(^|[^A-Za-z0-9_.])0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*[pP][+-]?[0-9]+[fFlL]?)"
    R"(|#\s*include\s*<(cmath|math\.h)>)"
    // libm, with an optional f / l suffix, and the string-to-float functions
    R"(|(^|[^A-Za-z0-9_])(fabs|sqrt|cbrt|hypot|pow|floor|ceil|round|lround|llround|trunc|rint|lrint|nearbyint|fmod|remainder|fma|fmin|fmax|fdim|ldexp|frexp|modf|copysign|sin|cos|tan|asin|acos|atan2?|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1|log2|log10|log1p)[fl]?\s*\()"
    // plain log( is also a logging method name, so only the qualified form counts
    R"(|std::log[fl]?\s*\(|(^|[^A-Za-z0-9_:])::log[fl]?\s*\()"
    R"(|(^|[^A-Za-z0-9_])(atof|strtof|strtod|strtold|stof|stod|stold)\s*\()");

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
