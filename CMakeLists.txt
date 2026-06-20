cmake_minimum_required(VERSION 3.15)
project(BitRiot)

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
set(SOURCES ./src/AIGameView.cpp
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
./src/EntityFactory.cpp
./src/EntityManager.cpp
./src/EntityMessage.cpp
./src/EntityMessageQueue.cpp
./src/EntityRenderer.cpp
./src/EntityRendererFactory.cpp
./src/RmlUI/events/Event.cpp
./src/RmlUI/events/EventHandler.cpp
./src/RmlUI/events/EventHandlerOptions.cpp
./src/RmlUI/events/EventHandlerPlayerSelect.cpp
./src/RmlUI/events/EventInstancer.cpp
./src/RmlUI/events/EventManager.cpp
./src/RmlUI/binders/ScoreBoardBinder.cpp
./src/RmlUI/binders/PlayersSelectBinder.cpp
./src/ExplosionEntity.cpp
./src/GameEngine.cpp
./src/GameEntity.cpp
./src/GameSettings.cpp
./src/GameSound.cpp
./src/GameScoreBoardTimer.cpp
./src/GameTimer.cpp
./src/GameOverTimer.cpp
./src/IQPowerupEntity.cpp
./src/InfoPanel.cpp
./src/InputHandler.cpp
./src/Map.cpp
./src/MickSDLRenderer.cpp
./src/MenuSDLRenderer.cpp
./src/MickAssets.cpp
./src/MickBaseInput.cpp
./src/MickBaseSound.cpp
./src/MickLogger.cpp
./src/RmlUI/MickRmlUIElementUtil.cpp
./src/MickSDLAssets.cpp
./src/MickSDLUtil.cpp
./src/GameRunningSDLInput.cpp
./src/MickSDLSound.cpp
./src/MickUtil.cpp
./src/MineEntity.cpp
./src/PlayerCharacterEntity.cpp
./src/RmlUI/glue/RenderInterfaceSDL2.cpp
./src/RobotEntity.cpp
./src/RocketFiredEntity.cpp
./src/RocketSpinEntity.cpp
./src/RuntimeException.cpp
./src/SDLHelper.cpp
./src/RmlUI/glue/ShellFileInterface.cpp
./src/SimpleBrain.cpp
./src/SimpleMovingEntity.cpp
./src/SpawningPool.cpp
./src/RmlUI/glue/SystemInterfaceSDL2.cpp
./src/WallEaterEntity.cpp
./src/main.cpp)


add_executable(BitRiot ${SOURCES})

target_include_directories(BitRiot PRIVATE
  ${SDL2_INCLUDE_DIR}
  ${SDL2_MIXER_INCLUDE_DIR}
  ${SDL2_TTF_INCLUDE_DIR}
  ${SDL2IMAGE_INCLUDE_DIR}
  ${RMLUI_HEADERS}
  ${PROJECT_SOURCE_DIR}
  ${OPENGL_INCLUDE_DIR}
  ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(BitRiot PRIVATE cxx_std_14)

target_compile_options(BitRiot PRIVATE
  -Wall
  -Wno-comment
  # RmlUi 4.x headers (Types.h, robin_hood.h) use uintptr_t / UINT64_C without
  # including <cstdint>; GCC 13+ no longer pulls it in transitively. Force it.
  -include cstdint
  $<$<CONFIG:Debug>:-ggdb -O0>
  $<$<CONFIG:Release>:-O2>
)

if(MINGW)
  target_link_libraries(BitRiot PRIVATE
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
  target_link_libraries(BitRiot PRIVATE
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
  target_compile_definitions(BitRiot PRIVATE RMLUI_STATIC_LIB)
endif()
