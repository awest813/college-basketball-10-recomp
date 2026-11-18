# ncaabb10 - ReXGlue Recompiled Project
#
# The SDK package provides rexglue_project and the rexglue_setup_* functions.

cmake_minimum_required(VERSION 3.25)
project(ncaabb10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (REXSDK_DIR)
  add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else ()
  find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif ()
rexglue_project(ncaabb10 OUTPUT_DIR generated/default)

# Host sources. src/main.cpp stays on the executable.
set(NCAABB10_HOST_SOURCES src/ncaabb10_hooks.cpp src/kernel_fixes.cpp)

# OBJECT and never STATIC, an archive drops the REX_EXPORT symbols.
add_library(ncaabb10_common OBJECT ${NCAABB10_HOST_SOURCES})
rexglue_setup_host_library(ncaabb10_common)

if (WIN32)
  add_executable(ncaabb10 WIN32 src/main.cpp)
else ()
  add_executable(ncaabb10 src/main.cpp)
endif ()

rexglue_setup_target(ncaabb10)
target_link_libraries(ncaabb10 PRIVATE ncaabb10_common)
