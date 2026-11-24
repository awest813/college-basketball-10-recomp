#!/bin/bash
# Builds the Linux executable. Usage: build_linux.sh [debug|release|relwithdebinfo] [jobs] [preset]
# preset is linux-amd64-patched (default; tools/build_sdk.sh, release only) or linux-amd64 (an SDK
# install named by REXGLUE_SDK). Needs clang-20 and lld (system packages, or
# tools/fetch_toolchain.sh without root), CMake and Ninja.
set -euo pipefail
config="${1:-release}"
jobs="${2:-$(nproc)}"
preset="${3:-linux-amd64-patched}"
toolchain="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
if [ -f "$toolchain/env.sh" ]; then . "$toolchain/env.sh"; fi
cc=clang; cxx=clang++
if command -v clang-20 >/dev/null; then cc=clang-20; cxx=clang++-20; fi
cd "$(dirname "$0")"
cmake --preset "$preset" -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_LINKER_TYPE=LLD
cmake --build --preset "$preset-$config" -j "$jobs"
