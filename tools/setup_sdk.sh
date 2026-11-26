#!/bin/bash
# Gets and builds everything the game's build needs, beside this project's folder:
#   ../rexglue-sdk-src  the ReXGlue SDK v0.11.0 with its submodules, and patches/*.patch applied
#   ../vulkan-deps      Vulkan-Headers and Vulkan-Loader v1.4.363 and VMA v3.3.0, built into
#                       ../vulkan-deps/prefix-linux (skipped when VULKAN_SDK names a LunarG SDK)
# then builds and installs the SDK (tools/build_sdk.sh). REXGLUE_SDK_SRC puts the SDK elsewhere,
# as for tools/build_sdk.sh. A step already done is skipped, so after a failure fix the cause and
# run it again.
# Usage: setup_sdk.sh [jobs] [release|debug|relwithdebinfo|all]   (as tools/build_sdk.sh)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
sdk="${REXGLUE_SDK_SRC:-$top/rexglue-sdk-src}"
deps="$top/vulkan-deps"
command -v git >/dev/null || { echo "git was not found: sudo apt install git" >&2; exit 1; }
toolchain="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
if [ -f "$toolchain/env.sh" ]; then . "$toolchain/env.sh"; fi
# The tools first, so a missing one stops this before the downloads.
"$here/check_tools.sh"

if [ ! -f "$sdk/CMakeLists.txt" ]; then
  echo "Cloning the ReXGlue SDK v0.11.0 into $sdk"
  git clone --branch v0.11.0 --recurse-submodules https://github.com/rexglue/rexglue-sdk.git "$sdk"
fi
git -C "$sdk" submodule update --init --recursive
for patch in "$here"/../patches/rexglue-sdk-0.11.0-*.patch; do
  patch="$(cd "$(dirname "$patch")" && pwd)/$(basename "$patch")"
  if git -C "$sdk" apply --check --reverse "$patch" 2>/dev/null; then
    echo "Applied already: $(basename "$patch")"
  elif git -C "$sdk" apply "$patch"; then
    echo "Applied: $(basename "$patch")"
  else
    echo "$(basename "$patch") does not apply. $sdk should be a clean v0.11.0 checkout." >&2
    exit 1
  fi
done

clone() {  # clone <folder> <url> <tag>: shallow clone of one tag, unless it is there
  if [ -e "$deps/$1/CMakeLists.txt" ] || [ -d "$deps/$1/include" ]; then return 0; fi
  echo "Cloning $1 $3 into $deps/$1"
  git clone --depth 1 --branch "$3" "$2" "$deps/$1"
}
prefix="$deps/prefix-linux"
if [ -z "${VULKAN_SDK:-}" ] && ! { [ -f "$prefix/include/vulkan/vulkan.h" ] \
    && [ -f "$prefix/include/vma/vk_mem_alloc.h" ] && ls "$prefix"/lib*/libvulkan.so >/dev/null 2>&1; }; then
  mkdir -p "$deps"
  clone Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git v1.4.363
  clone Vulkan-Loader https://github.com/KhronosGroup/Vulkan-Loader.git v1.4.363
  clone VulkanMemoryAllocator https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0
  "$here/build_vulkan_deps.sh"
fi

exec "$here/build_sdk.sh" "$@"
