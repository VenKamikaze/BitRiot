#include "BuildId.h"

std::string bitriotHex64(uint64_t value)
{
  static const char DIGITS[] = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; i--)
  {
    s[i] = DIGITS[value & 0xf];
    value >>= 4;
  }
  return s;
}
