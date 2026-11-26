#!/bin/bash
# Checks the build tools (after the toolchain's env.sh, which the build scripts source first) and
# says what to install when one is missing. Exits 1 if any is.
missing=0
cxx=clang++
if command -v clang++-20 >/dev/null; then cxx=clang++-20; fi
if ! command -v "$cxx" >/dev/null; then
  echo "clang++ was not found: sudo apt install clang-20 lld-20, or tools/fetch_toolchain.sh (no root)" >&2
  missing=1
else
  major="$("$cxx" -dM -E -x c++ /dev/null 2>/dev/null | awk '/__clang_major__/ {print $3}')"
  if [ -z "$major" ] || [ "$major" -lt 20 ]; then
    echo "$cxx is clang ${major:-of unknown version}; the SDK needs 20 or newer: sudo apt install clang-20 lld-20, or tools/fetch_toolchain.sh" >&2
    missing=1
  fi
fi
if ! command -v ld.lld-20 >/dev/null && ! command -v ld.lld >/dev/null; then
  echo "lld was not found: sudo apt install lld-20, or tools/fetch_toolchain.sh" >&2
  missing=1
fi
for tool in cmake ninja; do
  if ! command -v "$tool" >/dev/null; then
    pkg="$tool"; [ "$tool" = ninja ] && pkg=ninja-build
    echo "$tool was not found: sudo apt install $pkg" >&2
    missing=1
  fi
done
exit "$missing"
