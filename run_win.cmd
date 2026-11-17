@echo off
rem Runs the Windows build. Usage: run_win.cmd [debug|release|relwithdebinfo] [extra --scope.setting=value ...]
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=debug
if /i "%CONFIG%"=="debug" set DIR=Debug
if /i "%CONFIG%"=="release" set DIR=Release
if /i "%CONFIG%"=="relwithdebinfo" set DIR=RelWithDebInfo
shift
set ROOT=%~dp0
"%ROOT%out\build\win-amd64\%DIR%\ncaabb10.exe" --title.game_root="%ROOT%assets" --app.cache_root="%ROOT%out\cache" --ui.fullscreen=false %1 %2 %3 %4 %5 %6 %7 %8 %9
