@echo off
rem Runs the game. Usage: run_win.cmd [patched|stock] [release|debug|relwithdebinfo] [--scope.setting=value ...]
rem Default: the patched-SDK Release build (build_win.cmd release 12 win-amd64-patched). The stock
rem SDK build still has the bugs fixed in patches\ (missing text, dirty-disc freeze, dcbz) and is
rem only for Debug. Everything after the build words goes to the game unchanged.
setlocal
set "ROOT=%~dp0"
set BUILD=win-amd64-patched
set DIR=Release
rem The whole command line, as typed: cmd splits %1..%9 at = and , as well, which would cut
rem --scope.setting=value apart. The build words are taken off its front one by one.
set "REST=%*"
:args
if /i "%~1"=="patched" (set BUILD=win-amd64-patched& goto next)
if /i "%~1"=="stock" (set BUILD=win-amd64& goto next)
if /i "%~1"=="release" (set DIR=Release& goto next)
if /i "%~1"=="debug" (set DIR=Debug& goto next)
if /i "%~1"=="relwithdebinfo" (set DIR=RelWithDebInfo& goto next)
goto run
:next
shift
set "REST_IN=%REST%"
set "REST="
for /f "tokens=1,*" %%a in ("%REST_IN%") do set "REST=%%b"
goto args
:run
set "EXE=%ROOT%out\build\%BUILD%\%DIR%\ncaabb10.exe"
if not exist "%EXE%" goto missing
"%EXE%" --title.game_root="%ROOT%assets" --app.cache_root="%ROOT%out\cache" --ui.fullscreen=false %REST%
exit /b %ERRORLEVEL%
:missing
echo "%EXE%" is not built yet.
exit /b 1
