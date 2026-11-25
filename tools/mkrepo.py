#!/usr/bin/env python3
"""mkrepo.py - build a Leact-get repository disk image from .lapp files.

  python3 tools/mkrepo.py repo.img sdk/out/*.lapp
  python3 tools/mkrepo.py repo.img --desc snake="Classic snake game" sdk/out/snake.lapp
  python3 tools/mkrepo.py --dir repo/ sdk/out/*.lapp        # a plain folder, e.g. to publish on GitHub

The disk is a small FAT16 image with leact-repo.txt (the index) plus the
packages in a "pkg" folder.  Attach it to QEMU as a second drive:
  qemu-system-i386 -kernel kernel.bin -drive file=repo.img,format=raw,if=ide,index=1
then in the LeactedOS terminal:   leact-get list    leact-get install snake
Name, version and size come from each .lapp header.
"""
import argparse, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfat

MAGIC = 0x5050414C


def read_header(data, path):
    if len(data) < 64 or struct.unpack_from("<I", data, 0)[0] != MAGIC:
        sys.exit(f"{path}: not a .lapp file")
    name = data[40:64].split(b"\0")[0].decode("ascii", "replace")
    return name


def index_text(entries):
    lines = ["LEACT-REPO 1", "# name|file|version|description"]
    for name, rel, ver, desc in entries:
        lines.append(f"{name}|{rel}|{ver}|{desc}")
    return ("\n".join(lines) + "\n").encode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", help="image file to write, or the folder when --dir is used")
    ap.add_argument("--dir", action="store_true", help="write leact-repo.txt and pkg/ as real files instead of a disk image")
    ap.add_argument("apps", nargs="+")
    ap.add_argument("--desc", action="append", default=[], help="name=description")
    ap.add_argument("--ver", default="1.0")
    ap.add_argument("--size-mb", type=int, default=None)
    a = ap.parse_args()
    descs = dict(d.split("=", 1) for d in a.desc)
    entries, tree = [], {}
    pkg = {}
    for path in a.apps:
        data = open(path, "rb").read()
        title = read_header(data, path)
        base = os.path.splitext(os.path.basename(path))[0]
        name = "".join(c for c in base if c.isalnum() or c in "-_")[:17]
        pkg[os.path.basename(path)] = data
        entries.append((name, "pkg/" + os.path.basename(path), a.ver, descs.get(name, title)))
    tree["leact-repo.txt"] = index_text(entries)
    tree["pkg"] = pkg
    if a.dir:
        os.makedirs(os.path.join(a.out, "pkg"), exist_ok=True)
        open(os.path.join(a.out, "leact-repo.txt"), "wb").write(tree["leact-repo.txt"])
        for fn, data in pkg.items():
            open(os.path.join(a.out, "pkg", fn), "wb").write(data)
        print(f"wrote folder {a.out}: {len(entries)} app(s)")
        return
    open(a.out, "wb").write(mkfat.build(16, tree, size_mb=a.size_mb))
    print(f"wrote {a.out}: {len(entries)} app(s)")


main()
