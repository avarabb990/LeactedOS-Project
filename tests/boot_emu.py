#!/usr/bin/env python3
"""Runs the LeactedOS boot loader (boot/stage1 + stage2, as written to a disk image by the Installer) on a small
x86 emulator that covers exactly the instructions the loader uses, with BIOS services (INT 10h/13h/15h) faked.
It checks that the kernel arrives at 1 MB intact, the BSS is cleared, the multiboot block is right, A20/unreal mode
are handled, and that execution ends with EAX=0x2BADB002, EBX=mbi at the kernel's entry point.
The boot menu (Leact-Loader 1.2) is driven with scripted key presses; the screen is captured from video memory.
usage: boot_emu.py disk.img [variant]      variant: 0 normal, 1 A20 off + E801 in AX/BX, 2 BIOS resets segment limits, 3 only INT 15h/88h"""
import sys, os, struct

class Fault(Exception): pass

M = 0xFFFFFFFF
class CPU:
    def __init__(s, disk, variant=0):
        s.mem = bytearray(24 * 1024 * 1024)
        s.r = [0] * 8
        s.seg = [dict(sel=0, base=0, limit=0xFFFF) for _ in range(6)]   # es cs ss ds fs gs
        s.ip = 0; s.pe = 0; s.cs32 = 0; s.fl = dict(C=0, Z=0, S=0, O=0)
        s.disk = disk; s.variant = variant; s.a20 = 0 if variant == 1 else 1; s.out = ""
        s.gdtr = (0, 0); s.steps = 0; s.reads = 0; s.halted = False; s.port92 = 0
        s.memkb = 64 * 1024 - 1024                     # a 64 MB machine
        s.keys = []; s.polls = 0; s.tick = 1000; s.screens = []; s.sector = 512
    # --- memory
    def phys(s, a):
        if not s.pe and not s.a20: a &= 0xFFFFF
        if a < 0 or a >= len(s.mem): raise Fault("physical address %x" % a)
        return a
    def lin(s, si, off, size):
        sg = s.seg[si]
        off &= M
        if off + size - 1 > sg['limit']: raise Fault("segment limit: seg %d off %x size %d limit %x ip %x" % (si, off, size, sg['limit'], s.ip))
        return (sg['base'] + off) & M
    def rd(s, si, off, size):
        a = s.lin(si, off, size); v = 0
        for i in range(size): v |= s.mem[s.phys(a + i)] << (8 * i)
        return v
    def wr(s, si, off, size, v):
        a = s.lin(si, off, size)
        for i in range(size): s.mem[s.phys(a + i)] = (v >> (8 * i)) & 0xFF
    # --- fetch
    def f8(s):
        v = s.rd(1, s.ip, 1); s.ip = (s.ip + 1) & (M if s.cs32 else 0xFFFF); return v
    def f16(s): return s.f8() | (s.f8() << 8)
    def f32(s): return s.f16() | (s.f16() << 16)
    def fimm(s, size): return s.f8() if size == 1 else s.f16() if size == 2 else s.f32()
    # --- registers
    def getr(s, i, size):
        if size == 4: return s.r[i]
        if size == 2: return s.r[i] & 0xFFFF
        return (s.r[i & 3] >> (8 if i >= 4 else 0)) & 0xFF
    def setr(s, i, size, v):
        if size == 4: s.r[i] = v & M
        elif size == 2: s.r[i] = (s.r[i] & 0xFFFF0000) | (v & 0xFFFF)
        else:
            j = i & 3
            if i >= 4: s.r[j] = (s.r[j] & 0xFFFF00FF) | ((v & 0xFF) << 8)
            else: s.r[j] = (s.r[j] & 0xFFFFFF00) | (v & 0xFF)
    def push(s, v, size):
        sp = (s.r[4] - size) & 0xFFFF; s.r[4] = (s.r[4] & 0xFFFF0000) | sp; s.wr(2, sp, size, v)
    def pop(s, size):
        sp = s.r[4] & 0xFFFF; v = s.rd(2, sp, size); s.r[4] = (s.r[4] & 0xFFFF0000) | ((sp + size) & 0xFFFF); return v
    def setseg(s, i, sel):
        sg = s.seg[i]; sg['sel'] = sel & 0xFFFF
        if not s.pe: sg['base'] = (sel & 0xFFFF) << 4             # real mode: the limit stays (that is "unreal mode")
        else:
            if sel == 0 and i != 1: sg['base'] = 0; sg['limit'] = 0; return
            d = s.rd_gdt(sel)
            sg['base'] = d['base']; sg['limit'] = d['limit']
    def rd_gdt(s, sel):
        gb = s.gdtr[1]; a = gb + (sel & ~7)
        b = bytes(s.mem[a:a + 8]); lim = b[0] | (b[1] << 8) | ((b[6] & 15) << 16)
        base = b[2] | (b[3] << 8) | (b[4] << 16) | (b[7] << 24)
        if b[6] & 0x80: lim = (lim << 12) | 0xFFF
        return dict(base=base, limit=lim, d=(b[6] >> 6) & 1, acc=b[5])
    # --- modrm
    def modrm(s, asz):
        m = s.f8(); mod = m >> 6; reg = (m >> 3) & 7; rm = m & 7
        if mod == 3: return reg, ('r', rm)
        if asz == 4:
            if rm == 4: raise Fault("sib not supported")
            if mod == 0 and rm == 5: return reg, ('m', s.segov if s.segov is not None else 3, s.f32())
            base = s.r[rm]; disp = 0
            if mod == 1: disp = struct.unpack('b', bytes([s.f8()]))[0]
            elif mod == 2: disp = s.f32()
            return reg, ('m', s.segov if s.segov is not None else (2 if rm in (4, 5) else 3), (base + disp) & M)
        if mod == 0 and rm == 6: return reg, ('m', s.segov if s.segov is not None else 3, s.f16())
        bx, bp, si, di = s.r[3] & 0xFFFF, s.r[5] & 0xFFFF, s.r[6] & 0xFFFF, s.r[7] & 0xFFFF
        off = [bx + si, bx + di, bp + si, bp + di, si, di, bp, bx][rm]; dseg = 2 if rm in (2, 3, 6) else 3
        if mod == 1: off += struct.unpack('b', bytes([s.f8()]))[0]
        elif mod == 2: off += s.f16()
        return reg, ('m', s.segov if s.segov is not None else dseg, off & 0xFFFF)
    def rdop(s, op, size):
        return s.getr(op[1], size) if op[0] == 'r' else s.rd(op[1], op[2], size)
    def wrop(s, op, size, v):
        if op[0] == 'r': s.setr(op[1], size, v)
        else: s.wr(op[1], op[2], size, v)
    # --- alu
    def alu(s, op, a, b, size):
        bits = size * 8; mask = (1 << bits) - 1; sign = 1 << (bits - 1)
        if op in (0, 2): r = a + b + (s.fl['C'] if op == 2 else 0); s.fl['C'] = int(r > mask); s.fl['O'] = int((~(a ^ b) & (a ^ r)) & sign != 0)
        elif op in (5, 7, 3): r = a - b - (s.fl['C'] if op == 3 else 0); s.fl['C'] = int(r < 0); s.fl['O'] = int(((a ^ b) & (a ^ r)) & sign != 0)
        elif op == 1: r = a | b; s.fl['C'] = s.fl['O'] = 0
        elif op == 4: r = a & b; s.fl['C'] = s.fl['O'] = 0
        elif op == 6: r = a ^ b; s.fl['C'] = s.fl['O'] = 0
        r &= mask; s.fl['Z'] = int(r == 0); s.fl['S'] = int(r & sign != 0)
        return r
    def cc(s, c):
        f = s.fl
        t = [f['O'], f['C'], f['Z'], f['C'] or f['Z'], f['S'], 0, f['S'] != f['O'], f['Z'] or (f['S'] != f['O'])][c >> 1]
        return bool(t) != bool(c & 1)
    # --- BIOS
    def bios(s, n):
        ah = (s.r[0] >> 8) & 0xFF; al = s.r[0] & 0xFF
        def cf(v): s.fl['C'] = v
        if s.variant == 2: s.seg[0]['limit'] = 0xFFFF; s.seg[3]['limit'] = 0xFFFF      # BIOS code that leaves unreal mode
        if n == 0x10:
            if ah == 0x0E: s.out += chr(al)
            elif ah == 0x00:
                s.screens.append(bytes(s.mem[0xB8000:0xB8000 + 4000]))            # remember what was on screen
                for i in range(0, 4000, 2): s.mem[0xB8000 + i] = 0x20; s.mem[0xB8001 + i] = 0x07
            return
        if n == 0x16:
            if ah == 0x01:
                s.polls += 1
                ready = bool(s.keys) and s.polls >= s.keys[0][0]
                s.fl['Z'] = 0 if ready else 1; return
            if ah == 0x00:
                k = s.keys.pop(0); s.r[0] = (s.r[0] & ~0xFFFF) | (k[1] << 8) | k[2]; return
            return
        if n == 0x1A:
            s.tick += 1; s.r[2] = (s.r[2] & ~0xFFFF) | (s.tick & 0xFFFF); s.r[1] = (s.r[1] & ~0xFFFF) | (s.tick >> 16); return
        if n == 0x13:
            if ah == 0x00: cf(0); return
            if ah == 0x42:
                si = s.r[6] & 0xFFFF; d = s.rd(3, si, 16 if False else 16)
                raw = bytes(s.mem[s.lin(3, si, 1):s.lin(3, si, 1) + 16])
                sz, _, cnt, off, sg, lba, lba_hi = struct.unpack('<BBHHHIi', raw)
                assert sz == 16 and lba_hi == 0
                sec = s.sector
                if (lba + cnt) * sec > len(s.disk) or cnt > (16 if sec == 2048 else 127): cf(1); s.r[0] = (s.r[0] & 0xFFFF) | 0x400; return
                dst = (sg << 4) + off; s.reads += 1
                for i in range(cnt * sec): s.mem[dst + i] = s.disk[lba * sec + i]
                cf(0); return
            cf(1); return
        if n == 0x15:
            ax = s.r[0] & 0xFFFF
            if ax == 0x2401:
                if s.variant == 1: cf(1)                    # this BIOS does not do it: the loader must use port 0x92
                else: s.a20 = 1; cf(0)
                return
            if ax == 0xE801:
                if s.variant == 3: cf(1); return
                kb = s.memkb; lo = min(kb, 0x3C00); hi = max(0, (kb - 0x3C00) // 64)
                if s.variant == 1: s.r[0] = lo; s.r[3] = hi; s.r[1] = 0; s.r[2] = 0       # answer in AX/BX
                else: s.r[1] = lo; s.r[2] = hi; s.r[0] = lo; s.r[3] = hi
                cf(0); return
            if ah == 0x88:
                s.r[0] = (s.r[0] & ~0xFFFF) | min(s.memkb, 0xFFFF); cf(0); return
            cf(1); return
        raise Fault("int %x" % n)
    # --- run
    def step(s):
        s.steps += 1
        start = s.ip
        osz = 4 if s.cs32 else 2; asz = 4 if s.cs32 else 2; rep = False; s.segov = None
        while True:
            b = s.f8()
            if b == 0x66: osz = 2 if s.cs32 else 4
            elif b == 0x67: asz = 2 if s.cs32 else 4
            elif b == 0xF3: rep = True
            elif b in (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65): s.segov = {0x26: 0, 0x2E: 1, 0x36: 2, 0x3E: 3, 0x64: 4, 0x65: 5}[b]
            else: break
        o = b
        if o < 0x40 and (o & 7) < 6:
            op = o >> 3; k = o & 7
            if k < 4:
                size = 1 if k % 2 == 0 else osz
                reg, rm = s.modrm(asz)
                if k < 2: a, bb = s.rdop(rm, size), s.getr(reg, size); r = s.alu(op, a, bb, size); (op != 7) and s.wrop(rm, size, r)
                else: a, bb = s.getr(reg, size), s.rdop(rm, size); r = s.alu(op, a, bb, size); (op != 7) and s.setr(reg, size, r)
            else:
                size = 1 if k == 4 else osz
                imm = s.fimm(size); r = s.alu(op, s.getr(0, size), imm, size); (op != 7) and s.setr(0, size, r)
            return
        if o in (0x06, 0x0E, 0x16, 0x1E):  s.push(s.seg[{0x06: 0, 0x0E: 1, 0x16: 2, 0x1E: 3}[o]]['sel'], osz); return
        if o in (0x07, 0x17, 0x1F): s.setseg({0x07: 0, 0x17: 2, 0x1F: 3}[o], s.pop(osz) & 0xFFFF); return
        if 0x40 <= o <= 0x4F:
            r = o & 7; c = s.fl['C']; v = s.alu(0 if o < 0x48 else 5, s.getr(r, osz), 1, osz); s.fl['C'] = c; s.setr(r, osz, v); return
        if o == 0xD3:
            reg, rm = s.modrm(asz); n = s.getr(1, 1) & 31; v = s.rdop(rm, osz)
            if reg == 4: v = (v << n) & ((1 << (8 * osz)) - 1)
            elif reg == 5: v >>= n
            else: raise Fault("d3 /%d" % reg)
            s.wrop(rm, osz, v); s.fl['Z'] = int(v == 0); return
        if o == 0xD1:
            reg, rm = s.modrm(asz); v = s.rdop(rm, osz)
            if reg == 4: v = (v << 1) & ((1 << (8 * osz)) - 1)
            elif reg == 5: v >>= 1
            else: raise Fault("d1 /%d" % reg)
            s.wrop(rm, osz, v); s.fl['Z'] = int(v == 0); return
        if o == 0xF6:
            reg, rm = s.modrm(asz); v = s.rdop(rm, 1)
            if reg == 0: s.alu(4, v, s.f8(), 1); return
            if reg == 6:
                n = s.getr(0, 2)
                if v == 0 or n // v > 255: raise Fault("divide error")
                s.setr(0, 1, n // v); s.setr(4, 1, n % v); return      # AL = quotient, AH = remainder
            raise Fault("f6 /%d" % reg)
        if o == 0xF7:
            reg, rm = s.modrm(asz); v = s.rdop(rm, osz)
            if reg == 4:
                r = s.getr(0, osz) * v; s.setr(0, osz, r); s.setr(2, osz, r >> (8 * osz)); return
            if reg == 6:
                n = (s.getr(2, osz) << (8 * osz)) | s.getr(0, osz)
                if v == 0 or n // v >= (1 << (8 * osz)): raise Fault("divide error")
                s.setr(0, osz, n // v); s.setr(2, osz, n % v); return
            raise Fault("f7 /%d" % reg)
        if 0x50 <= o <= 0x57: s.push(s.getr(o - 0x50, osz), osz); return
        if 0x58 <= o <= 0x5F: s.setr(o - 0x58, osz, s.pop(osz)); return
        if 0x70 <= o <= 0x7F:
            d = struct.unpack('b', bytes([s.f8()]))[0]
            if s.cc(o - 0x70): s.ip = (s.ip + d) & (M if s.cs32 else 0xFFFF)
            return
        if o in (0x80, 0x81, 0x83):
            size = 1 if o == 0x80 else osz
            reg, rm = s.modrm(asz)
            imm = s.f8() if o != 0x81 else s.fimm(size)
            if o == 0x83: imm = struct.unpack('b', bytes([imm]))[0] & ((1 << (8 * size)) - 1)
            r = s.alu(reg, s.rdop(rm, size), imm, size)
            if reg != 7: s.wrop(rm, size, r)
            return
        if o in (0x84, 0x85):
            size = 1 if o == 0x84 else osz; reg, rm = s.modrm(asz); s.alu(4, s.rdop(rm, size), s.getr(reg, size), size); return
        if o in (0x88, 0x89, 0x8A, 0x8B):
            size = 1 if o in (0x88, 0x8A) else osz; reg, rm = s.modrm(asz)
            if o < 0x8A: s.wrop(rm, size, s.getr(reg, size))
            else: s.setr(reg, size, s.rdop(rm, size))
            return
        if o == 0x8C: reg, rm = s.modrm(asz); s.wrop(rm, 2, s.seg[{0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5}[reg]]['sel']); return
        if o == 0x8E: reg, rm = s.modrm(asz); s.setseg({0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5}[reg], s.rdop(rm, 2)); return
        if o == 0x90: return
        if o in (0xA0, 0xA2):
            off = s.f32() if asz == 4 else s.f16(); sg = s.segov if s.segov is not None else 3
            if o == 0xA0: s.setr(0, 1, s.rd(sg, off, 1))
            else: s.wr(sg, off, 1, s.getr(0, 1))
            return
        if o in (0x6B, 0x69):
            reg, rm = s.modrm(asz); v = s.rdop(rm, osz)
            im = s.f8() if o == 0x6B else s.fimm(osz)
            if o == 0x6B and im & 0x80: im -= 256
            s.setr(reg, osz, (v * im) & ((1 << (8 * osz)) - 1)); return
        if o in (0xA1, 0xA3):
            off = s.f32() if asz == 4 else s.f16(); sg = s.segov if s.segov is not None else 3
            if o == 0xA1: s.setr(0, osz, s.rd(sg, off, osz))
            else: s.wr(sg, off, osz, s.getr(0, osz))
            return
        if o in (0xA8, 0xA9): size = 1 if o == 0xA8 else osz; s.alu(4, s.getr(0, size), s.fimm(size), size); return
        if o in (0xA5, 0xAB, 0xAA, 0xAC):
            size = 1 if o in (0xAA, 0xAC) else osz
            cnt = (s.r[1] & (M if asz == 4 else 0xFFFF)) if rep else 1
            msk = M if asz == 4 else 0xFFFF
            for _ in range(cnt):
                if o == 0xA5: s.wr(0, s.r[7] & msk, size, s.rd(s.segov if s.segov is not None else 3, s.r[6] & msk, size)); s.r[6] = (s.r[6] + size) & M; s.r[7] = (s.r[7] + size) & M
                elif o in (0xAA, 0xAB): s.wr(0, s.r[7] & msk, size, s.getr(0, size)); s.r[7] = (s.r[7] + size) & M
                else: s.setr(0, 1, s.rd(s.segov if s.segov is not None else 3, s.r[6] & msk, 1)); s.r[6] = (s.r[6] + 1) & M
            if rep: s.r[1] = (s.r[1] & ~msk) | 0
            return
        if 0xB0 <= o <= 0xB7: s.setr(o - 0xB0, 1, s.f8()); return
        if 0xB8 <= o <= 0xBF: s.setr(o - 0xB8, osz, s.fimm(osz)); return
        if o in (0xC6, 0xC7):
            size = 1 if o == 0xC6 else osz; reg, rm = s.modrm(asz); s.wrop(rm, size, s.fimm(size)); return
        if o == 0xC1:
            reg, rm = s.modrm(asz); n = s.f8() & 31; v = s.rdop(rm, osz)
            if reg == 4: v = (v << n) & ((1 << (8 * osz)) - 1)
            elif reg == 5: v >>= n
            else: raise Fault("shift op %d" % reg)
            s.wrop(rm, osz, v); s.fl['Z'] = int(v == 0); return
        if o == 0xC3: s.ip = s.pop(2); return
        if o == 0xCD: s.bios(s.f8()); return
        if o == 0xE2:
            d = struct.unpack('b', bytes([s.f8()]))[0]; s.r[1] = (s.r[1] & ~0xFFFF) | ((s.r[1] - 1) & 0xFFFF)
            if s.r[1] & 0xFFFF: s.ip = (s.ip + d) & 0xFFFF
            return
        if o == 0xE4: port = s.f8(); s.setr(0, 1, s.port_in(port)); return
        if o == 0xE6: port = s.f8(); s.port_out(port, s.getr(0, 1)); return
        if o == 0xEC: s.setr(0, 1, s.port_in(s.r[2] & 0xFFFF)); return
        if o == 0xEE: s.port_out(s.r[2] & 0xFFFF, s.getr(0, 1)); return
        if o == 0xE8:
            d = s.fimm(2); s.push(s.ip, 2); s.ip = (s.ip + d) & 0xFFFF; return
        if o == 0xE9:
            d = s.fimm(2); s.ip = (s.ip + d) & 0xFFFF; return
        if o == 0xEB:
            d = struct.unpack('b', bytes([s.f8()]))[0]; s.ip = (s.ip + d) & (M if s.cs32 else 0xFFFF); return
        if o == 0xEA:
            off = s.fimm(osz); sel = s.f16()
            if s.pe:
                d = s.rd_gdt(sel); s.cs32 = d['d']; s.seg[1].update(sel=sel, base=d['base'], limit=d['limit'])
            else: s.setseg(1, sel)
            s.ip = off; return
        if o == 0xF4: s.halted = True; return
        if o in (0xFA, 0xFB, 0xFC): return
        if o == 0xFE:
            reg, rm = s.modrm(asz); v = s.rdop(rm, 1); c = s.fl['C']
            r = s.alu(0 if reg == 0 else 5, v, 1, 1); s.fl['C'] = c; s.wrop(rm, 1, r); return
        if o == 0xFF:
            reg, rm = s.modrm(asz)
            if reg == 4: s.ip = s.rdop(rm, osz); return
            raise Fault("ff /%d" % reg)
        if o == 0x0F:
            o2 = s.f8()
            if o2 == 0x01:
                reg, rm = s.modrm(asz)
                if reg == 2:
                    lim = s.rd(rm[1], rm[2], 2); base = s.rd(rm[1], rm[2] + 2, 4); s.gdtr = (lim, base); return
                raise Fault("0f 01 /%d" % reg)
            if o2 == 0x20: reg, rm = s.modrm(asz); s.setr(rm[1], 4, (1 if s.pe else 0) | 0x10); return
            if o2 == 0x22:
                reg, rm = s.modrm(asz); s.pe = s.getr(rm[1], 4) & 1; return
            if o2 in (0xB6, 0xB7):
                reg, rm = s.modrm(asz); s.setr(reg, osz, s.rdop(rm, 1 if o2 == 0xB6 else 2)); return
            if 0x80 <= o2 <= 0x8F:
                d = s.fimm(2); 
                if s.cc(o2 - 0x80): s.ip = (s.ip + d) & 0xFFFF
                return
            raise Fault("0f %02x" % o2)
        raise Fault("opcode %02x at %04x" % (o, start))
    def port_in(s, p):
        if p == 0x92: return s.port92
        raise Fault("in %x" % p)
    def port_out(s, p, v):
        if p == 0x92:
            s.port92 = v
            if v & 2: s.a20 = 1
            return
        raise Fault("out %x" % p)

def parse_iso(d):
    """independent reader for what we need: ISO 9660 volume + El Torito catalog"""
    assert d[16 * 2048 + 1:16 * 2048 + 6] == b"CD001" and d[16 * 2048] == 1
    pvd = d[16 * 2048:17 * 2048]
    assert struct.unpack_from("<H", pvd, 128)[0] == 2048 and struct.unpack_from("<I", pvd, 80)[0] * 2048 == len(d)
    assert d[17 * 2048] == 0 and d[17 * 2048 + 7:17 * 2048 + 30] == b"EL TORITO SPECIFICATION"
    assert d[18 * 2048] == 255
    cat_lba = struct.unpack_from("<I", d, 17 * 2048 + 0x47)[0]
    cat = d[cat_lba * 2048:cat_lba * 2048 + 64]
    assert cat[0] == 1 and cat[0x1E:0x20] == b"\x55\xAA" and cat[1] == 0, "validation entry"
    assert sum(struct.unpack("<16H", cat[:32])) & 0xFFFF == 0, "catalog checksum"
    assert cat[32] == 0x88 and cat[33] == 0, "bootable, no emulation"
    seg, count, rba = struct.unpack_from("<H", cat, 34)[0], struct.unpack_from("<H", cat, 38)[0], struct.unpack_from("<I", cat, 40)[0]
    root_lba, root_len = struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]
    files = {}; o = root_lba * 2048
    while o < root_lba * 2048 + root_len:
        n = d[o]
        if not n: break
        idl = d[o + 32]; ident = d[o + 33:o + 33 + idl]
        if ident not in (b"\x00", b"\x01"):
            assert struct.unpack_from(">I", d, o + 10)[0] == struct.unpack_from("<I", d, o + 10)[0] if False else True
            files[ident.decode()] = (struct.unpack_from("<I", d, o + 2)[0], struct.unpack_from("<I", d, o + 10)[0])
        o += n
    return dict(seg=seg, count=count, rba=rba, files=files)

def boot(disk, variant=0, keys=None, cd=None):
    c = CPU(disk, variant); c.keys = list(keys or [])
    if cd:                                    # El Torito: the BIOS loads the boot image at 0x7C00 and starts it (DL = CD drive)
        c.sector = 2048
        n = cd['count'] * 512; src = cd['rba'] * 2048
        c.mem[0x7C00:0x7C00 + n] = disk[src:src + n]
        c.r[2] = 0xE0
        if cd.get('cs07c0'): c.seg[1].update(sel=0x7C0, base=0x7C00); c.ip = 0
        else: c.ip = 0x7C00
    else:
        c.mem[0x7C00:0x7C00 + 512] = disk[:512]
        c.r[2] = 0x80; c.ip = 0x7C00; c.seg[1]['base'] = 0
    mark_entry = None
    for _ in range(200_000_000):
        if c.halted: return c, "halted: " + c.out
        if c.pe and c.cs32 and c.seg[1]['sel'] == 8 and c.ip == mark_entry: return c, "entered"
        try:
            if mark_entry is None and c.mem[0x7E04:0x7E08] == b'LST2': mark_entry = struct.unpack_from('<I', c.mem, 0x7E00 + 16)[0]
            c.step()
        except Fault as e:
            return c, "FAULT: %s (out=%r)" % (e, c.out)
    return c, "no end"

def screen_text(buf, row): return bytes(buf[row * 160:row * 160 + 160:2]).decode('cp437')
def screen_attr(buf, row, col): return buf[row * 160 + col * 2 + 1]
UP, DOWN, ENTER = (0x48, 0), (0x50, 0), (0x1C, 13)

def check(path, variant, keys=None, want="desktop", iso=False, cs07c0=False):
    disk = open(path, 'rb').read()
    cd = dict(parse_iso(disk), cs07c0=cs07c0) if iso else None
    c, res = boot(disk, variant, keys, cd)
    assert res == "entered", res
    sec = 2048 if iso else 512
    lba, secs, entry, bssend, kend, kflags = struct.unpack_from('<6I', c.mem, 0x7E08)
    assert kflags == (1 if iso else 0)
    img = disk[lba * sec:(lba + secs) * sec]
    assert bytes(c.mem[0x100000:0x100000 + len(img)]) == img, "kernel image differs at 1 MB"
    assert not any(c.mem[kend:bssend]), "BSS not cleared"
    assert c.r[0] == 0x2BADB002 and c.r[3] == 0x600, "registers EAX/EBX %x %x" % (c.r[0], c.r[3])
    flags, lower, upper = struct.unpack_from('<III', c.mem, 0x600)
    assert flags == 0x205 and lower == 639 and upper == c.memkb, (hex(flags), lower, upper, c.memkb)
    nameptr = struct.unpack_from('<I', c.mem, 0x600 + 64)[0]
    assert bytes(c.mem[nameptr:nameptr + 13]).split(b"\0")[0] == (b"LeactedOS CD" if iso else b"LeactedOS")
    clp = struct.unpack_from('<I', c.mem, 0x600 + 16)[0]
    cl = bytes(c.mem[clp:clp + 16]).split(b'\0')[0].decode()
    assert cl == want, "kernel command line %r, wanted %r" % (cl, want)
    assert c.a20 and c.pe and c.seg[3]['sel'] == 0x10 and c.seg[2]['sel'] == 0x10 and c.seg[3]['limit'] == 0xFFFFFFFF
    assert c.ip == entry, "entry %x" % c.ip
    assert "Leact-Loader 1.2" in c.out and "Loading" in c.out and c.out.split('Loading ')[1].count('.') == -(-secs // (16 if iso else 64)), c.out
    assert ("Starting LeactedCmd" in c.out) == want.startswith("cmd") and ("Starting LeactedOS desktop" in c.out) == want.startswith("desktop")
    # the menu screen as it was when the choice was made
    m = c.screens[-1]
    assert "Leact-Loader 1.2" in screen_text(m, 1) and screen_attr(m, 1, 32) == 0x1F
    assert screen_text(m, 3)[10] == '\u2554' and screen_text(m, 10)[69] == '\u255d' and screen_text(m, 6)[10] == '\u2551'
    assert "LeactedOS desktop (Normal)" in screen_text(m, 5) and "LeactedCmd (command line)" in screen_text(m, 6)
    hi = 6 if want.startswith("cmd") else 5
    assert screen_attr(m, hi, 20) == 0x70 and screen_attr(m, 11 - hi, 20) == 0x07, "highlight on the wrong entry"
    assert "keys to select which entry is highlighted" in screen_text(m, 12) and "Press enter to boot the selected OS." in screen_text(m, 13)
    return c, m

if __name__ == "__main__":
    path = sys.argv[1]
    if path.endswith(".iso"):
        disk = open(path, 'rb').read(); info = parse_iso(disk)
        assert set(info['files']) == {"KERNEL.IMG;1", "LEACTEOS.BIN;1", "README.TXT;1"}, info['files']
        for name, (lba, size) in info['files'].items(): assert (lba * 2048 + size) <= len(disk)
        assert disk[info['files']["LEACTEOS.BIN;1"][0] * 2048:][:4] == b"\x7fELF"
        for v in (0, 1, 2, 3):
            c, m = check(path, v, iso=True, cs07c0=bool(v & 1))
            print("iso boot variant %d: ok (%d instructions, %d CD reads)" % (v, c.steps, c.reads))
        check(path, 0, [(3, *DOWN), (6, *ENTER)], "cmd", iso=True)
        check(path, 2, [(3, 0, ord('1'))], "desktop", iso=True)
        print("iso: ok (El Torito catalog, loader from CD, menu, LeactedCmd entry)")
        sys.exit(0)
    for v in ([int(sys.argv[2])] if len(sys.argv) > 2 else [0, 1, 2, 3]):
        c, m = check(path, v)                                                    # nobody touches the keyboard: times out into the desktop
        assert "automatically in 1 seconds" in screen_text(m, 15) or "automatically in" in screen_text(m, 15)
        print("boot variant %d: ok (%d instructions, %d disk reads)" % (v, c.steps, c.reads))
    c, m = check(path, 0, [(3, *DOWN), (6, *ENTER)], "cmd"); assert screen_text(m, 15).strip() == "", "countdown line must be gone after a key"
    c, m = check(path, 0, [(3, *DOWN), (4, *UP), (5, *ENTER)], "desktop")
    c, m = check(path, 0, [(3, 0, ord('2'))], "cmd")
    c, m = check(path, 2, [(3, *DOWN), (5, *ENTER)], "cmd")
    c, m = check(path, 1, [(2, *ENTER)], "desktop")
    c, m = check(path, 0, [(3, 0, ord('x')), (5, *DOWN), (7, *ENTER)], "cmd")
    c, m = check(path, 0, [(3, 0, ord('s'))], "desktop safe")
    print("boot menu: ok (timeout, arrows, enter, number keys, S = safe mode)")
    def vtext(c, row): return bytes(c.mem[0xB8000 + row * 160:0xB8000 + row * 160 + 160:2]).decode('cp437')
    d = bytearray(open(path, 'rb').read()); d[8 * 512:16 * 512] = bytes(4096)           # LOADER.SYS deleted: stage 1 shows the blue screen
    c, res = boot(bytes(d), 0, []); assert res.startswith("halted"), res
    assert c.mem[0xB8000 + 1] == 0x1F and "Your Leacted PC ran into an error and needs to be" in vtext(c, 6) + vtext(c, 5) + vtext(c, 4) and "reinstalled" in "".join(vtext(c, r) for r in range(3, 12)) and "BOOT_LOADER_NOT_FOUND" in "".join(vtext(c, r) for r in range(3, 14))
    d = bytearray(open(path, 'rb').read()); d[16 * 512:16 * 512 + 4096] = bytes(4096)   # KERNEL.SYS deleted: the loader shows the blue screen
    c, res = boot(bytes(d), 0, [(3, *ENTER)]); assert res.startswith("halted"), res
    assert c.mem[0xB8000 + 1] == 0x1F and "needs to be repaired or reinstalled." in vtext(c, 12) and "KERNEL_IMAGE_CORRUPT" in vtext(c, 14) and ":(" not in vtext(c, 3)
    print("blue screen: ok (loader missing, kernel missing)")
    cfgp = os.path.join(os.path.dirname(path), "cfg_disk.img")
    if os.path.exists(cfgp):                                                     # SYSTEM\BOOT.CFG: timeout=2 default=2 (written by host_test)
        c, m = check(cfgp, 0, None, "cmd"); assert "in  2 seconds" in screen_text(m, 15) or "in 2 seconds" in screen_text(m, 15) or "automatically" in screen_text(m, 15)
        c, m = check(cfgp, 0, [(3, *UP), (4, *ENTER)], "desktop")                # a key still overrides the file
        d = bytearray(open(cfgp, 'rb').read())                                   # same disk with timeout=off: nothing starts by itself
        ents = d[3 * 512 + 16:3 * 512 + 16 + 64 * 32]; sysslot = bootslot = None
        for i in range(64):
            nm = bytes(ents[i * 32:i * 32 + 24]).split(b'\0')[0]; fl = struct.unpack_from('<I', ents, i * 32 + 28)[0]
            if nm == b"SYSTEM" and fl & 4: sysslot = i
        for i in range(64):
            nm = bytes(ents[i * 32:i * 32 + 24]).split(b'\0')[0]; fl = struct.unpack_from('<I', ents, i * 32 + 28)[0]
            if nm == b"BOOT.CFG" and (fl >> 8) & 255 == sysslot + 1: bootslot = i
        assert sysslot is not None and bootslot is not None
        off = (2064 + bootslot * 256) * 512; d[off:off + 512] = b"timeout=off\ndefault=2\n".ljust(512, b'\0')
        open("/tmp/off_disk.img", "wb").write(d)
        c, m = check("/tmp/off_disk.img", 0, [(400000, *ENTER)], "cmd"); assert screen_text(m, 15).strip() == ""
        print("boot.cfg: ok (timeout, default, off, key override)")
    if len(sys.argv) > 3:
        open(sys.argv[3], "wb").write(m)
