#!/usr/bin/env python3
"""Generate test disk images + expected files for the host test."""
import os, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import sys
import mkfat

out = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(out, exist_ok=True)

def prng(n, seed):
    x, b = seed, bytearray()
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        b.append((x >> 16) & 0xFF)
    return bytes(b)

def bmp24(w, h):
    stride = (w * 3 + 3) & ~3
    px = bytearray()
    for y in range(h - 1, -1, -1):            # bottom-up
        row = bytearray()
        for x in range(w):
            row += bytes((x * 255 // w, y * 255 // h, 128))   # B, G, R
        row += b"\0" * (stride - w * 3)
        px += row
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(px), 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(px), 2835, 2835, 0, 0)
    return hdr + dib + bytes(px)

def bmp8(w, h):
    pal = b"".join(bytes((i, 255 - i, (i * 7) & 255, 0)) for i in range(256))
    stride = (w + 3) & ~3
    px = bytearray()
    for y in range(h - 1, -1, -1):
        px += bytes(((x + y) & 255) for x in range(w)) + b"\0" * (stride - w)
    off = 54 + 1024
    hdr = b"BM" + struct.pack("<IHHI", off + len(px), 0, 0, off)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(px), 2835, 2835, 256, 256)
    return hdr + dib + pal + bytes(px)

files = {
    "README.TXT": b"Hello from a FAT drive!\r\nSecond line.\r\n",
    "Hello World.txt": b"This file has a long name with spaces.\n",
    "notes.md": b"# lowercase 8.3 name\n",
    "big.bin": prng(10000, 1),
    "pic24.bmp": bmp24(64, 48),
    "pic8.bmp": bmp8(40, 30),
    "A Very Long File Name Indeed 1234567890.txt": b"long name test\n",
}
docs = {"Report 2026.txt": b"report body\n", "deep": {"x.txt": b"deep file\n", "More Stuff": {"y.txt": b"y\n"}}}
many = {"F%02d.TXT" % i: ("file %d\n" % i).encode() for i in range(40)}
tree = dict(files); tree["Documents"] = docs; tree["Many"] = many
tree["EMPTY.TXT"] = b""

for name, kind, mbr in (("fat16.img", 16, True), ("fat32.img", 32, False), ("fat12.img", 12, False)):
    t = dict(tree)
    if kind == 12:
        t["huge.bin"] = prng(900000, 7)            # >340 clusters: exercises FAT12 entries that straddle sectors
    open(os.path.join(out, name), "wb").write(mkfat.build(kind, t, mbr=mbr))

# expected contents the host test compares against
for n, d in files.items():
    open(os.path.join(out, "exp_" + n.replace(" ", "_")), "wb").write(d)
open(os.path.join(out, "exp_huge.bin"), "wb").write(prng(900000, 7))
# a small FAT16 disk holding the sample apps (handy for trying them in QEMU too)
appdir = os.path.join(os.path.dirname(__file__), "..", "sdk", "out")
apps = {"README.TXT": b"LeactedOS sample apps. Open a .lapp file in the File Manager and choose Run as app.\n"}
if os.path.isdir(appdir):
    for fn in sorted(os.listdir(appdir)):
        if fn.endswith(".lapp"):
            apps[fn] = open(os.path.join(appdir, fn), "rb").read()
open(os.path.join(out, "apps.img"), "wb").write(mkfat.build(16, apps))
# Leact-get repositories: a good one, and a "v2" copy with one app rebuilt (different CRC) plus bad entries
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
if os.path.isdir(appdir):
    good = {fn: open(os.path.join(appdir, fn), "rb").read() for fn in sorted(os.listdir(appdir)) if fn.endswith(".lapp")}
    def idx(rows): return ("LEACT-REPO 1\r\n# test\r\n" + "\r\n".join(rows) + "\r\n").encode()
    rows = ["hello|pkg/hello.lapp|1.0|Hello demo", "snake|pkg/snake.lapp|1.1|Classic snake", "doodle|pkg/doodle.lapp|1.0|Drawing pad",
            "broken|pkg/broken.lapp|0.1|Corrupt package", "ghost|pkg/ghost.lapp|0.1|File missing", "bad name!|pkg/x.lapp|1|skipped"]
    pkgs = dict(good); pkgs["broken.lapp"] = b"NOPE" * 40
    open(os.path.join(out, "repo.img"), "wb").write(mkfat.build(16, {"leact-repo.txt": idx(rows), "pkg": pkgs}))
    p2 = dict(pkgs); h = bytearray(good["hello.lapp"]); h[-1] ^= 0xFF; p2["hello.lapp"] = bytes(h)   # same size, corrupt reloc -> still must be rejected
    sn = bytearray(good["snake.lapp"]); sn[40] = ord("S"); sn[41] = ord("2")                          # name tweak -> header CRC unchanged? (crc covers image only)
    open(os.path.join(out, "repo_bad.img"), "wb").write(mkfat.build(16, {"leact-repo.txt": idx(rows), "pkg": p2}))
    open(os.path.join(out, "norepo.img"), "wb").write(mkfat.build(16, {"README.TXT": b"no index here\n"}))
    open(os.path.join(out, "badindex.img"), "wb").write(mkfat.build(16, {"leact-repo.txt": b"hello|pkg/hello.lapp\n"}))
print("images written to", out)
