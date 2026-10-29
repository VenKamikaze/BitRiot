/*
 * GameOptions.h
 *
 * The game's command line, parsed with getopt_long. Kept out of main() so it can be
 * tested; it touches no global state of the game (main() applies the result).
 *
 *   -f, --fullscreen     scale to fullscreen
 *   -b, --big-map        use the big (25x19) map
 *   -h, --help           show the usage and exit
 *       --record <dir>   write a match record of every round into <dir> (brief 05:
 *                        one file per round, doco/netplay/match-record-format.md)
 */

#ifndef _GAMEOPTIONS_H
#define _GAMEOPTIONS_H

#include <iosfwd>
#include <string>

struct GameOptions
{
  bool fullscreen = false;
  bool bigMap = false;
  bool help = false;
  std::string recordDir;   // empty: do not record
  bool error = false;      // a bad option or a missing argument (already reported on `err`)
};

// Parses argv (argv[0] is the program name). Reports a problem on `err` and sets
// `error`. Safe to call more than once in a process.
GameOptions parseGameOptions(int argc, char* argv[], std::ostream& err);

void printGameUsage(const std::string& exec, std::ostream& out);

#endif /* _GAMEOPTIONS_H */
