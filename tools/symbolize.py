"""Names the frames of a crash report in a game log.

Usage: python -I tools/symbolize.py <log file> [--bin <folder>] [--all]

The SDK's crash report lists each frame as `0x... (module+0xoffset)`. This runs llvm-symbolizer
(from LLVM, the same install as clang) on each one against the binaries in --bin, by default the
folder above the log's `logs` folder, which is the build folder or the unpacked game folder. A
recompiled game function comes out as sub_<guest address>, the address in the game's own code; the
port's and the SDK's code come out with file and line. That needs the debug info of the exact build
that crashed: on Windows the PDBs beside the exe and the DLLs (a Release build keeps them in its
build folder; tools/package.ps1 -Symbols packs them), on Linux the unstripped binaries.

Without --all only the lines of the last crash report are printed.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

FRAME = re.compile(r"\((?P<module>[^()\s+]+)\+0x(?P<offset>[0-9a-fA-F]+)\)")
BANNER = re.compile(r"==== .+ crash report, ")
END = re.compile(r"==== end of crash report ====")


def find_symbolizer() -> str:
    for name in ("llvm-symbolizer", "llvm-symbolizer-20"):
        found = shutil.which(name)
        if found:
            return found
    # Beside clang (LLVM's bin folder), or LLVM's default Windows install folder.
    clang = shutil.which("clang++") or shutil.which("clang++-20")
    candidates = []
    if clang:
        candidates.append(Path(clang).parent)
    if os.name == "nt":
        candidates.append(Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "LLVM" / "bin")
    for folder in candidates:
        for name in ("llvm-symbolizer.exe", "llvm-symbolizer"):
            if (folder / name).is_file():
                return str(folder / name)
    sys.exit("llvm-symbolizer was not found; it comes with LLVM (winget install LLVM.LLVM, or "
             "sudo apt install llvm-20)")


def elf_address(path: Path, offset: int) -> int | None:
    """An ELF file offset (what the Linux report gives) as the virtual address symbols use."""
    with open(path, "rb") as f:
        header = f.read(64)
        if header[:4] != b"\x7fELF" or header[4] != 2:
            return None
        phoff, = struct.unpack_from("<Q", header, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", header, 0x36)
        for i in range(phnum):
            f.seek(phoff + i * phentsize)
            kind, _flags, p_offset, p_vaddr, _paddr, p_filesz = struct.unpack("<IIQQQQ", f.read(40))
            if kind == 1 and p_offset <= offset < p_offset + p_filesz:  # PT_LOAD
                return offset - p_offset + p_vaddr
    return None


def symbolize(symbolizer: str, binary: Path, offsets: list[int]) -> dict[int, str]:
    """Maps each offset to 'function at file:line' (or just the function)."""
    pe = binary.suffix.lower() in (".exe", ".dll")
    queries = []
    for offset in offsets:
        address = offset if pe else elf_address(binary, offset)
        queries.append(address if address is not None else offset)
    args = [symbolizer, f"--obj={binary}", "--demangle", "--inlines"]
    if pe:
        args.append("--relative-address")  # The report's offsets are from the image base.
    text = subprocess.run(args, input="\n".join(f"0x{q:x}" for q in queries) + "\n",
                          capture_output=True, text=True, check=False).stdout
    results = {}
    blocks = text.strip().split("\n\n")
    for offset, block in zip(offsets, blocks):
        lines = [line.strip() for line in block.splitlines() if line.strip()]
        if not lines or lines[0] == "??":
            continue
        # Pairs of function, file:line; with inlining, the innermost first.
        frames = []
        for function, where in zip(lines[0::2], lines[1::2]):
            if where.startswith("??"):
                frames.append(function)
            else:
                frames.append(f"{function} at {where}")
        results[offset] = "  <-  ".join(frames) if frames else lines[0]
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path)
    parser.add_argument("--bin", type=Path, help="folder with the exe and DLLs (or binaries) that ran")
    parser.add_argument("--all", action="store_true", help="every line of the log, not only the last crash report")
    args = parser.parse_args()

    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    if not args.all:
        starts = [i for i, line in enumerate(lines) if BANNER.search(line)]
        if not starts:
            sys.exit(f"{args.log} has no crash report (use --all to name every module+offset in it)")
        lines = lines[starts[-1]:]
        ends = [i for i, line in enumerate(lines) if END.search(line)]
        if ends:
            lines = lines[:ends[0] + 1]
    binaries = args.bin or args.log.resolve().parent.parent
    symbolizer = find_symbolizer()

    wanted: dict[str, set[int]] = {}
    for line in lines:
        for match in FRAME.finditer(line):
            wanted.setdefault(match["module"], set()).add(int(match["offset"], 16))
    names: dict[tuple[str, int], str] = {}
    missing = []
    for module, offsets in wanted.items():
        binary = binaries / module
        if not binary.is_file():
            missing.append(module)
            continue
        ordered = sorted(offsets)
        for offset, name in symbolize(symbolizer, binary, ordered).items():
            names[(module, offset)] = name

    for line in lines:
        found = [names.get((m["module"], int(m["offset"], 16))) for m in FRAME.finditer(line)]
        found = [n for n in found if n]
        print(f"{line}  ->  {'; '.join(found)}" if found else line)
    if missing:
        print(f"\nnot in {binaries}: {', '.join(sorted(missing))}", file=sys.stderr)


if __name__ == "__main__":
    main()
