# ncaabb10 - ReXGlue Recompiled Project
#
# The SDK package provides rexglue_project and the rexglue_setup_* functions.

cmake_minimum_required(VERSION 3.25)
# The port's own version: the launcher shows it and the Windows exe carries it.
project(ncaabb10 VERSION 0.1.0 LANGUAGES CXX)
if (WIN32)
  enable_language(RC)
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (REXSDK_DIR)
  add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else ()
  find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif ()
rexglue_project(ncaabb10 OUTPUT_DIR generated/default)

# Host sources. src/main.cpp stays on the executable.
set(NCAABB10_HOST_SOURCES src/ncaabb10_hooks.cpp src/kernel_fixes.cpp src/diagnostics.cpp src/perf.cpp src/sampler.cpp src/gpu_wait.cpp src/audio_capture.cpp src/frontend.cpp)

# OBJECT and never STATIC, an archive drops the REX_EXPORT symbols.
add_library(ncaabb10_common OBJECT ${NCAABB10_HOST_SOURCES})
rexglue_setup_host_library(ncaabb10_common)
target_compile_definitions(ncaabb10_common PRIVATE NCAABB10_VERSION="${PROJECT_VERSION}")

# The commit the build came from, refreshed on every build (ncaabb10_git.h changes only with the
# commit), and the configuration: the log's first line and the launcher name them, so a log
# someone sends says which build wrote it.
set(NCAABB10_GIT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ncaabb10_git.h")
add_custom_target(ncaabb10_git_rev
                  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
                          -DOUT=${NCAABB10_GIT_HEADER} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/git_rev.cmake
                  BYPRODUCTS ${NCAABB10_GIT_HEADER}
                  COMMENT "" VERBATIM)
add_dependencies(ncaabb10_common ncaabb10_git_rev)
target_include_directories(ncaabb10_common PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(ncaabb10_common PRIVATE NCAABB10_CONFIG="$<CONFIG>")

# Debug info for the port's own code in Release too (the SDK sets none there). With the linker's
# map of every recompiled function (sub_<guest address>) it lets tools/symbolize.py name the
# frames of a crash report from a Release build. The recompiled code itself gets none, which
# keeps the build time.
if (WIN32)
  target_compile_options(ncaabb10_common PRIVATE $<$<CONFIG:Release>:-g -gcodeview>)
else ()
  target_compile_options(ncaabb10_common PRIVATE $<$<CONFIG:Release>:-g>)
endif ()

if (WIN32)
  # Icon and version information (Explorer, the taskbar; SDL takes the window icon from it).
  configure_file(src/ncaabb10.rc.in "${CMAKE_CURRENT_BINARY_DIR}/ncaabb10.rc" @ONLY)
  add_executable(ncaabb10 WIN32 src/main.cpp src/ncaabb10.manifest
                                "${CMAKE_CURRENT_BINARY_DIR}/ncaabb10.rc")
else ()
  add_executable(ncaabb10 src/main.cpp)
endif ()

rexglue_setup_target(ncaabb10)
target_link_libraries(ncaabb10 PRIVATE ncaabb10_common)
add_dependencies(ncaabb10 ncaabb10_git_rev)
# SIMDe's loops ask for vectorization, which the Debug build's -O1 does not do; clang said so once
# per use (2675 warnings in a Debug build). The higher levels of the other builds vectorize them.
target_compile_options(ncaabb10_recompiled PRIVATE $<$<CONFIG:Debug>:-Wno-pass-failed>)
target_include_directories(ncaabb10 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(ncaabb10 PRIVATE NCAABB10_VERSION="${PROJECT_VERSION}"
                                            NCAABB10_CONFIG="$<CONFIG>")
if (WIN32)
  set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS
                              "$<$<CONFIG:Release>:-g>;$<$<CONFIG:Release>:-gcodeview>")
  # ncaabb10.pdb beside the Release exe (not packaged; tools/package.ps1 -Symbols keeps it). /DEBUG
  # alone would also turn off /OPT:REF and /OPT:ICF and turn on incremental linking.
  target_link_options(ncaabb10 PRIVATE "$<$<CONFIG:Release>:LINKER:/DEBUG>"
                      "$<$<CONFIG:Release>:LINKER:/OPT:REF>" "$<$<CONFIG:Release>:LINKER:/OPT:ICF>"
                      "$<$<CONFIG:Release>:LINKER:/INCREMENTAL:NO>")
else ()
  set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-g>")
endif ()
# SDL3 comes with the SDK; the test input feed pushes window key events through it.
find_package(SDL3 CONFIG REQUIRED)
target_link_libraries(ncaabb10 PRIVATE SDL3::SDL3)
# getenv and fopen are fine here; the MSVC runtime headers flag them as deprecated.
if (WIN32)
  target_compile_definitions(ncaabb10_common PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(ncaabb10 PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Copy the Visual C++ runtime the exe and the SDK DLLs import next to them (app-local, as
  # Microsoft allows), so the build folder runs on a PC without the VC++ redistributable.
  # vcvars64 (build_win.cmd) sets VCToolsRedistDir.
  file(GLOB NCAABB10_VC_RUNTIME
       "$ENV{VCToolsRedistDir}x64/Microsoft.VC*.CRT/vcruntime140.dll"
       "$ENV{VCToolsRedistDir}x64/Microsoft.VC*.CRT/vcruntime140_1.dll"
       "$ENV{VCToolsRedistDir}x64/Microsoft.VC*.CRT/msvcp140.dll"
       "$ENV{VCToolsRedistDir}x64/Microsoft.VC*.CRT/msvcp140_atomic_wait.dll")
  if (NCAABB10_VC_RUNTIME)
    add_custom_command(TARGET ncaabb10 POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NCAABB10_VC_RUNTIME}
                               $<TARGET_FILE_DIR:ncaabb10>
                       VERBATIM)
  else ()
    message(WARNING "Visual C++ runtime DLLs not found (VCToolsRedistDir='$ENV{VCToolsRedistDir}'); "
                    "the build folder will need the VC++ redistributable installed to run")
  endif ()
endif ()
