@echo off
rem Runs the game. Usage: run_win.cmd [patched|stock] [release|debug] [--scope.setting=value ...]
rem Default: the patched-SDK Release build (build_win.cmd release 12 win-amd64-patched). The stock
rem SDK build still has the missing-text and dirty-disc bugs fixed in patches\ and is only for Debug.
setlocal
set ROOT=%~dp0
set BUILD=win-amd64-patched
set DIR=Release
:args
if /i "%~1"=="patched" (set BUILD=win-amd64-patched& shift& goto args)
if /i "%~1"=="stock" (set BUILD=win-amd64& shift& goto args)
if /i "%~1"=="release" (set DIR=Release& shift& goto args)
if /i "%~1"=="debug" (set DIR=Debug& shift& goto args)
if /i "%~1"=="relwithdebinfo" (set DIR=RelWithDebInfo& shift& goto args)
set EXE=%ROOT%out\build\%BUILD%\%DIR%\ncaabb10.exe
if not exist "%EXE%" (
  echo %EXE% is not built yet.
  exit /b 1
)
"%EXE%" --title.game_root="%ROOT%assets" --app.cache_root="%ROOT%out\cache" --ui.fullscreen=false %1 %2 %3 %4 %5 %6 %7 %8 %9
