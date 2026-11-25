"""Writes THIRD_PARTY_NOTICES.txt for a packaged build.

The licenses of the code inside the shipped binaries ask for their notices to travel with them.
This gathers each one from the SDK source tree the build used, so the text is always the version
that was compiled in.

    python -I tools/notices.py <rexglue-sdk-src> <output file>
"""

import re
import subprocess
import sys
from pathlib import Path

# (name, where it ships, license, path under the SDK, how to take the text)
#   "file": the whole file
#   ("lead", stop): the file's opening lines, up to the first line matching stop
#   ("upto", end): the file's opening lines, through the first line matching end
#   ("from", start): from the first line matching start to the end
COMPONENTS = [
    ("ReXGlue SDK (with portions from Xenia)", "every rex*.dll", "BSD 3-Clause",
     "LICENSE", "file"),
    ("SDL 3", "SDL3.dll", "zlib", "thirdparty/sdl3/LICENSE.txt", "file"),
    ("Dear ImGui", "rexbase.dll", "MIT", "thirdparty/imgui/LICENSE.txt", "file"),
    ("spdlog", "rexbase.dll", "MIT", "thirdparty/spdlog/LICENSE", "file"),
    ("toml++", "rexbase.dll, rexkernel.dll", "MIT", "thirdparty/tomlplusplus/LICENSE", "file"),
    ("xxHash", "rexbase.dll, rexkernel.dll, rexgpu-xenos-*.dll", "BSD 2-Clause",
     "thirdparty/xxHash/LICENSE", "file"),
    ("SIMDe", "rexbase.dll and the modules built on it", "MIT", "thirdparty/simde/COPYING",
     "file"),
    ("stb_image", "rexbase.dll, rexui.dll", "MIT or public domain",
     "thirdparty/stb/stb_image.h", ("from", r"This software is available under 2 licenses")),
    ("excrypt", "rexkernel.dll, rexfilesystem.dll", "BSD 3-Clause",
     "thirdparty/excrypt/LICENSE", "file"),
    ("TLSF", "rexkernel.dll", "BSD", "thirdparty/tlsf/README.md",
     ("upto", r"Released under the BSD license")),
    ("AMD DXBC checksum", "rexgpu-xenos-d3d12.dll", "AMD permissive license",
     "thirdparty/dxbc/DXBCChecksum.cpp", ("lead", r"^\s*#include")),
    ("RenderDoc in-application API header", "rexgpu-xenos-*.dll", "MIT",
     "thirdparty/renderdoc/renderdoc_app.h", ("lead", r"^\s*#")),
    ("glslang", "rexgpu-xenos-vulkan.dll", "BSD 3-Clause and others (see text)",
     "thirdparty/glslang/LICENSE.txt", "file"),
    ("Vulkan Memory Allocator", "rexgpu-xenos-vulkan.dll", "MIT",
     "thirdparty/VulkanMemoryAllocator/LICENSE.txt", "file"),
    ("Vulkan-Headers", "rexgpu-xenos-vulkan.dll", "Apache 2.0 and MIT",
     "thirdparty/Vulkan-Headers/LICENSE.md", "file"),
    ("FFmpeg (libavcodec, libavutil)", "rexkernel.dll", "LGPL 2.1 or later",
     "thirdparty/FFmpeg/LICENSE.md", "file"),
    ("libmspack", "rexfilesystem.dll", "LGPL 2.1",
     "thirdparty/libmspack/libmspack/COPYING.LIB", "file"),
]

LGPL_TEXT = "thirdparty/FFmpeg/COPYING.LGPLv2.1"

HEADER = """\
NCAA Basketball 10 PC port: third-party notices
================================================

This build is made with the ReXGlue SDK and the libraries below. Each section names the
module that contains it and gives its license text, taken from the source the build used.

The Visual C++ runtime DLLs (vcruntime140*.dll, msvcp140*.dll) are Microsoft's redistributable
files, shipped beside the executable as Microsoft's Visual Studio license allows.

LGPL libraries: FFmpeg ({ffmpeg_decoders} decoders only, commit {ffmpeg_commit}) is built into
rexkernel.dll, and libmspack (commit {mspack_commit}) into rexfilesystem.dll. Their source, and
the complete source of those two DLLs, is the ReXGlue SDK 0.11.0 with its submodules
(https://github.com/rexglue/rexglue-sdk, tag v0.11.0) plus the patches in this port's
patches/ folder. Rebuilding the SDK from it (tools/build_sdk.cmd or tools/build_sdk.sh) gives
replacement DLLs, built with a modified FFmpeg or libmspack if you like, that drop in beside
the executable. The full LGPL 2.1 text is at the end of this file.
"""


def git_commit(path: Path) -> str:
    try:
        out = subprocess.run(["git", "-C", str(path), "rev-parse", "--short=12", "HEAD"],
                             capture_output=True, text=True, check=True)
        return out.stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def ffmpeg_decoders(sdk: Path) -> str:
    listing = sdk / "thirdparty/ffmpeg-overlay/codec_list.c"
    names = re.findall(r"&ff_(\w+)_decoder", listing.read_text(encoding="utf-8"))
    return ", ".join(names) if names else "audio"


def take(text: str, how) -> str:
    lines = text.splitlines()
    if how == "file":
        return text.rstrip()
    kind, pattern = how
    rx = re.compile(pattern)
    if kind == "lead":
        out = []
        for i, line in enumerate(lines):
            if i > 0 and rx.search(line):
                break
            out.append(line)
        return "\n".join(out).rstrip()
    if kind == "upto":
        for i, line in enumerate(lines):
            if rx.search(line):
                return "\n".join(lines[:i + 1]).rstrip()
    if kind == "from":
        for i, line in enumerate(lines):
            if rx.search(line):
                return "\n".join(lines[i:]).rstrip()
    raise ValueError(f"no license text found by {how}")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    sdk = Path(sys.argv[1])
    out_path = Path(sys.argv[2])
    parts = [HEADER.format(
        ffmpeg_decoders=ffmpeg_decoders(sdk),
        ffmpeg_commit=git_commit(sdk / "thirdparty/FFmpeg"),
        mspack_commit=git_commit(sdk / "thirdparty/libmspack"))]
    for name, where, license_name, rel, how in COMPONENTS:
        source = sdk / rel
        if not source.is_file():
            print(f"notices: missing {source}", file=sys.stderr)
            return 1
        body = take(source.read_text(encoding="utf-8", errors="replace"), how)
        parts.append(f"\n\n{'-' * 78}\n{name}\nIn: {where}\nLicense: {license_name}\n"
                     f"{'-' * 78}\n\n{body}")
    lgpl = sdk / LGPL_TEXT
    parts.append(f"\n\n{'=' * 78}\nGNU Lesser General Public License, version 2.1\n{'=' * 78}\n\n"
                 + lgpl.read_text(encoding="utf-8", errors="replace").rstrip())
    out_path.write_text("".join(parts) + "\n", encoding="utf-8", newline="\r\n")
    print(f"notices: {len(COMPONENTS)} components into {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
