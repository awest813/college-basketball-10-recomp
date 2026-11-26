@echo off
rem Builds the Windows executable. Usage: build_win.cmd [release|debug|relwithdebinfo] [jobs] [preset]
rem preset is win-amd64-patched (default; the SDK from tools\setup_sdk.cmd) or win-amd64 (the stock
rem SDK 0.11.0 release unpacked beside this folder). jobs defaults to the number of processors.
rem Needs Visual Studio 2022 or later (Build Tools is enough: MSVC + Windows SDK), CMake, Ninja and
rem clang 20 or newer; tools\check_tools.cmd says what is missing. The clang may target MinGW by
rem default; the preset makes it target MSVC, because the ReXGlue SDK binaries use the MSVC ABI.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=release
set CFG=
if /i "%CONFIG%"=="release" set CFG=Release
if /i "%CONFIG%"=="debug" set CFG=Debug
if /i "%CONFIG%"=="relwithdebinfo" set CFG=RelWithDebInfo
if "%CFG%"=="" (
  echo Unknown configuration "%CONFIG%": use release, debug or relwithdebinfo.
  exit /b 1
)
set JOBS=%~2
if "%JOBS%"=="" set JOBS=%NUMBER_OF_PROCESSORS%
if "%JOBS%"=="0" set JOBS=%NUMBER_OF_PROCESSORS%
set PRESET=%~3
if "%PRESET%"=="" set PRESET=win-amd64-patched
cd /d "%~dp0"

rem What the build needs that is not a tool: the game's executable (codegen reads it) and the SDK
rem built in this configuration.
if not exist "assets\default.xex" (
  echo assets\default.xex is missing. The build translates the game's code from it: extract the
  echo disc into assets\ first ^(see "The game copy" in README.md^).
  exit /b 1
)
set "SDKDIR="
if /i "%PRESET%"=="win-amd64-patched" set "SDKDIR=%~dp0..\rexglue-sdk-src\out\install\win-amd64"
if /i "%PRESET%"=="win-amd64" set "SDKDIR=%~dp0..\rexglue-sdk-0.11.0-win-amd64\win-amd64"
if not defined SDKDIR (
  echo Unknown preset "%PRESET%": use win-amd64-patched or win-amd64.
  exit /b 1
)
if not exist "%SDKDIR%\lib\cmake\rexglue\rexglueConfig.cmake" (
  if /i "%PRESET%"=="win-amd64-patched" (
    echo The patched SDK is not built yet. Run tools\setup_sdk.cmd first ^(about 20 minutes^).
  ) else (
    echo The stock SDK is not in %SDKDIR%. Unpack rexglue-sdk-0.11.0-win-amd64.zip from
    echo https://github.com/rexglue/rexglue-sdk/releases/tag/v0.11.0 beside this folder.
  )
  exit /b 1
)
if not exist "%SDKDIR%\lib\cmake\rexglue\rexglueTargets-%CFG%.cmake" (
  echo The SDK in %SDKDIR% has no %CFG% build. Run tools\build_sdk.cmd 0 %CONFIG% first.
  exit /b 1
)

call "%~dp0tools\vcvars.cmd" || exit /b 1
call "%~dp0tools\check_tools.cmd" || exit /b 1
cmake --preset %PRESET% || exit /b 1
rem A clean checkout has no generated code, so that configure knew no recompiled sources: the SDK
rem picks them up from generated\default\sources.cmake, which codegen writes during the build, after
rem Ninja has read its build files. Run codegen alone first and configure again, or the first build
rem links without the game's code.
if not exist "generated\default\sources.cmake" (
  echo Generating the recompiled code first ^(a clean checkout, about 10 minutes^)
  cmake --build --preset %PRESET%-%CONFIG% --target ncaabb10_codegen || exit /b 1
  cmake --preset %PRESET% || exit /b 1
)
cmake --build --preset %PRESET%-%CONFIG% -j %JOBS%
