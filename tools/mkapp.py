#!/usr/bin/env python3
"""mkapp.py - turn a linked ELF (ld -q, base 0) into a LeactedOS .lapp file.

    python3 tools/mkapp.py app.elf -o app.lapp --name "My App" --size 320x200
"""
import argparse, struct, sys, zlib

SHT_PROGBITS, SHT_NOBITS, SHT_REL = 1, 8, 9
SHF_ALLOC = 2
R_386_32, R_386_PC32, R_386_PLT32 = 1, 2, 4
MAGIC, FORMAT, API_MIN = 0x5050414C, 1, 1
MAX_MEM, MAX_RELOC = 65536, 4096


def die(msg):
    sys.exit("mkapp: " + msg)


def read_elf(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        die("not a 32-bit little-endian ELF file")
    (etype, mach, _ver, entry, phoff, shoff, _fl, _eh, _phes, _phn, shentsize, shnum, shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
    if mach != 3:
        die("not an i386 ELF (compile with -m32)")
    secs = []
    for i in range(shnum):
        (name, typ, flags, addr, off, size, link, info, align, entsz) = struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info))
    stroff = secs[shstrndx]["off"]
    for s in secs:
        e = d.index(b"\0", stroff + s["name"])
        s["name"] = d[stroff + s["name"]:e].decode()
    return d, entry, secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--name", default="App")
    ap.add_argument("--size", default="320x200", help="client area, WIDTHxHEIGHT")
    a = ap.parse_args()
    try:
        win_w, win_h = (int(v) for v in a.size.lower().split("x"))
    except ValueError:
        die("--size must look like 320x200")
    if not (64 <= win_w <= 620 and 48 <= win_h <= 420):
        die("window size must be between 64x48 and 620x420")
    if len(a.name.encode()) > 23:
        die("name is limited to 23 characters")

    d, entry, secs = read_elf(a.elf)
    alloc = [s for s in secs if s["flags"] & SHF_ALLOC and s["size"]]
    if not alloc:
        die("no loadable sections")
    base = min(s["addr"] for s in alloc)
    if base != 0:
        die("app must be linked at address 0 (use sdk/app.ld)")
    img_end = max(s["addr"] + s["size"] for s in alloc if s["type"] == SHT_PROGBITS)
    bss_end = max(s["addr"] + s["size"] for s in alloc)
    image = bytearray(img_end)
    for s in alloc:
        if s["type"] == SHT_PROGBITS:
            image[s["addr"]:s["addr"] + s["size"]] = d[s["off"]:s["off"] + s["size"]]
    while len(image) % 4:
        image.append(0)
    bss = bss_end - len(image) if bss_end > len(image) else 0
    if len(image) + bss > MAX_MEM:
        die("app needs %d bytes, limit is %d" % (len(image) + bss, MAX_MEM))

    relocs = []
    for s in secs:
        if s["type"] != SHT_REL:
            continue
        target = secs[s["info"]]
        if not target["flags"] & SHF_ALLOC:
            continue
        for k in range(s["size"] // 8):
            r_off, r_info = struct.unpack_from("<II", d, s["off"] + k * 8)
            t = r_info & 0xFF
            if t == R_386_32:
                relocs.append(r_off)
            elif t in (R_386_PC32, R_386_PLT32):
                pass                      # position independent
            else:
                die("unsupported relocation type %d in %s (do not use -fPIC)" % (t, target["name"]))
    relocs = sorted(set(relocs))
    if len(relocs) > MAX_RELOC:
        die("too many relocations")
    for r in relocs:
        if r + 4 > len(image):
            die("relocation outside the image")
    if entry >= len(image):
        die("entry point outside the image")

    body = bytes(image) + b"".join(struct.pack("<I", r) for r in relocs)
    hdr = struct.pack("<IHHIIIIIIHHI24s", MAGIC, FORMAT, 0, API_MIN, entry, len(image), bss,
                      len(relocs), zlib.crc32(body) & 0xFFFFFFFF, win_w, win_h, 0, a.name.encode())
    assert len(hdr) == 64
    open(a.output, "wb").write(hdr + body)
    print("%s: %d bytes (image %d, bss %d, %d relocations, window %dx%d)" % (a.output, 64 + len(body), len(image), bss, len(relocs), win_w, win_h))


main()
