@echo off
rem Builds and installs the patched ReXGlue SDK (Release only) from ..\..\rexglue-sdk-src,
rem branch ncaabb10-fixes, into ..\..\rexglue-sdk-src\out\install\win-amd64.
rem Usage: build_sdk.cmd [jobs]
setlocal
set JOBS=%~1
if "%JOBS%"=="" set JOBS=12
set SDK=%~dp0..\..\rexglue-sdk-src
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul || exit /b 1
rem Vulkan headers, loader import library and VMA come from tools\build_vulkan_deps.cmd unless a
rem real Vulkan SDK is set in VULKAN_SDK.
if "%VULKAN_SDK%"=="" set VULKAN_SDK=%~dp0..\..\vulkan-deps\prefix
cd /d "%SDK%" || exit /b 1
rem The clang on PATH may target MinGW; the SDK and the game use the MSVC ABI.
cmake --preset win-amd64 -DREXGLUE_BUILD_TESTS=OFF ^
  -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_DEFAULT_BUILD_TYPE=Release -DCMAKE_DEFAULT_CONFIGS=Release ^
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_C_COMPILER_TARGET=x86_64-pc-windows-msvc -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc ^
  -DCMAKE_LINKER_TYPE=MSVC || exit /b 1
cmake --build out\build\win-amd64 --config Release --target install --parallel %JOBS%
