@echo off
rem Gets and builds everything the game's build needs, beside this project's folder:
rem   ..\rexglue-sdk-src  the ReXGlue SDK v0.11.0 with its submodules, and patches\*.patch applied
rem   ..\vulkan-deps      Vulkan-Headers and Vulkan-Loader v1.4.363 and VMA v3.3.0, built into
rem                       ..\vulkan-deps\prefix (skipped when VULKAN_SDK names a LunarG SDK)
rem then builds and installs the SDK (tools\build_sdk.cmd). A step already done is skipped, so
rem after a failure fix the cause and run it again.
rem Usage: setup_sdk.cmd [jobs] [release|debug|relwithdebinfo|all]   (as tools\build_sdk.cmd)
setlocal
set "HERE=%~dp0"
for %%d in ("%~dp0..\..") do set "TOP=%%~fd"
set "SDK=%TOP%\rexglue-sdk-src"
set "DEPS=%TOP%\vulkan-deps"
where git >nul 2>nul || (
  echo git was not found. Install it: winget install Git.Git
  exit /b 1
)
rem The tools first, so a missing one stops this before the downloads.
call "%HERE%vcvars.cmd" || exit /b 1
call "%HERE%check_tools.cmd" || exit /b 1

if exist "%SDK%\CMakeLists.txt" goto have_sdk
echo Cloning the ReXGlue SDK v0.11.0 into %SDK%
git clone --branch v0.11.0 --recurse-submodules https://github.com/rexglue/rexglue-sdk.git "%SDK%" || exit /b 1
:have_sdk
git -C "%SDK%" submodule update --init --recursive || exit /b 1
for %%p in ("%HERE%..\patches\rexglue-sdk-0.11.0-*.patch") do (
  call :patch "%%~fp" || exit /b 1
)

if defined VULKAN_SDK goto build_sdk
if exist "%DEPS%\prefix\include\vulkan\vulkan.h" if exist "%DEPS%\prefix\include\vma\vk_mem_alloc.h" if exist "%DEPS%\prefix\lib\vulkan-1.lib" goto build_sdk
call :clone Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git v1.4.363 || exit /b 1
call :clone Vulkan-Loader https://github.com/KhronosGroup/Vulkan-Loader.git v1.4.363 || exit /b 1
call :clone VulkanMemoryAllocator https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0 || exit /b 1
call "%HERE%build_vulkan_deps.cmd" || exit /b 1

:build_sdk
call "%HERE%build_sdk.cmd" %*
exit /b %ERRORLEVEL%

rem :patch <file>  applies one patch to the SDK, or says it is applied already.
:patch
git -C "%SDK%" apply --check --reverse "%~1" >nul 2>nul && (
  echo Applied already: %~nx1
  exit /b 0
)
git -C "%SDK%" apply "%~1" || (
  echo %~nx1 does not apply. %SDK% should be a clean v0.11.0 checkout.
  exit /b 1
)
echo Applied: %~nx1
exit /b 0

rem :clone <folder> <url> <tag>  shallow clone of one tag into ..\vulkan-deps, unless it is there.
:clone
if exist "%DEPS%\%~1\CMakeLists.txt" exit /b 0
if exist "%DEPS%\%~1\include" exit /b 0
echo Cloning %~1 %~3 into %DEPS%\%~1
git clone --depth 1 --branch %~3 %~2 "%DEPS%\%~1"
exit /b %ERRORLEVEL%
