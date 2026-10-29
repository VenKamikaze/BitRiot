// bitriot_headless: the simulation with no window. See HeadlessCli.h.

#include <iostream>
#include <string>
#include <vector>

#include "HeadlessCli.h"

int main(int argc, char** argv)
{
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++)
  {
    args.push_back(argv[i]);
  }
  // The result goes to stdout and nothing else may: the simulation's logger writes
  // its DEBUG (Debug builds) and INFO lines to std::cout, and stdout has to be the
  // same on every build for 05b to diff it. So the results get their own stream on
  // stdout's buffer, and everything else that is written to std::cout goes to stderr.
  std::ostream out(std::cout.rdbuf());
  std::cout.rdbuf(std::cerr.rdbuf());
  const int code = headless::run(args, out, std::cerr);
  out.flush();
  return code;
}
