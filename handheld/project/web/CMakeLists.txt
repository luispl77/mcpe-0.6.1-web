cmake_minimum_required(VERSION 3.16)
project(mcpe061web CXX)

# Configure with emcmake so the Emscripten toolchain file is picked up:
#
#   source ~/emsdk/emsdk_env.sh
#   emcmake cmake -S . -B build && cmake --build build -j8
#
# Output lands in build/ as minecraftpe.{html,js,wasm,data}.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This target must be configured through emcmake.")
endif()

# Same 2013-era C++03 as the other targets. Extensions stay on because the
# Emscripten headers use GNU variadic-macro behaviour in EM_ASM/EM_JS.
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(SRC_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../data")
set(RAKNET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib_projects/raknet/jni/RaknetSources")
set(SHELL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

# SDL2, libpng and zlib all ship as Emscripten ports, so there is nothing to
# find_package() -- these flags fetch and build them on first use. They are
# needed at compile time (for the headers) as well as at link time.
set(EM_PORTS "-sUSE_SDL=2" "-sUSE_LIBPNG=1" "-sUSE_ZLIB=1")

#
# RakNet
#
# Browsers cannot open raw UDP sockets, so multiplayer cannot work here. RakNet
# is still compiled in rather than stubbed out, because it reaches into
# NinecraftApp and the whole network/ tree; unpicking that is a much larger and
# riskier change than letting portable code compile and simply never listen.
#
file(GLOB RAKNET_SOURCES "${RAKNET_DIR}/*.cpp")
add_library(raknet STATIC ${RAKNET_SOURCES})
target_include_directories(raknet PUBLIC "${RAKNET_DIR}")
target_compile_options(raknet PRIVATE -w ${EM_PORTS})

#
# Game
#
file(GLOB_RECURSE GAME_SOURCES "${SRC_DIR}/*.cpp")

# Entry points and platform layers belonging to the other targets.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_android.*\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_dedicated\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_rpi\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/AppPlatform_win32\\.cpp$")
# OpenSL ES is Android's audio backend; the web uses the OpenAL one, which
# Emscripten implements over WebAudio.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/SoundSystemSL\\.cpp$")
# The vendored RakNet copy under src/ is built separately above.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/src/raknet/")

add_executable(minecraftpe ${GAME_SOURCES})

target_include_directories(minecraftpe PRIVATE
    "${SRC_DIR}"
    "${RAKNET_DIR}"
)

target_compile_definitions(minecraftpe PRIVATE
    MC_WASM
    MC_SDL2
    # Where the preload package below mounts the asset tree inside the VFS.
    MC_DATA_DIR="/data"
)

# This code predates -Wall being survivable; silence the noise so real errors
# stay visible.
target_compile_options(minecraftpe PRIVATE -w ${EM_PORTS})

target_link_options(minecraftpe PRIVATE
    ${EM_PORTS}

    # WebGL has no fixed-function pipeline. This emulates GLES 1.1 on top of it.
    # GL_FFP_ONLY is valid because the game never binds a shader of its own, and
    # it lets the emulation skip its shader-interop bookkeeping.
    -sLEGACY_GL_EMULATION=1
    -sGL_FFP_ONLY=1

    # The 3.8M asset tree is baked into a .data file fetched alongside the wasm.
    "--preload-file" "${DATA_DIR}@/data"

    # Saves are mounted from IndexedDB at runtime; see main_sdl.h.
    -lidbfs.js
    -sFORCE_FILESYSTEM=1

    # Chunk buffers and the terrain atlas make this hungrier than the default
    # 16MB heap, and worldgen is recursive enough to overflow the 64KB stack.
    -sINITIAL_MEMORY=268435456
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=4194304

    # _malloc/_free are called from the EM_JS prompt bridge in the web dialog.
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,UTF8ToString,stringToUTF8,lengthBytesUTF8

    -sENVIRONMENT=web
    "--shell-file" "${SHELL_FILE}"
)

set_target_properties(minecraftpe PROPERTIES
    SUFFIX ".html"
    LINK_DEPENDS "${SHELL_FILE}"
)

# Menu music, if a track has been dropped in beside this file. It is copied next
# to the output rather than added to the --preload-file package above: nothing
# needs it until the menu is on screen, so streaming it keeps the wait before the
# game appears exactly as long as it was. Absent, the page plays nothing -- see
# the music block in shell.html.
set(MUSIC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/menu.m4a")
if(EXISTS "${MUSIC_FILE}")
    add_custom_command(TARGET minecraftpe POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${MUSIC_FILE}" "$<TARGET_FILE_DIR:minecraftpe>/menu.m4a")
else()
    message(STATUS "No menu.m4a beside shell.html -- building without menu music.")
endif()

target_link_libraries(minecraftpe PRIVATE raknet)
