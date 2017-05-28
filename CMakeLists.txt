project(BitRiot)
cmake_minimum_required(VERSION 3.4.3)

set(CMAKE_BUILD_TYPE Release)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=gnu++0x -Wno-comment")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb -O0")

set(CMAKE_CSS_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")


if(CMAKE_BUILD_TYPE MATCHES "Release")
	SET (CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS_INIT} -s)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${BitRiot_SOURCE_DIR}/cmake")

find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(LibRocket REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PkgConfig)

pkg_check_modules(GLEW glew)

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

if(NOT LIBROCKET_FOUND)
  message(SEND_ERROR "Failed to find libRocket.")
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
./src/rocket/events/Event.cpp
./src/rocket/events/EventHandler.cpp
./src/rocket/events/EventHandlerOptions.cpp
./src/rocket/events/EventInstancer.cpp
./src/rocket/events/EventManager.cpp
./src/ExplosionEntity.cpp
./src/GameEngine.cpp
./src/GameEntity.cpp
./src/GameSettings.cpp
./src/GameSound.cpp
./src/IQPowerupEntity.cpp
./src/InfoPanel.cpp
./src/InputException.cpp
./src/InputHandler.cpp
./src/Map.cpp
./src/MenuRenderer.cpp
./src/MickAssets.cpp
./src/MickBaseInput.cpp
./src/MickBaseSound.cpp
./src/rocket/MickRocketElementUtil.cpp
./src/MickSDLAssets.cpp
./src/MickSDLInput.cpp
./src/MickSDLSound.cpp
./src/MickUtil.cpp
./src/MineEntity.cpp
./src/PlayerCharacterEntity.cpp
./src/rocket/glue/RenderInterfaceSDL2.cpp
./src/RobotEntity.cpp
./src/RocketFiredEntity.cpp
./src/RocketSpinEntity.cpp
./src/RuntimeException.cpp
./src/SDLHelper.cpp
./src/rocket/glue/ShellFileInterface.cpp
./src/SimpleBrain.cpp
./src/SimpleMovingEntity.cpp
./src/SpawningPool.cpp
./src/rocket/glue/SystemInterfaceSDL2.cpp
./src/WallEaterEntity.cpp
./src/main.cpp)


include_directories(${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR} ${SDL2_TTF_INCLUDE_DIR} ${SDL2IMAGE_INCLUDE_DIR} ${LIBROCKET_HEADERS} ${PROJECT_SOURCE_DIR} ${OPENGL_INCLUDE_DIR} ${GLEW_INCLUDE_DIR}_
  ${PROJECT_SOURCE_DIR}/src )
add_executable(BitRiot ${SOURCES})

if(MINGW)
  target_link_libraries(BitRiot ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${SDL2_TTF_LIBRARY} ${SDL2IMAGE_LIBRARY} ${LIBROCKET_LIBRARIES} ${OPENGL_LIBRARIRES} ${GLEW_LIBRARIES} shlwapi -mwindows)
else()
  target_link_libraries(BitRiot ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY} ${SDL2_TTF_LIBRARY} ${SDL2IMAGE_LIBRARY} ${LIBROCKET_LIBRARIES} ${OPENGL_LIBRARIRES} ${GLEW_LIBRARIES})

    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ln -sf ${PROJECT_SOURCE_DIR}/data ${PROJECT_BINARY_DIR}/
        COMMAND ln -sf ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}/
    )
endif()
