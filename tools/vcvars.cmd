@echo off
rem Sets up the MSVC x64 build environment in the calling script (no setlocal here, on purpose).
rem Asks vswhere for the newest Visual Studio with the C++ tools (any edition, Build Tools
rem included), then falls back to the default 2022 Build Tools and Community folders.
rem Usage, from another script: call "%~dp0tools\vcvars.cmd" || exit /b 1
set "NCAABB10_VCVARS="
set "NCAABB10_VS="
set "NCAABB10_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%NCAABB10_VSWHERE%" goto fallback
rem Through a file, not for /f: cmd /c mangles the quoted path, and the ")" of "(x86)" ends the
rem for's command.
set "NCAABB10_VSOUT=%TEMP%\ncaabb10_vswhere_%RANDOM%.txt"
"%NCAABB10_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%NCAABB10_VSOUT%" 2>nul
set /p NCAABB10_VS=<"%NCAABB10_VSOUT%"
del "%NCAABB10_VSOUT%" 2>nul
set "NCAABB10_VSOUT="
if defined NCAABB10_VS set "NCAABB10_VCVARS=%NCAABB10_VS%\VC\Auxiliary\Build\vcvars64.bat"
:fallback
if defined NCAABB10_VCVARS if exist "%NCAABB10_VCVARS%" goto found
set "NCAABB10_VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%NCAABB10_VCVARS%" goto found
set "NCAABB10_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%NCAABB10_VCVARS%" goto found
echo Visual Studio with the C++ x64 tools was not found. Install Visual Studio 2022 or Build Tools
echo with the "Desktop development with C++" workload.
exit /b 1
:found
rem Its stderr is dropped: the 2022 Build Tools' vcvars64.bat prints "'vswhere.exe' is not
rem recognized" there on every call and still succeeds.
call "%NCAABB10_VCVARS%" >nul 2>nul || (
  echo "%NCAABB10_VCVARS%" failed.
  exit /b 1
)
exit /b 0
