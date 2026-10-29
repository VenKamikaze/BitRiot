#include "RumbleSink.h"

namespace
{
RumbleSink* s_installed = nullptr;

class DiscardingRumbleSink : public RumbleSink
{
  public:
    void rumble(int, int, int) override {}
};
}

RumbleSink* RumbleSink::getInstance()
{
  if (s_installed)
  {
    return s_installed;
  }
  static DiscardingRumbleSink discard;
  return &discard;
}

void RumbleSink::setInstance(RumbleSink* sink)
{
  s_installed = sink;
}
