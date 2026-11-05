#include "HeadlessNet.h"

#include <ostream>

namespace headless
{
int runNetCommand(const std::vector<std::string>&, std::ostream&, std::ostream& err)
{
  err << "error: not implemented yet\n";
  return 2;
}
}
