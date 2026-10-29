# EmscriptenGame.cmake - the web build of the game (brief 09).
#
#   emcmake cmake -S . -B build-wasm -DBITRIOT_BUILD_TESTS=OFF
#   cmake --build build-wasm -j4
#
# Included by the top-level CMakeLists.txt in place of the find_package calls when
# EMSCRIPTEN is set. It:
#   * turns on the Emscripten ports the game uses (SDL2, SDL2_image with PNG and BMP,
#     SDL2_ttf, SDL2_mixer with WAV only, FreeType) with compile and link flags,
#     so there is nothing to find and nothing to install;
#   * sets the exception model and the memory model for the whole build;
#   * builds RmlUi 4.3 from the ext/RmlUi/RmlUi submodule as a static library, with
#     the FreeType port (the submodule's own CMake is not used: it needs CMake < 3.5
#     compatibility that CMake 4 removed, and its FreeType lookup).
# and defines bitriot_configure_web_target(), which the top level calls on BitRiot.

# ---------------------------------------------------------------------------
# Compiler and linker flags for every target
# ---------------------------------------------------------------------------
# The ports are fetched (or taken from the cache scripts/setup-emsdk.sh --with-ports
# seeded) the first time a flag asks for one. The brackets are for emcc, not CMake;
# the quotes keep them out of the shell's hands. SDL_image gets PNG and BMP: the menu
# pages show the player faces, which are BMP files, through IMG_LoadTyped_RW (the
# game's own sprites are loaded with SDL_LoadBMP).
set(BITRIOT_WASM_PORT_FLAGS
  -sUSE_SDL=2
  -sUSE_SDL_IMAGE=2
  "-sSDL2_IMAGE_FORMATS=[\"png\",\"bmp\"]"
  -sUSE_SDL_TTF=2
  -sUSE_SDL_MIXER=2
  "-sSDL2_MIXER_FORMATS=[]"
  -sUSE_FREETYPE=1)

# -fwasm-exceptions (native wasm exception handling, no JS glue, small and fast):
# the game still throws (renderer and menu setup, the asset loaders, the sim's
# logic_error checks), and the same flag is what brief 05b uses for the sim.
# Compile and link must agree, and so must every library, RmlUi included, so it is
# global. No pthreads: no SharedArrayBuffer, so no COOP/COEP headers are needed.
add_compile_options(${BITRIOT_WASM_PORT_FLAGS} -fwasm-exceptions)
add_link_options(${BITRIOT_WASM_PORT_FLAGS} -fwasm-exceptions
  -sALLOW_MEMORY_GROWTH=1
  # Emscripten's default stack is 64 KB, which is small for RmlUi's layout and the
  # AI's search; 2 MB is well under the native 8 MB and costs nothing until used.
  -sSTACK_SIZE=2097152)

# ---------------------------------------------------------------------------
# What the native branch gets from find_package
# ---------------------------------------------------------------------------
# The ports' headers are on the include path through the flags above.
set(SDL2_INCLUDE_DIR "")
set(SDL2_MIXER_INCLUDE_DIR "")
set(SDL2_TTF_INCLUDE_DIR "")
set(SDL2IMAGE_INCLUDE_DIR "")
set(SDL2_LIBRARY "")
set(SDL2_MIXER_LIBRARY "")
set(SDL2_TTF_LIBRARY "")
set(SDL2IMAGE_LIBRARY "")

# ---------------------------------------------------------------------------
# RmlUi 4.3, static, from the submodule
# ---------------------------------------------------------------------------
set(BITRIOT_RMLUI_SOURCE_DIR ${PROJECT_SOURCE_DIR}/ext/RmlUi/RmlUi)
if(NOT EXISTS ${BITRIOT_RMLUI_SOURCE_DIR}/CMake/FileList.cmake)
  message(FATAL_ERROR "RmlUi is not checked out: run  git submodule update --init ext/RmlUi/RmlUi")
endif()

# FileList.cmake builds its lists from PROJECT_SOURCE_DIR; a function scope lets us
# point that at RmlUi without changing it for the rest of the build. Targets are
# global, so they outlive the function.
function(bitriot_add_rmlui)
  set(PROJECT_SOURCE_DIR ${BITRIOT_RMLUI_SOURCE_DIR})
  include(${BITRIOT_RMLUI_SOURCE_DIR}/CMake/FileList.cmake)

  foreach(library Core Debugger)
    add_library(Rml${library} STATIC ${${library}_SRC_FILES})
    target_include_directories(Rml${library} PUBLIC ${BITRIOT_RMLUI_SOURCE_DIR}/Include)
    # What RmlUi's own CMake does for a static build (see the top of Include/RmlUi/Core/Header.h).
    target_compile_definitions(Rml${library} PUBLIC RMLUI_STATIC_LIB)
    # Its own warnings are not ours to fix.
    target_compile_options(Rml${library} PRIVATE -w)
  endforeach()
  target_compile_definitions(RmlCore PRIVATE RMLUI_VERSION="4.3")
  target_link_libraries(RmlDebugger PUBLIC RmlCore)
endfunction()
bitriot_add_rmlui()

# FindRmlUi's results. The debugger is only used when !NDEBUG (MenuSDLRenderer.cpp);
# in a Release build the linker leaves the unreferenced archive members out.
set(RMLUI_HEADERS ${BITRIOT_RMLUI_SOURCE_DIR}/Include)
set(RMLUI_LIBRARIES RmlCore RmlDebugger)

# ---------------------------------------------------------------------------
# The page: shell, preloaded files, outputs
# ---------------------------------------------------------------------------
# Called for the BitRiot executable. Output is build-wasm/BitRiot.{html,js,wasm,data}
# and a copy of the page as index.html, so the directory can be served as it is.
function(bitriot_configure_web_target target)
  set(shell ${PROJECT_SOURCE_DIR}/web/shell.html)
  # The preloaded files are packed into BitRiot.data at link time, so editing one
  # must relink (the glob is read at configure time: re-run cmake after adding a file).
  file(GLOB_RECURSE preloaded CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/data/* ${PROJECT_SOURCE_DIR}/assets/*)
  target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src/web/WebHooks.cpp)
  set_target_properties(${target} PROPERTIES
    SUFFIX ".html"
    LINK_DEPENDS "${shell};${preloaded}")
  target_link_options(${target} PRIVATE
    "SHELL:--shell-file ${shell}"
    # The page starts the game from its "Click to start" button (Module.callMain).
    -sEXPORTED_RUNTIME_METHODS=callMain
    # The game reads data/ and assets/ relative to its working directory, which is
    # / in Emscripten's file system. Nothing is written, so no IDBFS yet (brief 12).
    # (SHELL: keeps each pair together and stops CMake dropping the repeated option.)
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/data@/data"
    "SHELL:--preload-file ${PROJECT_SOURCE_DIR}/assets@/assets")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.html
            $<TARGET_FILE_DIR:${target}>/index.html
    COMMENT "Copying the page to index.html")
endfunction()
