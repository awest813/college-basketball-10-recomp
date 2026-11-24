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
set(NCAABB10_HOST_SOURCES src/ncaabb10_hooks.cpp src/kernel_fixes.cpp src/diagnostics.cpp src/perf.cpp src/sampler.cpp src/gpu_wait.cpp src/audio_capture.cpp src/frontend.cpp)

# OBJECT and never STATIC, an archive drops the REX_EXPORT symbols.
add_library(ncaabb10_common OBJECT ${NCAABB10_HOST_SOURCES})
rexglue_setup_host_library(ncaabb10_common)

if (WIN32)
  add_executable(ncaabb10 WIN32 src/main.cpp src/ncaabb10.manifest)
else ()
  add_executable(ncaabb10 src/main.cpp)
endif ()

rexglue_setup_target(ncaabb10)
target_link_libraries(ncaabb10 PRIVATE ncaabb10_common)
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
