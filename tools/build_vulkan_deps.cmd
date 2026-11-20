@echo off
rem Builds a minimal local Vulkan SDK stand-in for building the ReXGlue SDK: Vulkan-Headers and
rem Vulkan-Loader v1.4.363 (the version the ReXGlue CI pins) and the VMA v3.3.0 header, from
rem ..\..\vulkan-deps (git clones) into ..\..\vulkan-deps\prefix. Point VULKAN_SDK at that prefix.
rem The loader is only needed so find_package(Vulkan) succeeds: ReXGlue loads the system
rem vulkan-1.dll at runtime and does not link or ship this one on Windows.
setlocal
set DEPS=%~dp0..\..\vulkan-deps
set PREFIX=%DEPS%\prefix
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
call "%VCVARS%" >nul || exit /b 1
cmake -S "%DEPS%\Vulkan-Headers" -B "%DEPS%\build\headers" -G Ninja -DCMAKE_INSTALL_PREFIX="%PREFIX%" || exit /b 1
cmake --build "%DEPS%\build\headers" --target install || exit /b 1
cmake -S "%DEPS%\Vulkan-Loader" -B "%DEPS%\build\loader" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
  -DCMAKE_PREFIX_PATH="%PREFIX%" -DBUILD_TESTS=OFF || exit /b 1
cmake --build "%DEPS%\build\loader" --target install || exit /b 1
if not exist "%PREFIX%\include\vma" mkdir "%PREFIX%\include\vma"
copy /y "%DEPS%\VulkanMemoryAllocator\include\vk_mem_alloc.h" "%PREFIX%\include\vma\" >nul || exit /b 1
echo Vulkan deps installed to %PREFIX%
