#include "GameOptions.h"

#include <getopt.h>

#include <iostream>
#include <string>

GameOptions parseGameOptions(int argc, char* argv[], std::ostream& err)
{
  GameOptions options;

  static const struct option longOptions[] = {
    { "fullscreen", no_argument, nullptr, 'f' },
    { "big-map", no_argument, nullptr, 'b' },
    { "help", no_argument, nullptr, 'h' },
    { "record", required_argument, nullptr, 'r' },
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
      case ':':
        err << "option --record needs a directory\n";
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
      << std::endl;
}
