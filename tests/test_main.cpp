// Test entry point, shared by both test executables.
//
// A custom main (rather than gtest_main) is needed so the one-time setup in
// HeadlessEnvironment runs before any test, and so the RNG can be seeded - the
// simulation draws from SimRandom, and the harness derives each test's round
// seeds from the base seed chosen here (see HeadlessGame.h).
//
// bitriot_sim_tests links the simulation library alone (no SDL, no RmlUi) and
// builds this file as it is. bitriot_tests links the whole game and defines
// BITRIOT_TESTS_WITH_CORE, which also brings up SDL and RmlUi for the tests that
// need them.

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <iostream>

#include "support/HeadlessGame.h"
#ifdef BITRIOT_TESTS_WITH_CORE
#include "support/RmlLayoutHarness.h"
#endif

namespace bitriot_test
{
bool linkedWithCore()
{
#ifdef BITRIOT_TESTS_WITH_CORE
  return true;
#else
  return false;
#endif
}
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // Deterministic by default so a failure reproduces. Override with
  // BITRIOT_TEST_SEED=<n> (or 0 for a time-based seed) to shake out
  // order/luck-dependent behaviour.
  uint64_t seed = 1;
  if (const char* fromEnv = std::getenv("BITRIOT_TEST_SEED"))
  {
    seed = (uint64_t) std::strtoull(fromEnv, nullptr, 10);
    if (seed == 0)
    {
      seed = (uint64_t) std::time(nullptr);
    }
    std::cerr << "[bitriot] RNG seed: " << seed << std::endl;
  }
  bitriot_test::HeadlessEnvironment::setBaseSeed(seed);

  // Environments run SetUp in registration order.
  ::testing::AddGlobalTestEnvironment(new bitriot_test::HeadlessEnvironment());
#ifdef BITRIOT_TESTS_WITH_CORE
  ::testing::AddGlobalTestEnvironment(new bitriot_test::RmlEnvironment());
#endif

  return RUN_ALL_TESTS();
}
