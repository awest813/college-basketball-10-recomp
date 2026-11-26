#!/bin/bash
# Builds and installs the patched ReXGlue SDK on Linux, by default from ../../rexglue-sdk-src
# (tools/setup_sdk.sh clones and patches it) into its out/install/linux-amd64. Set
# REXGLUE_SDK_SRC to build a copy elsewhere (on WSL a copy on the Linux file system builds much
# faster than one under /mnt/c).
# The game's Release build needs the Release SDK (the default); a Debug or RelWithDebInfo game
# build needs the SDK built in that configuration too. They install side by side.
# Needs clang-20 and lld-20 (system packages, or tools/fetch_toolchain.sh without root) and
# tools/build_vulkan_deps.sh run first unless VULKAN_SDK points at a LunarG Vulkan SDK.
# Usage: build_sdk.sh [jobs] [release|debug|relwithdebinfo|all]
set -euo pipefail
jobs="${1:-0}"
[ "$jobs" = 0 ] && jobs="$(nproc)"
case "${2:-release}" in
  release) configs=(Release) ;;
  debug) configs=(Debug) ;;
  relwithdebinfo) configs=(RelWithDebInfo) ;;
  all) configs=(Release Debug RelWithDebInfo) ;;
  *) echo "unknown configuration '$2': use release, debug, relwithdebinfo or all" >&2; exit 1 ;;
esac
here="$(cd "$(dirname "$0")" && pwd)"
sdk="${REXGLUE_SDK_SRC:-$here/../../rexglue-sdk-src}"
[ -f "$sdk/CMakeLists.txt" ] || { echo "no SDK source at $sdk (run tools/setup_sdk.sh, or set REXGLUE_SDK_SRC)" >&2; exit 1; }
toolchain="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
if [ -f "$toolchain/env.sh" ]; then . "$toolchain/env.sh"; fi
"$here/check_tools.sh"
# Checked first: a failing $(...) inside export would not stop the script.
vulkan="${VULKAN_SDK:-$here/../../vulkan-deps/prefix-linux}"
[ -d "$vulkan" ] || { echo "no Vulkan SDK at $vulkan (run tools/build_vulkan_deps.sh or set VULKAN_SDK)" >&2; exit 1; }
export VULKAN_SDK="$(cd "$vulkan" && pwd)"
cc=clang; cxx=clang++
if command -v clang-20 >/dev/null; then cc=clang-20; cxx=clang++-20; fi
cd "$sdk"
# Every configuration is generated (that costs nothing), and only the ones asked for are built.
cmake --preset linux-amd64 -DREXGLUE_BUILD_TESTS=OFF \
  "-DCMAKE_CONFIGURATION_TYPES=Debug;Release;RelWithDebInfo" -DCMAKE_DEFAULT_BUILD_TYPE=Release -DCMAKE_DEFAULT_CONFIGS=Release \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_LINKER_TYPE=LLD \
  -DCMAKE_PREFIX_PATH="$VULKAN_SDK"
for config in "${configs[@]}"; do
  echo "Building the SDK, $config"
  cmake --build out/build/linux-amd64 --config "$config" --target install --parallel "$jobs"
done
echo "SDK installed to $sdk/out/install/linux-amd64 (${configs[*]})"
