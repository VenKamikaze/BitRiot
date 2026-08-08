// Test entry point.
//
// A custom main (rather than gtest_main) is needed so the one-time SDL/asset
// setup in HeadlessEnvironment runs before any test, and so the RNG can be
// pinned - SpawningPool and several entity behaviours call rand() directly.

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "support/HeadlessGame.h"
#include "support/RmlLayoutHarness.h"

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Deterministic by default so a failure reproduces. Override with
  // BITRIOT_TEST_SEED=<n> (or 0 for a time-based seed) to shake out
  // order/luck-dependent behaviour.
  unsigned int seed = 1;
  if (const char* fromEnv = std::getenv("BITRIOT_TEST_SEED"))
  {
    seed = (unsigned int) std::strtoul(fromEnv, nullptr, 10);
    if (seed == 0)
    {
      seed = (unsigned int) std::time(nullptr);
    }
    std::cerr << "[bitriot] RNG seed: " << seed << std::endl;
  }
  std::srand(seed);

  // Order matters: HeadlessEnvironment brings up SDL and the entity renderers,
  // which the Rml layout tests' player entities need before RmlUi is asked for
  // anything. Environments run SetUp in registration order.
  ::testing::AddGlobalTestEnvironment(new bitriot_test::HeadlessEnvironment());
  ::testing::AddGlobalTestEnvironment(new bitriot_test::RmlEnvironment());

  return RUN_ALL_TESTS();
}
