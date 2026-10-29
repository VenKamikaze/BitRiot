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
  return headless::run(args, std::cout, std::cerr);
}
