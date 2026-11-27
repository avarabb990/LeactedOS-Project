#!/usr/bin/env python3
"""Build a bootable LeactedOS install CD (ISO 9660 + El Torito, no emulation) without any external tool.
usage: mkiso.py leacteos.bin leacteos.iso
The CD boots Leact-Loader 1.2 (menu) and starts the kernel from the CD; the Installer inside can then write a
bootable hard disk (qemu: -cdrom leacteos.iso -drive file=disk.img,format=raw,if=ide)."""
import struct, sys, re, os, time

SEC = 2048
here = os.path.dirname(os.path.abspath(__file__))

def header_array(name):
    t = open(os.path.join(here, "..", "boot_bin.h")).read()
    m = re.search(r"%s\[\d+\] = \{(.*?)\};" % name, t, re.S)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-f]{2})", m.group(1)))

def elf_flat(path):
    """flat memory image of the kernel from its first byte at 1 MB, its entry point and the end of its BSS"""
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1, "not a 32-bit ELF kernel"
    entry = struct.unpack_from("<I", d, 24)[0]
    phoff, phentsize, phnum = struct.unpack_from("<I", d, 28)[0], struct.unpack_from("<H", d, 42)[0], struct.unpack_from("<H", d, 44)[0]
    segs = []
    for i in range(phnum):
        t, off, va, pa, fsz, msz = struct.unpack_from("<6I", d, phoff + i * phentsize)
        if t == 1: segs.append((pa, off, fsz, msz))
    base = min(s[0] for s in segs); assert base == 0x100000, hex(base)
    flat_end = max(s[0] + s[2] for s in segs); bss_end = max(s[0] + s[3] for s in segs)
    img = bytearray(flat_end - base)
    for pa, off, fsz, msz in segs: img[pa - base:pa - base + fsz] = d[off:off + fsz]
    return bytes(img), entry, bss_end

def both32(v): return struct.pack("<I", v) + struct.pack(">I", v)
def both16(v): return struct.pack("<H", v) + struct.pack(">H", v)
def pad(b, n, c=b" "): return b + c * (n - len(b))
def dtime(): t = time.gmtime(); return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, 0])
def dstr(): t = time.gmtime(); return ("%04d%02d%02d%02d%02d%02d00" % t[:6]).encode() + b"\0"
def dirrec(extent, size, ident, flags=0):
    n = 33 + len(ident); n += n & 1
    r = bytes([n, 0]) + both32(extent) + both32(size) + dtime() + bytes([flags, 0, 0]) + both16(1) + bytes([len(ident)]) + ident
    return pad(r, n, b"\0")

def build(kernel_path, out_path):
    flat, entry, bss_end = elf_flat(kernel_path)
    elf = open(kernel_path, "rb").read()
    st1, st2 = header_array("boot_stage1cd"), bytearray(header_array("boot_stage2"))
    assert len(st1) == 512 and len(st2) == 4096 and st2[4:8] == b"LST2"
    readme = (b"LeactedOS install CD\r\n\r\nBoot this CD (BIOS). Pick LeactedOS desktop (Normal) or LeactedCmd.\r\n"
              b"To install: attach a hard disk, open Installer, choose the drive, click Install Now twice.\r\n"
              b"LEACTEOS.BIN is the kernel (multiboot ELF) for qemu -kernel or GRUB.\r\n")
    BOOTCAT, PT_L, PT_M, ROOT, BOOT = 19, 20, 21, 22, 24          # CD sectors; stage 1 at 24, stage 2 at 25..26
    kimg = 27
    ksecs = -(-len(flat) // SEC)
    elf_lba = kimg + ksecs; elf_secs = -(-len(elf) // SEC)
    rd_lba = elf_lba + elf_secs; rd_secs = 1
    total = rd_lba + rd_secs
    struct.pack_into("<6I", st2, 8, kimg, ksecs, entry, bss_end, 0x100000 + ksecs * SEC, 1)   # CD mode
    img = bytearray(total * SEC)
    def put(lba, data): img[lba * SEC:lba * SEC + len(data)] = data
    pvd = bytearray(SEC)
    pvd[0:8] = b"\x01CD001\x01\x00"
    pvd[8:40] = pad(b"LEACTEDOS", 32); pvd[40:72] = pad(b"LEACTEDOS", 32)
    pvd[80:88] = both32(total); pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(SEC)
    pvd[132:140] = both32(10)
    pvd[140:144] = struct.pack("<I", PT_L); pvd[148:152] = struct.pack(">I", PT_M)
    pvd[156:190] = dirrec(ROOT, SEC, b"\x00", 2)
    pvd[190:318] = pad(b"LEACTEDOS", 128)
    pvd[318:446] = pad(b"", 128); pvd[446:574] = pad(b"LEACTEDOS PROJECT", 128); pvd[574:702] = pad(b"mkiso.py", 128)
    for off in (702, 739, 776): pvd[off:off + 37] = pad(b"", 37)
    pvd[813:830] = dstr(); pvd[830:847] = dstr(); pvd[847:864] = b"0000000000000000\0"; pvd[864:881] = b"0000000000000000\0"
    pvd[881] = 1
    put(16, pvd)
    br = bytearray(SEC); br[0:7] = b"\x00CD001\x01"; br[7:39] = pad(b"EL TORITO SPECIFICATION", 32, b"\0"); br[71:75] = struct.pack("<I", BOOTCAT)
    put(17, br)
    term = bytearray(SEC); term[0:7] = b"\xffCD001\x01"; put(18, term)
    cat = bytearray(SEC)
    cat[0] = 1; cat[1] = 0; cat[4:28] = pad(b"LEACTEDOS", 24, b"\0"); cat[30] = 0x55; cat[31] = 0xAA
    cat[28:30] = struct.pack("<H", (-sum(struct.unpack("<16H", bytes(cat[0:32])))) & 0xFFFF)
    cat[32] = 0x88; cat[33] = 0; cat[34:36] = struct.pack("<H", 0); cat[36] = 0; cat[38:40] = struct.pack("<H", 4); cat[40:44] = struct.pack("<I", BOOT)
    put(BOOTCAT, cat)
    put(PT_L, bytes([1, 0]) + struct.pack("<I", ROOT) + struct.pack("<H", 1) + b"\x00\x00")
    put(PT_M, bytes([1, 0]) + struct.pack(">I", ROOT) + struct.pack(">H", 1) + b"\x00\x00")
    struct.pack_into("<I", img, 16 * SEC + 132, 10); struct.pack_into(">I", img, 16 * SEC + 136, 10)
    root = dirrec(ROOT, SEC, b"\x00", 2) + dirrec(ROOT, SEC, b"\x01", 2)
    root += dirrec(kimg, len(flat), b"KERNEL.IMG;1") + dirrec(elf_lba, len(elf), b"LEACTEOS.BIN;1") + dirrec(rd_lba, len(readme), b"README.TXT;1")
    put(ROOT, root)
    put(BOOT, st1); put(BOOT + 1, bytes(st2)); put(kimg, flat); put(elf_lba, elf); put(rd_lba, readme)
    open(out_path, "wb").write(img)
    return total, ksecs

if __name__ == "__main__":
    t, k = build(sys.argv[1], sys.argv[2])
    print("%s: %d sectors (%d KB), kernel image %d sectors" % (sys.argv[2], t, t * 2, k))
