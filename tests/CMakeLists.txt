# Headless test harness. See tests/README.md.

# Two executables, one set of simulation tests.
#
#   bitriot_sim_tests  links bitriot_sim and GoogleTest only: no SDL, no RmlUi, no
#                      OpenGL. It runs whole rounds, so it proves by linking that the
#                      simulation needs none of them.
#   bitriot_tests      (only with BITRIOT_BUILD_GAME=ON, the default) links the whole
#                      game (bitriot_core) and adds the tests that
#                      need it: the scoreboard layout, the frame loop, and the tile
#                      counts against the real bitmaps. It also runs every simulation
#                      test again, against the game's own library.
#
# The simulation tests are compiled once, as an object library, and linked into both.
set(SIM_TEST_SOURCES
  support/HeadlessGame.cpp
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
  SimLibraryTest.cpp
  MapTest.cpp
)

set(CORE_TEST_SOURCES
  support/RmlLayoutHarness.cpp
  ScoreboardLayoutTest.cpp
  ScoreboardRandomTest.cpp
  FrameLoopTest.cpp
  RenderSideTest.cpp
)

add_library(bitriot_sim_test_objs OBJECT ${SIM_TEST_SOURCES})
target_include_directories(bitriot_sim_test_objs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitriot_sim_test_objs PRIVATE bitriot_sim GTest::gtest)

# The whole project builds as C++17 (CMAKE_CXX_STANDARD in the top-level
# CMakeLists.txt), which GoogleTest >= 1.17 requires.

# SimRandomSourceGuardTest scans src/ for libc random calls, and the tests may be
# run from the build directory. The sim guards read the generated source list and
# the archive's path.
set(BITRIOT_TEST_DEFINITIONS
  BITRIOT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  BITRIOT_SIM_SOURCES_FILE="${CMAKE_BINARY_DIR}/bitriot_sim_sources.txt"
  BITRIOT_SIM_LIBRARY="$<TARGET_FILE:bitriot_sim>"
  BITRIOT_COMPILER_TARGET_ARCH="${CMAKE_SYSTEM_PROCESSOR}"
)
target_compile_definitions(bitriot_sim_test_objs PRIVATE ${BITRIOT_TEST_DEFINITIONS})

set(BITRIOT_TEST_WARNINGS
  -Wall
  -Wno-comment
  $<$<CONFIG:Debug>:-ggdb -O0>
  $<$<CONFIG:Release>:-O2>
)
target_compile_options(bitriot_sim_test_objs PRIVATE ${BITRIOT_TEST_WARNINGS})

# --- bitriot_sim_tests: the simulation alone -------------------------------------
add_executable(bitriot_sim_tests test_main.cpp $<TARGET_OBJECTS:bitriot_sim_test_objs>)
target_include_directories(bitriot_sim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bitriot_sim_tests PRIVATE ${BITRIOT_TEST_WARNINGS})
target_link_libraries(bitriot_sim_tests PRIVATE bitriot_sim GTest::gtest)

# --- bitriot_tests: everything (only when the game is built) -------------------
if(BITRIOT_BUILD_GAME)
  add_executable(bitriot_tests test_main.cpp ${CORE_TEST_SOURCES} $<TARGET_OBJECTS:bitriot_sim_test_objs>)
  target_include_directories(bitriot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(bitriot_tests PRIVATE BITRIOT_TESTS_WITH_CORE ${BITRIOT_TEST_DEFINITIONS})
  target_compile_options(bitriot_tests PRIVATE ${BITRIOT_TEST_WARNINGS})
  target_link_libraries(bitriot_tests PRIVATE bitriot_core GTest::gtest)
endif()

# The tests read data/ (and, in bitriot_tests, assets/) relative to the working
# directory. Building either executable creates the build directory's links, so
# running it from there works after building only that target.
if(TARGET bitriot_runtime_links)
  add_dependencies(bitriot_sim_tests bitriot_runtime_links)
  if(BITRIOT_BUILD_GAME)
    add_dependencies(bitriot_tests bitriot_runtime_links)
  endif()
endif()

# Asset paths in the game are relative ("data/renderer.txt",
# "assets/bitmaps/..."), so the tests must run from the repository root. This is
# also why the tests do not depend on the BitRiot target's data/assets symlinks.
if(BITRIOT_BUILD_GAME)
  add_test(
    NAME bitriot_tests
    COMMAND bitriot_tests
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endif()
add_test(
  NAME bitriot_sim_tests
  COMMAND bitriot_sim_tests
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
