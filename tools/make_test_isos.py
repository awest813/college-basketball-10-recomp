"""Builds small synthetic XDVDFS images to test the launcher's disc setup (NCAABB10_TEST_ISO).

usage: python -I make_test_isos.py OUTDIR REAL_DEFAULT_XEX

good.iso holds the real default.xex and 300 MB of filler and must set up; every other image must
fail with nothing written outside <game>.partial.
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


class Image:
    def __init__(self):
        self.data = bytearray(34 * SECTOR)  # Header at sector 32; files start at 34.

    def alloc(self, payload):
        sector = len(self.data) // SECTOR
        self.data += payload
        self.data += bytes((-len(self.data)) % SECTOR)
        return sector

    @staticmethod
    def table(entries):
        """entries: (name bytes, sector, size, attr). Chained through right pointers, in order."""
        raw = []
        offsets = []
        off = 0
        for name, *_ in entries:
            offsets.append(off)
            n = 14 + len(name)
            off += n + (-n) % 4
        for i, (name, sector, size, attr) in enumerate(entries):
            right = offsets[i + 1] // 4 if i + 1 < len(entries) else 0
            e = struct.pack("<HHIIBB", 0, right, sector, size, attr, len(name)) + name
            raw.append(e + b"\xff" * ((-len(e)) % 4))
        t = b"".join(raw)
        return t + b"\xff" * ((-len(t)) % SECTOR)

    def finish(self, root_sector, root_size, truncate=0):
        hdr = MAGIC + struct.pack("<II", root_sector, root_size) + bytes(8)
        self.data[32 * SECTOR:32 * SECTOR + len(hdr)] = hdr
        self.data[33 * SECTOR - 20:33 * SECTOR] = MAGIC
        return bytes(self.data[:len(self.data) - truncate] if truncate else self.data)


def build(files, root_size=None, truncate=0):
    """files: list of (name bytes, payload or (sector, size) override, attr)."""
    img = Image()
    entries = []
    for name, payload, attr in files:
        if isinstance(payload, tuple):
            entries.append((name, payload[0], payload[1], attr))
        else:
            entries.append((name, img.alloc(payload), len(payload), attr))
    t = Image.table(entries)
    root = img.alloc(t)
    return img.finish(root, root_size if root_size is not None else len(t), truncate)


def main():
    out, xex_path = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    xex = open(xex_path, "rb").read()
    small = b"hello\n" * 100
    images = {}
    images["notxbox"] = os.urandom(200 * SECTOR)
    images["noxex"] = build([(b"readme.txt", small, 0)])
    images["wrongver"] = build([(b"default.xex", b"XEX2" + bytes(4096), 0)])
    # A subdirectory whose table is the root table again: an endless loop without the guard.
    img = Image()
    payload = img.alloc(small)
    root_sector = len(img.data) // SECTOR
    t = Image.table([(b"default.xex", payload, len(small), 0), (b"sub", root_sector, 0, 0x10)])
    t = Image.table([(b"default.xex", payload, len(small), 0), (b"sub", root_sector, len(t), 0x10)])
    assert img.alloc(t) == root_sector
    images["loop"] = img.finish(root_sector, len(t))
    # A directory table claiming 2 GB.
    images["hugetable"] = build([(b"default.xex", small, 0), (b"big", (40, 0x7FFFFFF0), 0x10)])
    # Names that would escape the destination, beside a real game xex.
    images["evil"] = build([(b"default.xex", xex, 0), (b"..", small, 0), (b"..\\evil1.txt", small, 0),
                            (b"../evil2.txt", small, 0), (b"C:evil3.txt", small, 0),
                            (b"ok.txt", small, 0)])
    # A file that runs past the end of the image.
    images["truncated"] = build([(b"default.xex", xex, 0)], truncate=SECTOR * 100)
    images["shortread"] = build([(b"default.xex", (34, 50 << 20), 0)])
    # A name that is not valid UTF-8.
    images["badname"] = build([(b"default.xex", xex, 0), (b"bad\xff\xfe.bin", small, 0)])
    # A name Windows cannot create.
    images["winname"] = build([(b"default.xex", xex, 0), (b"a<b>.bin", small, 0)])
    # A Windows device name (writes would vanish into NUL), with an extension as Windows still
    # treats it.
    images["devname"] = build([(b"default.xex", xex, 0), (b"NUL.txt", small, 0)])
    # Two names that differ only in case: one file on Windows, so the second would overwrite.
    images["casedup"] = build([(b"default.xex", xex, 0), (b"Data.bin", small, 0),
                               (b"data.bin", small, 0)])
    # A trailing dot, which Windows drops ("data." would become "data").
    images["trailingdot"] = build([(b"default.xex", xex, 0), (b"data.", small, 0)])
    # 60 files of 4 GB that share one sector: more than any free space, so setup must refuse
    # before copying anything.
    images["nospace"] = build([(b"default.xex", xex, 0)] +
                              [(b"big%02d.bin" % i, (34, 0xFFFFF000), 0) for i in range(60)])
    # A good image: the real xex first, then 300 MB of filler (time for a test to lock a file).
    images["good"] = build([(b"default.xex", xex, 0), (b"filler.bin", bytes(300 << 20), 0)])
    for name, data in images.items():
        with open(os.path.join(out, name + ".iso"), "wb") as f:
            f.write(data)
        print(f"{name}.iso {len(data)}")


if __name__ == "__main__":
    main()
