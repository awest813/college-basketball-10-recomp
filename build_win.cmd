@echo off
rem Builds the Windows executable. Usage: build_win.cmd [debug|release|relwithdebinfo] [jobs] [preset]
rem preset is win-amd64-patched (default; tools/build_sdk.cmd, release only) or win-amd64 (stock SDK).
rem Needs Visual Studio 2022 or later (Build Tools is enough: MSVC + Windows SDK), CMake, Ninja and
rem a clang++ on PATH. The clang may target MinGW by default; the preset makes it target MSVC,
rem because the ReXGlue SDK binaries use the MSVC ABI.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=release
set JOBS=%~2
if "%JOBS%"=="" set JOBS=14
set PRESET=%~3
if "%PRESET%"=="" set PRESET=win-amd64-patched
call "%~dp0tools\vcvars.cmd" || exit /b 1
cd /d "%~dp0"
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET%-%CONFIG% -j %JOBS%
