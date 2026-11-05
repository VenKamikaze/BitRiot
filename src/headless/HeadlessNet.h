/*
 * HeadlessNet.h
 *
 * The `host` and `join` commands of bitriot_headless (brief 07): play networked
 * rounds with no window, over vnet's native UDP backend, with scripted inputs. Compiled
 * only when BITRIOT_NETPLAY is on. See HeadlessCli.cpp's help text for the usage.
 */
#ifndef _HEADLESSNET_H
#define _HEADLESSNET_H

#include <iosfwd>
#include <string>
#include <vector>

namespace headless
{
  // args[0] is "host" or "join". Returns an exit code of HeadlessCli.h.
  int runNetCommand(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
}

#endif
