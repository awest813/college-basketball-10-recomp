@echo off
rem Sets up the MSVC x64 build environment in the calling script (no setlocal here, on purpose).
rem Asks vswhere for the newest Visual Studio with the C++ tools (any edition, Build Tools
rem included), then falls back to the default 2022 Build Tools and Community folders.
rem Usage, from another script: call "%~dp0tools\vcvars.cmd" || exit /b 1
set "NCAABB10_VCVARS="
set "NCAABB10_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%NCAABB10_VSWHERE%" goto fallback
rem "call" first: cmd /c would otherwise strip the quotes around a path with parentheses.
for /f "usebackq delims=" %%i in (`call "%NCAABB10_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "NCAABB10_VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
:fallback
if defined NCAABB10_VCVARS if exist "%NCAABB10_VCVARS%" goto found
set "NCAABB10_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%NCAABB10_VCVARS%" goto found
set "NCAABB10_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%NCAABB10_VCVARS%" goto found
echo Visual Studio with the C++ x64 tools was not found. Install Visual Studio 2022 or Build Tools
echo with the "Desktop development with C++" workload.
exit /b 1
:found
call "%NCAABB10_VCVARS%" >nul || exit /b 1
exit /b 0
