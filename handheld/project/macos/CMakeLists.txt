cmake_minimum_required(VERSION 3.16)
project(mcpe061 CXX)

# The source is 2013-era C++03. It compiles clean under modern clang at that
# standard; do not "modernise" it into C++11+ without checking, since a few
# spots rely on pre-C++11 template/iterator behaviour.
set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../data")
set(RAKNET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib_projects/raknet/jni/RaknetSources")

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)

find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(OPENGL_LIBRARY OpenGL REQUIRED)
find_library(OPENAL_LIBRARY OpenAL REQUIRED)
find_library(FOUNDATION_LIBRARY Foundation REQUIRED)

#
# RakNet
#
file(GLOB RAKNET_SOURCES "${RAKNET_DIR}/*.cpp")
add_library(raknet STATIC ${RAKNET_SOURCES})
target_include_directories(raknet PUBLIC "${RAKNET_DIR}")
target_compile_options(raknet PRIVATE -w)

#
# Game
#
file(GLOB_RECURSE GAME_SOURCES "${SRC_DIR}/*.cpp")

# Entry points and platform layers belonging to the other targets.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_android.*\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_dedicated\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/main_rpi\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/AppPlatform_win32\\.cpp$")
# OpenSL ES is Android's audio backend; macOS uses the OpenAL one.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/SoundSystemSL\\.cpp$")
# The vendored RakNet copy under src/ is built separately above.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/src/raknet/")

# moveFolder() only exists as Objective-C++ (it was written for iOS, and works
# unchanged on macOS). Compile it as ObjC++ without enabling a whole new
# language for the project.
set(OBJCXX_SOURCES
    "${SRC_DIR}/world/level/storage/MoveFolder.mm"
    "${SRC_DIR}/AppPlatform_macos_dialog.mm")
set_source_files_properties(${OBJCXX_SOURCES} PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-x;objective-c++")

add_executable(minecraftpe ${GAME_SOURCES} ${OBJCXX_SOURCES})

target_include_directories(minecraftpe PRIVATE
    "${SRC_DIR}"
    "${RAKNET_DIR}"
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(minecraftpe PRIVATE
    MC_MACOS
    MC_SDL2
    GL_SILENCE_DEPRECATION
    MC_DATA_DIR="${DATA_DIR}"
)

# This code predates -Wall being survivable; silence the noise so real errors
# stay visible.
target_compile_options(minecraftpe PRIVATE -w)

target_link_libraries(minecraftpe PRIVATE
    raknet
    SDL2::SDL2
    PNG::PNG
    ZLIB::ZLIB
    ${COCOA_LIBRARY}
    ${OPENGL_LIBRARY}
    ${OPENAL_LIBRARY}
    ${FOUNDATION_LIBRARY}
)
