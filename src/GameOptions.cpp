#include "GameOptions.h"

#include <getopt.h>

#include <iostream>
#include <string>

namespace
{
bool parseNumber(const char* text, int min, int max, int* out)
{
  if (text == nullptr || *text == '\0')
  {
    return false;
  }
  long v = 0;
  for (const char* c = text; *c != '\0'; c++)
  {
    if (*c < '0' || *c > '9')
    {
      return false;
    }
    v = v * 10 + (*c - '0');
    if (v > 100000)
    {
      return false;
    }
  }
  if (v < min || v > max)
  {
    return false;
  }
  *out = (int) v;
  return true;
}
} // namespace

GameOptions parseGameOptions(int argc, char* argv[], std::ostream& err)
{
  GameOptions options;

  static const struct option longOptions[] = {
    { "fullscreen", no_argument, nullptr, 'f' },
    { "big-map", no_argument, nullptr, 'b' },
    { "help", no_argument, nullptr, 'h' },
    { "record", required_argument, nullptr, 'r' },
    { "host", optional_argument, nullptr, 'H' },
    { "join", required_argument, nullptr, 'j' },
    { "name", required_argument, nullptr, 'n' },
    { "local-players", required_argument, nullptr, 'l' },
    { "players", required_argument, nullptr, 'p' },
    { nullptr, 0, nullptr, 0 }
  };

  // optind = 0 makes getopt_long start again (glibc, musl, MinGW), so this can be
  // called twice in one process (the tests do). opterr = 0: we word the messages.
  optind = 0;
  opterr = 0;

  int c = 0;
  while ((c = getopt_long(argc, argv, ":fbh", longOptions, nullptr)) != -1)
  {
    switch (c)
    {
      case 'f':
        options.fullscreen = true;
        break;
      case 'b':
        options.bigMap = true;
        break;
      case 'h':
        options.help = true;
        break;
      case 'r':
        options.recordDir = optarg;
        if (options.recordDir.empty())
        {
          err << "option --record needs a directory\n";
          options.error = true;
        }
        break;
      case 'H':
      {
        options.host = true;
        // `--host 7777` (a separate word that is all digits) works as well as `--host=7777`.
        const char* port = optarg;
        if (port == nullptr && optind < argc && argv[optind][0] >= '0' && argv[optind][0] <= '9')
        {
          port = argv[optind++];
        }
        if (port != nullptr && !parseNumber(port, 1, 65535, &options.hostPort))
        {
          err << "--host takes a port from 1 to 65535, not \"" << port << "\"\n";
          options.error = true;
        }
        break;
      }
      case 'j':
        options.join = optarg;
        if (options.join.empty() || options.join.find(':') == std::string::npos)
        {
          err << "--join takes host:port, not \"" << options.join << "\"\n";
          options.error = true;
        }
        break;
      case 'n':
        options.name = optarg;
        break;
      case 'l':
        if (!parseNumber(optarg, 1, 6, &options.localPlayers))
        {
          err << "--local-players takes a number from 1 to 6\n";
          options.error = true;
        }
        break;
      case 'p':
        if (!parseNumber(optarg, 2, 6, &options.players))
        {
          err << "--players takes a number from 2 to 6\n";
          options.error = true;
        }
        break;
      case ':':
        if (optopt == 'r' || optopt == 0)
        {
          err << "option --record needs a directory\n";
        }
        else
        {
          err << "option " << (optind > 0 && optind <= argc ? argv[optind - 1] : "") << " needs a value\n";
        }
        options.error = true;
        break;
      case '?':
      default:
        if (optopt != 0)
        {
          err << "unknown option -" << (char) optopt << "\n";
        }
        else if (optind > 0 && optind <= argc)
        {
          err << "unknown option " << argv[optind - 1] << "\n";
        }
        else
        {
          err << "unknown option\n";
        }
        options.error = true;
        break;
    }
  }
  if (optind < argc)
  {
    err << "unexpected argument " << argv[optind] << "\n";
    options.error = true;
  }
  if (options.host && !options.join.empty())
  {
    err << "--host and --join cannot be used together\n";
    options.error = true;
  }
  if (!options.networked() && (!options.name.empty() || options.localPlayers != 1 || options.players != 0))
  {
    err << "--name, --local-players and --players need --host or --join\n";
    options.error = true;
  }
  if (!options.host && options.players != 0)
  {
    err << "--players is for the host: it decides how many players the game has\n";
    options.error = true;
  }
  return options;
}

void printGameUsage(const std::string& exec, std::ostream& out)
{
  out << "Usage: " << exec << " <option(s)>\n"
      << "Options:\n"
      << "\t-h, --help\t\tShow this help message\n"
      << "\t-f, --fullscreen\tScale to fullscreen\n"
      << "\t-b, --big-map\t\tUse a big map\n"
      << "\t    --record <dir>\tWrite a match record of every round into <dir>\n"
#ifdef BITRIOT_NETPLAY
      << "Network play:\n"
      << "\t    --host [port]\tHost a game on a UDP port (default 7777)\n"
      << "\t    --join host:port\tJoin a hosted game\n"
      << "\t    --name <name>\tThe name the others see\n"
      << "\t    --local-players N\tPlayers on this machine (default 1)\n"
      << "\t    --players N\t\tHost: player slots in all; the rest are computer players\n"
#endif
      << std::endl;
}
