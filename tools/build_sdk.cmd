@echo off
rem Builds and installs the patched ReXGlue SDK from ..\..\rexglue-sdk-src (tools\setup_sdk.cmd
rem clones and patches it) into ..\..\rexglue-sdk-src\out\install\win-amd64.
rem Usage: build_sdk.cmd [jobs] [release|debug|relwithdebinfo|all]
rem The game's Release build needs the Release SDK (the default); a Debug or RelWithDebInfo game
rem build needs the SDK built in that configuration too. All of them install side by side (the
rem Debug files end in d, the RelWithDebInfo ones in rd) and each stays until rebuilt.
setlocal
set JOBS=%~1
if "%JOBS%"=="" set JOBS=%NUMBER_OF_PROCESSORS%
if "%JOBS%"=="0" set JOBS=%NUMBER_OF_PROCESSORS%
set CONFIGS=
if "%~2"=="" set CONFIGS=Release
if /i "%~2"=="release" set CONFIGS=Release
if /i "%~2"=="debug" set CONFIGS=Debug
if /i "%~2"=="relwithdebinfo" set CONFIGS=RelWithDebInfo
if /i "%~2"=="all" set CONFIGS=Release Debug RelWithDebInfo
if "%CONFIGS%"=="" (
  echo Unknown configuration "%~2": use release, debug, relwithdebinfo or all.
  exit /b 1
)
set SDK=%~dp0..\..\rexglue-sdk-src
if not exist "%SDK%\CMakeLists.txt" (
  echo No SDK source in %SDK%. Run tools\setup_sdk.cmd first.
  exit /b 1
)
call "%~dp0vcvars.cmd" || exit /b 1
call "%~dp0check_tools.cmd" || exit /b 1
rem Vulkan headers, loader import library and VMA come from tools\build_vulkan_deps.cmd unless a
rem real Vulkan SDK is set in VULKAN_SDK.
if "%VULKAN_SDK%"=="" set VULKAN_SDK=%~dp0..\..\vulkan-deps\prefix
if not exist "%VULKAN_SDK%\include\vulkan\vulkan.h" (
  echo No Vulkan headers in %VULKAN_SDK%. Run tools\setup_sdk.cmd, or set VULKAN_SDK to a LunarG SDK.
  exit /b 1
)
cd /d "%SDK%" || exit /b 1
rem Every configuration is generated (that costs nothing), and only the ones asked for are built.
rem The clang on PATH may target MinGW; the SDK and the game use the MSVC ABI. Release keeps
rem CodeView debug info in its PDBs (left in out\build, not installed) so a crash in it can still
rem be read with the build at hand.
cmake --preset win-amd64 -DREXGLUE_BUILD_TESTS=OFF ^
  "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo" -DCMAKE_DEFAULT_BUILD_TYPE=Release -DCMAKE_DEFAULT_CONFIGS=Release ^
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc ^
  -DCMAKE_LINKER_TYPE=MSVC ^
  "-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG -g -gcodeview" "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g -gcodeview" ^
  "-DCMAKE_SHARED_LINKER_FLAGS_RELEASE=-Xlinker /DEBUG -Xlinker /OPT:REF -Xlinker /OPT:ICF" ^
  "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-Xlinker /DEBUG -Xlinker /OPT:REF -Xlinker /OPT:ICF" || exit /b 1
for %%c in (%CONFIGS%) do (
  echo Building the SDK, %%c
  cmake --build out\build\win-amd64 --config %%c --target install --parallel %JOBS% || exit /b 1
)
rem The SDK installs PDBs only for Debug and RelWithDebInfo. Install the Release ones too: the
rem game's build copies a PDB found beside an SDK DLL next to its own copy, where debuggers and
rem tools\symbolize.py look (tools\package.ps1 leaves them out of the zip).
if exist out\win-amd64\Release\rexkernel.pdb (
  for %%f in (out\win-amd64\Release\*.pdb) do if /i not "%%~nf"=="rexglue" copy /y "%%f" out\install\win-amd64\bin\ >nul
)
echo SDK installed to %SDK%\out\install\win-amd64 (%CONFIGS%)
