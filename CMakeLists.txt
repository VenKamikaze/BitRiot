cmake_minimum_required(VERSION 3.15)
project(BitRiot)

# One language standard for the game, the tests and every compiler (GCC, Clang,
# Emscripten), so they cannot disagree by each using its own default (gnu++17 on
# current GCC). GoogleTest >= 1.17 needs C++17 anyway. Extensions off: plain
# -std=c++17.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#set(CMAKE_BUILD_TYPE Release)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(OpenGL_GL_PREFERENCE "GLVND")

set(RMLUI_STATIC false)
set(RMLUI_DIR ${BitRiot_SOURCE_DIR}/ext/RmlUi)

if(RMLUI_STATIC)
  set(RMLUI_STATIC_DIR ${BitRiot_SOURCE_DIR}/ext/RmlUi/lib)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Release")
  SET (CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS_INIT} -s)
else()
  if(RMLUI_STATIC)
	  # freetype wasn't linking in correctly when using RMLUI_STATIC due to use of Rml::Debugger. TODO fixme
    message(SEND_ERROR "RMLUI_STATIC should be false if running a Debug build due to unresolved linking issues")
  endif()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${BitRiot_SOURCE_DIR}/cmake")

find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)
find_package(RmlUi REQUIRED)
find_package(PkgConfig)

if(NOT SDL2_FOUND)
  message(SEND_ERROR "Failed to find SDL2.")
endif()

if(NOT SDL2_MIXER_FOUND)
  message(SEND_ERROR "Failed to find SDL2_mixer.")
endif()

if(NOT SDL2_TTF_FOUND)
  message(SEND_ERROR "Failed to find SDL2_ttf.")
endif()

if(NOT SDL2IMAGE_FOUND)
  message(SEND_ERROR "Failed to find SDL2_image.")
endif()

if(NOT RMLUI_FOUND)
  message(SEND_ERROR "Failed to find RmlUi.")
endif()


#file(GLOB SOURCES src/*.cpp)

## manual entries for now

# bitriot_sim: the deterministic simulation and what it reads (settings, data files,
# RNG, state hash). It links no SDL, SDL_image/ttf/mixer, RmlUi or OpenGL and
# includes none of their headers - tests/SimLibraryTest.cpp checks both - so a
# headless binary, a wasm-under-Node determinism gate and a hot-join fast-forward can
# build it on its own. Its sources are also what FixedPointSourceGuardTest scans for
# floating point and what the object-level check disassembles, so a file belongs
# here if the sim needs it, and in bitriot_core if it draws, plays, reads a device
# or shows a menu.
set(SIM_SOURCES ./src/AIGameView.cpp
./src/BlockEntity.cpp
./src/BombEntity.cpp
./src/DataReader.cpp
./src/DroneEntity.cpp
./src/DynamicMap.cpp
./src/EffectEntity.cpp
./src/EggEntity.cpp
./src/EggThiefEntity.cpp
./src/EnPlusEntity.cpp
./src/EnergyDropEntity.cpp
./src/EntityAnim.cpp
./src/EntityFactory.cpp
./src/EntityManager.cpp
./src/EntityMessage.cpp
./src/EntityMessageQueue.cpp
./src/ExplosionEntity.cpp
./src/GameEntity.cpp
./src/GameOverTimer.cpp
./src/GameSettings.cpp
./src/GameSound.cpp
./src/GameTimer.cpp
./src/IQPowerupEntity.cpp
./src/Map.cpp
./src/MatchConfig.cpp
./src/MickAssets.cpp
./src/MickBaseSound.cpp
./src/MickLogger.cpp
./src/MickUtil.cpp
./src/MineEntity.cpp
./src/PlayerCharacterEntity.cpp
./src/RobotEntity.cpp
./src/RocketFiredEntity.cpp
./src/RocketSpinEntity.cpp
./src/RumbleSink.cpp
./src/RuntimeException.cpp
./src/SimRandom.cpp
./src/SimpleBrain.cpp
./src/SimpleMovingEntity.cpp
./src/Simulation.cpp
./src/SpawningPool.cpp
./src/WallEaterEntity.cpp)

# bitriot_core: everything that draws, plays sound, reads devices, runs the menus
# and the game loop. It links bitriot_sim.
set(SOURCES ./src/EntityRenderer.cpp
./src/EntityRendererFactory.cpp
./src/RmlUI/events/Event.cpp
./src/RmlUI/events/EventHandler.cpp
./src/RmlUI/events/EventHandlerOptions.cpp
./src/RmlUI/events/EventHandlerPlayerSelect.cpp
./src/RmlUI/events/EventInstancer.cpp
./src/RmlUI/events/EventManager.cpp
./src/RmlUI/binders/ScoreBoardBinder.cpp
./src/RmlUI/binders/PlayersSelectBinder.cpp
./src/FrameLoop.cpp
./src/GameEngine.cpp
./src/InfoPanel.cpp
./src/InputHandler.cpp
./src/MapRenderer.cpp
./src/MickSDLRenderer.cpp
./src/MenuSDLRenderer.cpp
./src/MickBaseInput.cpp
./src/RmlUI/MickRmlUIElementUtil.cpp
./src/MickSDLAssets.cpp
./src/MickSDLUtil.cpp
./src/GameRunningSDLInput.cpp
./src/MickSDLSound.cpp
./src/RmlUI/glue/RenderInterfaceSDL2.cpp
./src/SDLHelper.cpp
./src/RmlUI/glue/ShellFileInterface.cpp
./src/TickPacer.cpp
./src/WorldRenderer.cpp
./src/RmlUI/glue/SystemInterfaceSDL2.cpp)

add_library(bitriot_sim STATIC ${SIM_SOURCES})

# The sim sees only the project's own headers.
target_include_directories(bitriot_sim PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)
target_compile_options(bitriot_sim PRIVATE
  -Wall
  -Wno-comment
  -Wfloat-conversion
  $<$<CONFIG:Debug>:-ggdb -O0>
  $<$<CONFIG:Release>:-O2>
)

# The tests read these at run time: which files make up the simulation (for the
# floating-point and SDL source guards) and where its archive is (for the
# object-level float check). Generated from the targets, so they cannot drift
# from what is actually built.
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/bitriot_sim_sources.txt
  CONTENT "$<JOIN:$<TARGET_PROPERTY:bitriot_sim,SOURCES>,\n>\n")

# Everything except main.cpp and the simulation lives in a static library so the
# test harness can link the real game code (see tests/). BitRiot is then just
# main.cpp + this + bitriot_sim.
add_library(bitriot_core STATIC ${SOURCES})
target_link_libraries(bitriot_core PUBLIC bitriot_sim)

# PUBLIC: the test target links bitriot_core and needs the same headers.
target_include_directories(bitriot_core PUBLIC
  ${SDL2_INCLUDE_DIR}
  ${SDL2_MIXER_INCLUDE_DIR}
  ${SDL2_TTF_INCLUDE_DIR}
  ${SDL2IMAGE_INCLUDE_DIR}
  ${RMLUI_HEADERS}
  ${PROJECT_SOURCE_DIR}
  ${OPENGL_INCLUDE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)

# INTERFACE half propagates to the test target: it includes the same headers,
# so it needs the same cstdint workaround. The warning/optimisation flags stay
# PRIVATE to the library.
target_compile_options(bitriot_core PRIVATE
  -Wall
  -Wno-comment
  -Wfloat-conversion
  $<$<CONFIG:Debug>:-ggdb -O0>
  $<$<CONFIG:Release>:-O2>
)
target_compile_options(bitriot_core INTERFACE
  # RmlUi 4.x headers (Types.h, robin_hood.h) use uintptr_t / UINT64_C without
  # including <cstdint>; GCC 13+ no longer pulls it in transitively. Force it.
  -include cstdint
)
# The library's own TUs need it too.
target_compile_options(bitriot_core PRIVATE -include cstdint)

add_executable(BitRiot ./src/main.cpp)
target_link_libraries(BitRiot PRIVATE bitriot_core)

# PUBLIC on the library so both BitRiot and the test executable inherit these.
if(MINGW)
  target_link_libraries(bitriot_core PUBLIC
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARY}
    ${SDL2_TTF_LIBRARY}
    ${SDL2IMAGE_LIBRARY}
    ${RMLUI_LIBRARIES}
    ${OPENGL_LIBRARIES}
    shlwapi
  )
  target_link_options(BitRiot PRIVATE -mwindows)
else()
  target_link_libraries(bitriot_core PUBLIC
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARY}
    ${SDL2_TTF_LIBRARY}
    ${SDL2IMAGE_LIBRARY}
    ${RMLUI_LIBRARIES}
    freetype
    ${OPENGL_LIBRARIES}
  )

  add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${PROJECT_SOURCE_DIR}/data ${PROJECT_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}/assets
  )
  if(RMLUI_STATIC)
    add_custom_command(
      TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${PROJECT_SOURCE_DIR}/ext/RmlUi/lib/libRmlCore.a ${PROJECT_BINARY_DIR}/libRmlCore.a
    )
  endif()
endif()

if(RMLUI_STATIC)
  target_compile_definitions(bitriot_core PUBLIC RMLUI_STATIC_LIB)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(BITRIOT_BUILD_TESTS "Build the headless test harness (requires GoogleTest)" ON)

if(BITRIOT_BUILD_TESTS)
  find_package(GTest)
  if(GTEST_FOUND)
    enable_testing()
    add_subdirectory(tests)
  else()
    message(STATUS "GoogleTest not found - skipping tests. Install gtest or configure with -DBITRIOT_BUILD_TESTS=OFF to silence this.")
  endif()
endif()
