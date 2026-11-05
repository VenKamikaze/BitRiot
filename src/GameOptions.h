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
 *
 * Network play (brief 07; only in a build with BITRIOT_NETPLAY):
 *       --host [port]    host a lockstep game on a UDP port (default 7777); also --host=PORT
 *       --join host:port join one
 *       --name <name>    the name the others see
 *       --local-players N  players on this machine (keyboard sets 1..N; default 1)
 *       --players N      host: player slots in all (the rest are computer players)
 *
 * Through a vnet relay (brief 12): `--host --relay ADDR` opens a room (its code is printed and shown in
 * the lobby), `--relay ADDR --room CODE` joins one. ADDR is host[:port] (the relay's UDP port, 7777 by
 * default) or a ws:// / wss:// URL.
 *       --relay ADDR     the relay to use
 *       --room CODE      join this room (with --relay)
 *       --spectate       join: watch only, never take a player over (a round already running is joined and watched)
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
  // Network play (brief 07).
  bool host = false;
  int hostPort = 7777;
  std::string join;        // "host:port", empty: not joining
  bool spectate = false;   // join: watch only (brief 13)
  std::string name;
  int localPlayers = 1;
  int players = 0;         // 0: as many as there are humans (at least 2)
  std::string relay;       // brief 12: "" = direct UDP
  std::string room;        // a joiner's room code
  bool networked() const { return host || !join.empty() || !room.empty(); }
  bool viaRelay() const { return !relay.empty(); }
  bool error = false;      // a bad option or a missing argument (already reported on `err`)
};

// Parses argv (argv[0] is the program name). Reports a problem on `err` and sets
// `error`. Safe to call more than once in a process.
GameOptions parseGameOptions(int argc, char* argv[], std::ostream& err);

void printGameUsage(const std::string& exec, std::ostream& out);

#endif /* _GAMEOPTIONS_H */
