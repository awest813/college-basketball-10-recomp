@echo off
rem Builds the Windows executable. Usage: build_win.cmd [debug|release|relwithdebinfo] [jobs]
rem Needs Visual Studio 2022 (Build Tools is enough: MSVC + Windows SDK), CMake, Ninja and
rem a clang++ on PATH. The clang may target MinGW by default; the preset makes it target MSVC,
rem because the ReXGlue SDK binaries use the MSVC ABI.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=debug
set JOBS=%~2
if "%JOBS%"=="" set JOBS=14
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
cmake --preset win-amd64 || exit /b 1
cmake --build --preset win-amd64-%CONFIG% -j %JOBS%
