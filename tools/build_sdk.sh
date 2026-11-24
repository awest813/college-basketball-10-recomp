#!/bin/bash
# Builds and installs the patched ReXGlue SDK (Release only) on Linux, by default from
# ../../rexglue-sdk-src (branch ncaabb10-fixes) into its out/install/linux-amd64. Set
# REXGLUE_SDK_SRC to build a copy elsewhere (on WSL a copy on the Linux file system builds much
# faster than one under /mnt/c).
# Needs clang-20 and lld-20 (system packages, or tools/fetch_toolchain.sh without root) and
# tools/build_vulkan_deps.sh run first unless VULKAN_SDK points at a LunarG Vulkan SDK.
# Usage: build_sdk.sh [jobs]
set -euo pipefail
jobs="${1:-$(nproc)}"
here="$(cd "$(dirname "$0")" && pwd)"
sdk="${REXGLUE_SDK_SRC:-$(cd "$here/../../rexglue-sdk-src" && pwd)}"
toolchain="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
if [ -f "$toolchain/env.sh" ]; then . "$toolchain/env.sh"; fi
export VULKAN_SDK="${VULKAN_SDK:-$(cd "$here/../../vulkan-deps/prefix-linux" && pwd)}"
cc=clang; cxx=clang++
if command -v clang-20 >/dev/null; then cc=clang-20; cxx=clang++-20; fi
cd "$sdk"
cmake --preset linux-amd64 -DREXGLUE_BUILD_TESTS=OFF \
  -DCMAKE_CONFIGURATION_TYPES=Release -DCMAKE_DEFAULT_BUILD_TYPE=Release -DCMAKE_DEFAULT_CONFIGS=Release \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_LINKER_TYPE=LLD \
  -DCMAKE_PREFIX_PATH="$VULKAN_SDK"
cmake --build out/build/linux-amd64 --config Release --target install --parallel "$jobs"
