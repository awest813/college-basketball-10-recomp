#!/bin/bash
# Builds a minimal local Vulkan SDK stand-in for building the ReXGlue SDK on Linux: Vulkan-Headers
# and Vulkan-Loader v1.4.363 (the version the ReXGlue CI pins) and the VMA v3.3.0 header, from
# ../../vulkan-deps (git clones) into ../../vulkan-deps/prefix-linux. Point VULKAN_SDK at it.
# Ubuntu 24.04's libvulkan-dev (1.3.275) is older than the 1.4.313 the SDK needs on x86-64. The
# loader built here only satisfies find_package(Vulkan); the game loads the system libvulkan.so.1.
set -euo pipefail
deps="$(cd "$(dirname "$0")/../../vulkan-deps" && pwd)"
build="${VULKAN_DEPS_BUILD:-$HOME/.cache/ncaabb10/vulkan-deps-build}"
prefix="$deps/prefix-linux"
cmake -S "$deps/Vulkan-Headers" -B "$build/headers" -G Ninja -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$build/headers" --target install
cmake -S "$deps/Vulkan-Loader" -B "$build/loader" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$prefix" -DBUILD_TESTS=OFF \
  -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF -DBUILD_WSI_WAYLAND_SUPPORT=OFF
cmake --build "$build/loader" --target install
mkdir -p "$prefix/include/vma"
cp "$deps/VulkanMemoryAllocator/include/vk_mem_alloc.h" "$prefix/include/vma/"
echo "Vulkan deps installed to $prefix"
