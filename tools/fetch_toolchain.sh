#!/bin/bash
# Fetches clang-20, lld-20 and libx11-xcb-dev from the distribution's own apt archive and unpacks
# them under ~/.local/ncaabb10-toolchain, without root (Ubuntu 24.04; the apt lists already on the
# system pick the versions). Then: source ~/.local/ncaabb10-toolchain/env.sh before building.
# With root, `sudo apt install clang-20 lld-20 libx11-xcb-dev` does the same system-wide.
set -euo pipefail
root="${NCAABB10_TOOLCHAIN:-$HOME/.local/ncaabb10-toolchain}"
debs="$root/debs"
mkdir -p "$debs"
# What compiling and linking need; clang's other recommends (tools, LLVM dev files) are left out.
# libllvm20 (clang's LLVM) comes with Mesa on Ubuntu 24.04; it is fetched too if missing.
pkgs="clang-20 libclang-cpp20 libclang-common-20-dev libclang-rt-20-dev libclang1-20
      llvm-20-linker-tools lld-20 libx11-xcb-dev libgc1 libobjc4 libobjc-13-dev"
dpkg -s libllvm20 >/dev/null 2>&1 || pkgs="$pkgs libllvm20"
(cd "$debs" && apt-get download $pkgs)
for deb in "$debs"/*.deb; do
  dpkg -x "$deb" "$root"
done
# The x11-xcb pkg-config file points at /usr; point it at the unpacked headers, and link the
# development library name to the runtime library the system already has.
lib="$root/usr/lib/x86_64-linux-gnu"
sed -i "s|^prefix=.*|prefix=$root/usr|" "$lib/pkgconfig/x11-xcb.pc"
ln -sf /usr/lib/x86_64-linux-gnu/libX11-xcb.so.1 "$lib/libX11-xcb.so"
cat > "$root/env.sh" <<EOF
# Puts the unpacked clang-20 and lld-20 first on PATH and makes x11-xcb visible to pkg-config.
export PATH="$root/usr/lib/llvm-20/bin:$root/usr/bin:\$PATH"
export LD_LIBRARY_PATH="$root/usr/lib/llvm-20/lib:$lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$lib/pkgconfig\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
export LIBRARY_PATH="$lib\${LIBRARY_PATH:+:\$LIBRARY_PATH}"
export CPATH="$root/usr/include\${CPATH:+:\$CPATH}"
EOF
. "$root/env.sh"
clang-20 --version | head -1
ld.lld --version
pkg-config --cflags --libs x11-xcb
