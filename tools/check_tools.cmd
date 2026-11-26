@echo off
rem Checks the build tools after tools\vcvars.cmd, and says what to install when one is missing.
rem No setlocal, on purpose: it can add LLVM's default folder to the calling script's PATH.
rem Usage, from another script: call "%~dp0tools\check_tools.cmd" || exit /b 1
set "NCAABB10_MISSING="
rem The LLVM installer does not put clang on PATH unless asked to; look in its default folder.
where clang++ >nul 2>nul || if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
where clang++ >nul 2>nul || (
  echo clang++ was not found. Install LLVM 20 or newer: winget install LLVM.LLVM
  echo ^(or https://github.com/llvm/llvm-project/releases^). Visual Studio's own clang is too old.
  set NCAABB10_MISSING=1
)
if defined NCAABB10_MISSING goto check_cmake
set "NCAABB10_CLANG="
for /f "tokens=3" %%v in ('clang++ -dM -E -x c++ NUL 2^>nul ^| findstr /c:"__clang_major__"') do set "NCAABB10_CLANG=%%v"
if not defined NCAABB10_CLANG (
  echo clang++ on PATH did not report its version.
  set NCAABB10_MISSING=1
  goto check_cmake
)
if %NCAABB10_CLANG% LSS 20 (
  echo clang++ on PATH is version %NCAABB10_CLANG%; the SDK needs 20 or newer: winget install LLVM.LLVM
  set NCAABB10_MISSING=1
)
:check_cmake
where cmake >nul 2>nul || (
  echo cmake was not found. Install CMake 3.25 or newer: winget install Kitware.CMake
  echo ^(or Visual Studio's "C++ CMake tools for Windows" component^).
  set NCAABB10_MISSING=1
)
where ninja >nul 2>nul || (
  echo ninja was not found. Install it: winget install Ninja-build.Ninja
  echo ^(or Visual Studio's "C++ CMake tools for Windows" component^).
  set NCAABB10_MISSING=1
)
set "NCAABB10_CLANG="
if defined NCAABB10_MISSING (
  set "NCAABB10_MISSING="
  exit /b 1
)
exit /b 0
