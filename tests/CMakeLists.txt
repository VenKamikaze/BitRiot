# Headless test harness. See tests/README.md.

set(TEST_SOURCES
  test_main.cpp
  support/HeadlessGame.cpp
  support/RmlLayoutHarness.cpp
  ScoreboardLayoutTest.cpp
  WinConditionTest.cpp
  RoundLifecycleTest.cpp
  FullRoundTest.cpp
  RoundRosterTest.cpp
  GameSettingsTest.cpp
  SpawningPoolTest.cpp
  EntitySimTest.cpp
  AISeekEnergyTest.cpp
  RoundHygieneTest.cpp
  SimRandomTest.cpp
  FixedPointSimTest.cpp
  FixedMathTest.cpp
  FixedPointGuardTest.cpp
  SimulationTest.cpp
)

add_executable(bitriot_tests ${TEST_SOURCES})

target_include_directories(bitriot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The whole project builds as C++17 (CMAKE_CXX_STANDARD in the top-level
# CMakeLists.txt), which GoogleTest >= 1.17 requires.

# SimRandomSourceGuardTest scans src/ for libc random calls, and the tests may be
# run from the build directory.
target_compile_definitions(bitriot_tests PRIVATE BITRIOT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

target_compile_options(bitriot_tests PRIVATE
  -Wall
  -Wno-comment
  $<$<CONFIG:Debug>:-ggdb -O0>
  $<$<CONFIG:Release>:-O2>
)

target_link_libraries(bitriot_tests PRIVATE
  bitriot_core
  GTest::gtest
)

# Asset paths in the game are relative ("data/renderer.txt",
# "assets/bitmaps/..."), so the tests must run from the repository root. This is
# also why the tests do not depend on the BitRiot target's data/assets symlinks.
add_test(
  NAME bitriot_tests
  COMMAND bitriot_tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
