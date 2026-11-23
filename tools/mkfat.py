#!/usr/bin/env python3
"""mkfat.py - build a FAT12/16/32 disk image from a folder (no root needed).

  python3 mkfat.py disk.img --type 32 --src myfiles/            # 40 MB FAT32, no partition table
  python3 mkfat.py disk.img --type 16 --size-mb 16 --mbr --src myfiles/

Attach the result to LeactedOS with:  qemu-system-i386 ... -drive file=disk.img,format=raw,if=ide
"""
import argparse, math, os, struct

ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")
DATE = ((2026 - 1980) << 9) | (11 << 5) | 20
TIME = (12 << 11)


def lfn_checksum(short11):
    s = 0
    for c in short11:
        s = (((s & 1) << 7) + (s >> 1) + c) & 0xFF
    return s


def lfn_entries(name, chk):
    u = name.encode("utf-16-le")
    chars = [u[i] | (u[i + 1] << 8) for i in range(0, len(u), 2)]
    if len(chars) % 13:
        chars.append(0)
        while len(chars) % 13:
            chars.append(0xFFFF)
    n = len(chars) // 13
    offs = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
    out = []
    for i in range(n):
        e = bytearray(32)
        e[0] = (i + 1) | (0x40 if i == n - 1 else 0)
        e[11] = 0x0F
        e[13] = chk
        for k, o in enumerate(offs):
            struct.pack_into("<H", e, o, chars[i * 13 + k])
        out.append(bytes(e))
    return out[::-1]


class Fat:
    def __init__(self, kind, total, spc, hidden=0, label="LEACTEDOS"):
        self.kind, self.total, self.spc, self.hidden, self.label = kind, total, spc, hidden, label
        self.rsvd = 32 if kind == 32 else 1
        self.nfats = 2
        self.rootent = 0 if kind == 32 else 512
        self.rootsec = (self.rootent * 32 + 511) // 512
        fatsz = 1
        while True:
            clusters = (total - self.rsvd - self.nfats * fatsz - self.rootsec) // spc
            need = math.ceil((clusters + 2) * {12: 1.5, 16: 2, 32: 4}[kind] / 512)
            if need <= fatsz:
                break
            fatsz = need
        self.fatsz, self.clusters = fatsz, clusters
        t = 12 if clusters < 4085 else 16 if clusters < 65525 else 32
        assert t == kind, "size/cluster size gives FAT%d, not FAT%d (%d clusters)" % (t, kind, clusters)
        self.eoc = {12: 0xFFF, 16: 0xFFFF, 32: 0x0FFFFFFF}[kind]
        self.fat = [0] * (clusters + 2)
        self.fat[0] = {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[kind]
        self.fat[1] = self.eoc
        self.data = bytearray(clusters * spc * 512)
        self.next = 2
        self.rootdir = b""
        self.root_cluster = 0

    # ---- allocation ----
    def alloc(self, n):
        first = self.next
        assert first + n <= self.clusters + 2, "image is full"
        for i in range(n):
            self.fat[first + i] = first + i + 1 if i < n - 1 else self.eoc
        self.next += n
        return first

    def put(self, first, data):
        off = (first - 2) * self.spc * 512
        self.data[off:off + len(data)] = data

    def write_file(self, data):
        if not data:
            return 0
        n = -(-len(data) // (self.spc * 512))
        first = self.alloc(n)
        self.put(first, data)
        return first

    # ---- names ----
    def make_short(self, name, used):
        base, dot, ext = name.rpartition(".") if "." in name else (name, "", "")
        if base == "" and dot:
            base, ext = name, ""
        fits = (0 < len(base) <= 8 and len(ext) <= 3 and
                all(c in ALLOWED for c in (base + ext).upper()) and name.count(".") <= 1 and " " not in name)
        same_case = name == name.upper() or name == name.lower()
        if fits and same_case:
            nt = 0
            if name == name.lower():
                if any(c.isalpha() for c in base): nt |= 0x08
                if any(c.isalpha() for c in ext): nt |= 0x10
            short = (base.upper().ljust(8) + ext.upper().ljust(3)).encode("ascii")
            used.add(short)
            return short, nt, False
        b = "".join(c for c in base.upper() if c in ALLOWED)[:6] or "FILE"
        e = "".join(c for c in ext.upper() if c in ALLOWED)[:3]
        n = 1
        while True:
            short = ((b + "~%d" % n).ljust(8) + e.ljust(3)).encode("ascii")
            if short not in used:
                used.add(short)
                return short, 0, True
            n += 1

    @staticmethod
    def dirent(short, attr, nt, cluster, size):
        e = bytearray(32)
        e[0:11] = short
        e[11] = attr
        e[12] = nt
        struct.pack_into("<HHH", e, 14, TIME, DATE, DATE)
        struct.pack_into("<H", e, 20, cluster >> 16)
        struct.pack_into("<HH", e, 22, TIME, DATE)
        struct.pack_into("<H", e, 26, cluster & 0xFFFF)
        struct.pack_into("<I", e, 28, size)
        return bytes(e)

    def make_dir(self, tree, parent, is_root=False):
        used, items = set(), []
        for name, val in tree.items():
            short, nt, lfn = self.make_short(name, used)
            ents = lfn_entries(name, lfn_checksum(short)) if lfn else []
            items.append((val, short, nt, ents))
        count = sum(len(i[3]) + 1 for i in items) + (0 if is_root else 2)
        if is_root and self.kind != 32:
            assert count <= self.rootent, "too many files in the root folder"
            first = 0
        else:
            first = self.alloc(max(1, -(-count * 32 // (self.spc * 512))))
            if is_root:
                self.root_cluster = first
        out = bytearray()
        if not is_root:
            out += self.dirent(b".          ", 0x10, 0, first, 0)
            out += self.dirent(b"..         ", 0x10, 0, parent, 0)
        for val, short, nt, ents in items:
            for e in ents:
                out += e
            if isinstance(val, dict):
                c = self.make_dir(val, 0 if is_root else first)
                out += self.dirent(short, 0x10, nt, c, 0)
            else:
                c = self.write_file(val)
                out += self.dirent(short, 0x20, nt, c, len(val))
        if is_root and self.kind != 32:
            self.rootdir = bytes(out)
            return 0
        self.put(first, bytes(out))
        return first

    # ---- image ----
    def pack_fat(self):
        n = self.fatsz * 512
        b = bytearray(n)
        for i, v in enumerate(self.fat):
            if self.kind == 12:
                o = i + i // 2
                if i % 2 == 0:
                    b[o] = v & 0xFF; b[o + 1] = (b[o + 1] & 0xF0) | ((v >> 8) & 0x0F)
                else:
                    b[o] = (b[o] & 0x0F) | ((v & 0x0F) << 4); b[o + 1] = (v >> 4) & 0xFF
            elif self.kind == 16:
                struct.pack_into("<H", b, i * 2, v)
            else:
                struct.pack_into("<I", b, i * 4, v)
        return bytes(b)

    def boot_sector(self):
        b = bytearray(512)
        b[0:3] = b"\xEB\x3C\x90"
        b[3:11] = b"MSWIN4.1"
        struct.pack_into("<HBHBH", b, 11, 512, self.spc, self.rsvd, self.nfats, self.rootent)
        if self.kind != 32 and self.total < 65536:
            struct.pack_into("<H", b, 19, self.total)
        b[21] = 0xF8
        if self.kind != 32:
            struct.pack_into("<H", b, 22, self.fatsz)
        struct.pack_into("<HHI", b, 24, 63, 255, self.hidden)
        if self.kind == 32 or self.total >= 65536:
            struct.pack_into("<I", b, 32, self.total)
        ext = 36
        if self.kind == 32:
            struct.pack_into("<I", b, 36, self.fatsz)
            struct.pack_into("<HHI", b, 40, 0, 0, self.root_cluster)
            struct.pack_into("<HH", b, 48, 1, 6)
            ext = 64
        b[ext] = 0x80; b[ext + 2] = 0x29
        struct.pack_into("<I", b, ext + 3, 0x1234ABCD)
        b[ext + 7:ext + 18] = self.label.upper().ljust(11)[:11].encode("ascii")
        b[ext + 18:ext + 26] = ("FAT%d" % self.kind).ljust(8).encode("ascii")
        b[510:512] = b"\x55\xAA"
        return bytes(b)

    def image(self):
        img = bytearray(self.total * 512)
        bs = self.boot_sector()
        img[0:512] = bs
        if self.kind == 32:
            fsi = bytearray(512)
            struct.pack_into("<I", fsi, 0, 0x41615252); struct.pack_into("<I", fsi, 484, 0x61417272)
            struct.pack_into("<II", fsi, 488, 0xFFFFFFFF, 0xFFFFFFFF); fsi[510:512] = b"\x55\xAA"
            img[512:1024] = fsi
            img[6 * 512:7 * 512] = bs
        f = self.pack_fat()
        for i in range(self.nfats):
            o = (self.rsvd + i * self.fatsz) * 512
            img[o:o + len(f)] = f
        ro = (self.rsvd + self.nfats * self.fatsz) * 512
        img[ro:ro + len(self.rootdir)] = self.rootdir
        do = ro + self.rootsec * 512
        img[do:do + len(self.data)] = self.data
        return img


def tree_from_dir(path):
    t = {}
    for n in sorted(os.listdir(path)):
        p = os.path.join(path, n)
        t[n] = tree_from_dir(p) if os.path.isdir(p) else open(p, "rb").read()
    return t


def build(kind, tree, size_mb=None, mbr=False, label="LEACTEDOS"):
    spc, default_mb = {12: (2, 2), 16: (2, 8), 32: (1, 40)}[kind]
    mb = size_mb or default_mb
    total = mb * 2048
    if kind == 32:
        spc = 1 if mb <= 260 else 8
    elif kind == 16:
        spc = 2 if mb <= 64 else 8
    start = 2048 if mbr else 0
    f = Fat(kind, total - start, spc, hidden=start, label=label)
    f.make_dir(tree, 0, is_root=True)
    body = f.image()
    if not mbr:
        return bytes(body)
    mb_ = bytearray(512)
    t = {12: 0x01, 16: 0x06, 32: 0x0C}[kind]
    mb_[0x1BE:0x1BE + 16] = struct.pack("<B3sB3sII", 0x80, b"\x00\x01\x00", t, b"\xFE\xFF\xFF", start, total - start)
    mb_[510:512] = b"\x55\xAA"
    return bytes(mb_) + b"\0" * ((start - 1) * 512) + bytes(body)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--type", type=int, choices=(12, 16, 32), default=32)
    ap.add_argument("--size-mb", type=int)
    ap.add_argument("--mbr", action="store_true", help="add an MBR partition table")
    ap.add_argument("--src", required=True, help="folder to copy into the image")
    ap.add_argument("--label", default="LEACTEDOS")
    a = ap.parse_args()
    open(a.out, "wb").write(build(a.type, tree_from_dir(a.src), a.size_mb, a.mbr, a.label))
    print("wrote", a.out)
