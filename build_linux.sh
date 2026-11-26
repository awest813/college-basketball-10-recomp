#!/bin/bash
# Builds the Linux executable. Usage: build_linux.sh [release|debug|relwithdebinfo] [jobs] [preset]
# preset is linux-amd64-patched (default; the SDK from tools/setup_sdk.sh) or linux-amd64 (an SDK
# install named by REXGLUE_SDK). The patched preset looks for the SDK in ../rexglue-sdk-src, or in
# REXGLUE_SDK_SRC when that is set (as for tools/build_sdk.sh). Needs clang-20 and lld (system
# packages, or tools/fetch_toolchain.sh without root), CMake and Ninja; tools/check_tools.sh says
# what is missing.
set -euo pipefail
config="${1:-release}"
jobs="${2:-0}"
[ "$jobs" = 0 ] && jobs="$(nproc)"
preset="${3:-linux-amd64-patched}"
case "$config" in
  release) cfg=release ;; debug) cfg=debug ;; relwithdebinfo) cfg=relwithdebinfo ;;
  *) echo "unknown configuration '$config': use release, debug or relwithdebinfo" >&2; exit 1 ;;
esac
toolchain="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
if [ -f "$toolchain/env.sh" ]; then . "$toolchain/env.sh"; fi
cd "$(dirname "$0")"

# What the build needs that is not a tool: the game's executable (codegen reads it) and the SDK
# built in this configuration.
if [ ! -f assets/default.xex ]; then
  echo "assets/default.xex is missing. The build translates the game's code from it: extract the" >&2
  echo "disc into assets/ first (see \"The game copy\" in README.md)." >&2
  exit 1
fi
extra=()
case "$preset" in
  linux-amd64-patched)
    sdkdir="$(cd .. && pwd)/rexglue-sdk-src/out/install/linux-amd64"
    if [ -n "${REXGLUE_SDK_SRC:-}" ]; then
      sdkdir="$(cd "$REXGLUE_SDK_SRC" && pwd)/out/install/linux-amd64"
      extra+=(-DCMAKE_PREFIX_PATH="$sdkdir")
    fi
    hint="run tools/setup_sdk.sh first (about 15 minutes)" ;;
  linux-amd64)
    sdkdir="${REXGLUE_SDK:?set REXGLUE_SDK to the unpacked stock SDK (its linux-amd64 folder)}"
    hint="unpack rexglue-sdk-0.11.0-linux-amd64.zip from https://github.com/rexglue/rexglue-sdk/releases/tag/v0.11.0 and set REXGLUE_SDK to its linux-amd64 folder" ;;
  *) echo "unknown preset '$preset': use linux-amd64-patched or linux-amd64" >&2; exit 1 ;;
esac
if [ ! -f "$sdkdir/lib/cmake/rexglue/rexglueConfig.cmake" ]; then
  echo "no SDK in $sdkdir: $hint" >&2
  exit 1
fi
if [ ! -f "$sdkdir/lib/cmake/rexglue/rexglueTargets-$cfg.cmake" ]; then
  echo "the SDK in $sdkdir has no $config build: run tools/build_sdk.sh 0 $config first" >&2
  exit 1
fi

tools/check_tools.sh
cc=clang; cxx=clang++
if command -v clang-20 >/dev/null; then cc=clang-20; cxx=clang++-20; fi
cmake --preset "$preset" -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_LINKER_TYPE=LLD "${extra[@]}"
cmake --build --preset "$preset-$config" -j "$jobs"
