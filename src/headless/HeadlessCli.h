/*
 * HeadlessCli.h
 *
 * The command line of bitriot_headless: play, replay and inspect rounds with no
 * window, no audio and no bitmaps. It links bitriot_sim only. The commands are
 * functions here (not in main()) so the tests can call them in-process; the real
 * binary is src/headless/main.cpp, which only forwards its arguments.
 *
 *   bitriot_headless replay <file> [--hashes] [--ignore-data-hash]
 *   bitriot_headless run --out <file> [--config SPEC] [--seed N] [--script FILE]
 *                    [--max-ticks N] [--checkpoint-every K] [--no-rle]
 *   bitriot_headless dump <file> [--tick N]
 *   bitriot_headless version [--items]
 *   bitriot_headless help
 *
 * The full text is printHelp() in HeadlessCli.cpp, and the files and exit codes are
 * in doco/netplay/match-record-format.md ("bitriot_headless"). Briefs 05b (the
 * cross-platform gate), 07 (host/join) and 13 (hot-join fast-forward) build on this
 * interface: stdout is the result and is the same on every build for the same input;
 * diagnostics that depend on the build go to stderr.
 */

#ifndef _HEADLESSCLI_H
#define _HEADLESSCLI_H

#include <iosfwd>
#include <string>
#include <vector>

#include "MatchConfig.h"

namespace headless
{
  // Exit codes.
  const int EXIT_OK = 0;       // success
  const int EXIT_FAILED = 1;   // the replay did not match, or the run could not finish a record
  const int EXIT_USAGE = 2;    // bad command line, unreadable or malformed file
  const int EXIT_DATA = 3;     // the record was made on different game data

  // Runs one command line (argv without the program name). Results go to `out`,
  // problems and build-dependent notes to `err`. Returns the exit code.
  // One Simulation may exist per process: a caller that already has one gets
  // EXIT_USAGE and a message.
  int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

  // Parses a --config spec: comma-separated key=value pairs (or "@file" holding the
  // same, one pair per line or comma-separated):
  //   players=N   map=WxH   blocks=P   spawner=on|off   teams=a:b:c   humans=0:1
  // Slots are 0-based (slot 0 is player 1), as in a ControlEvent. `base` supplies
  // every value the spec does not set. Throws std::invalid_argument with a
  // message naming the bad key.
  MatchConfig parseConfigSpec(const std::string& spec, const MatchConfig& base);

  // "0x..." or decimal. Throws std::invalid_argument.
  unsigned long long parseNumber(const std::string& text, const std::string& what);
}

#endif /* _HEADLESSCLI_H */
