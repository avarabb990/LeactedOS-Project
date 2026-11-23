/*
 * LeactedOS XP Edition 2.0
 * Rebuilt from the original LeactedOS XP kernel binary, with fixes and upgrades.
 *
 * Hardware: x86 (32-bit), multiboot, Bochs/QEMU "std" VGA (VBE dispi), PS/2
 * keyboard + mouse, ATA PIO primary master, PIT timer, CMOS RTC, PC speaker.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t i32;

#ifdef HOST
#include <string.h>
#else
/* freestanding libc bits the compiler may call for */
#define NOPAT __attribute__((optimize("no-tree-loop-distribute-patterns")))
NOPAT void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
NOPAT void *memcpy(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
NOPAT void *memmove(void *d, const void *s, size_t n) {
    u8 *p = d; const u8 *q = s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}
NOPAT int memcmp(const void *a, const void *b, size_t n) {
    const u8 *p = a, *q = b;
    while (n--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}
NOPAT size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
#endif

#include "font.h"
#include "sintab.h"

/* ------------------------------------------------------------------ */
/* Port I/O                                                            */
/* ------------------------------------------------------------------ */
#ifndef HOST
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
#else
static inline void outb(u16 p, u8 v) { (void)p; (void)v; }
static inline u8 inb(u16 p) { (void)p; return 0; }
static inline void outw(u16 p, u16 v) { (void)p; (void)v; }
static inline u16 inw(u16 p) { (void)p; return 0; }
static inline void outl(u16 p, u32 v) { (void)p; (void)v; }
static inline u32 inl(u16 p) { (void)p; return 0; }
#endif

/* ------------------------------------------------------------------ */
/* Small string / format helpers                                       */
/* ------------------------------------------------------------------ */
static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
static void kstrcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static char kupper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static char klower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int kvsnprintf(char *b, int n, const char *f, va_list ap) {
    int o = 0;
#define PUT(c) do { if (o < n - 1) b[o] = (c); o++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int zero = 0, wd = 0;
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') { wd = wd * 10 + (*f - '0'); f++; }
        char tmp[16]; int tl = 0;
        switch (*f) {
        case 'd': case 'u': case 'x': {
            u32 u; int neg = 0;
            if (*f == 'd') { i32 v = va_arg(ap, i32); if (v < 0) { neg = 1; u = (u32)0 - (u32)v; } else u = (u32)v; }
            else u = va_arg(ap, u32);
            u32 base = (*f == 'x') ? 16 : 10;
            if (u == 0) tmp[tl++] = '0';
            while (u) { tmp[tl++] = "0123456789abcdef"[u % base]; u /= base; }
            if (neg) tmp[tl++] = '-';
            for (int i = tl; i < wd; i++) PUT(zero ? '0' : ' ');
            while (tl) PUT(tmp[--tl]);
            break;
        }
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
            int l = 0; while (s[l]) l++;
            for (int i = l; i < wd; i++) PUT(' ');
            while (*s) { PUT(*s); s++; } break; }
        case 'c': PUT((char)va_arg(ap, int)); break;
        case '%': PUT('%'); break;
        default: PUT('?'); break;
        }
    }
    if (n > 0) b[o < n ? o : n - 1] = 0;
    return o;
#undef PUT
}
static int ksnprintf(char *b, int n, const char *f, ...) {
    va_list ap; va_start(ap, f);
    int r = kvsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */
#define W 640
#define H 480
#define TB_H 28          /* taskbar height */

static u32 back[W * H];  /* double buffer */
static u32 bgimg[W * H]; /* pre-rendered wallpaper */
static volatile u32 *lfb;
static volatile u32 ticks;          /* 100 Hz */
static u32 mem_kb;
static int boot_via_multiboot;

#define RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

/* ------------------------------------------------------------------ */
/* Interrupts: PIT timer so we can sleep with HLT instead of spinning  */
/* ------------------------------------------------------------------ */
struct idt_entry { u16 lo; u16 sel; u8 zero; u8 flags; u16 hi; } __attribute__((packed));
struct idt_ptr { u16 limit; u32 base; } __attribute__((packed));
static struct idt_entry idt[256];
extern void irq0_stub(void);

void timer_irq(void) {
    ticks++;
    outb(0x20, 0x20);
}

static void idt_set(int n, void (*fn)(void)) {
    u16 cs = 0x08;
#ifndef HOST
    __asm__ volatile("mov %%cs,%0" : "=r"(cs));
#endif
    u32 a = (u32)(uintptr_t)fn;
    idt[n].lo = a & 0xFFFF; idt[n].hi = a >> 16;
    idt[n].sel = cs; idt[n].zero = 0; idt[n].flags = 0x8E;
}

static void irq_init(void) {
#ifndef HOST
    /* remap PIC: IRQ0-7 -> 0x20.., IRQ8-15 -> 0x28.. ; only IRQ0 (timer) enabled.
       Keyboard and mouse are polled, so their IRQs stay masked. */
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xFE); outb(0xA1, 0xFF);
    /* PIT channel 0, 100 Hz */
    outb(0x43, 0x36);
    outb(0x40, 11932 & 0xFF); outb(0x40, 11932 >> 8);
    idt_set(0x20, irq0_stub);
    struct idt_ptr p = { sizeof(idt) - 1, (u32)(uintptr_t)idt };
    __asm__ volatile("lidt %0" ::"m"(p));
    __asm__ volatile("sti");
#endif
}

static void wait_tick(void) {
#ifndef HOST
    __asm__ volatile("hlt");
#endif
}
static void sleep_ticks(u32 n) {
#ifdef HOST
    (void)n;
#else
    u32 t = ticks + n;
    while ((i32)(ticks - t) < 0) wait_tick();
#endif
}

/* ------------------------------------------------------------------ */
/* PC speaker                                                          */
/* ------------------------------------------------------------------ */
static void beep(u32 hz, u32 ms) {
    if (hz < 20) hz = 20;
    if (hz > 20000) hz = 20000;
    u32 d = 1193180 / hz;
    outb(0x43, 0xB6);
    outb(0x42, d & 0xFF); outb(0x42, (d >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 3);
    sleep_ticks(ms / 10 + 1);
    outb(0x61, inb(0x61) & ~3);
}

/* ------------------------------------------------------------------ */
/* CMOS real-time clock                                                */
/* ------------------------------------------------------------------ */
struct rtc { int h, m, s, d, mo, y; };
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static void rtc_read(struct rtc *t) {
#ifdef HOST
    t->h = 17; t->m = 32; t->s = 5; t->d = 18; t->mo = 11; t->y = 2026;
    return;
#else
    int guard = 100000;
    while ((cmos(0x0A) & 0x80) && guard--) ;
    u8 s = cmos(0), m = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9), b = cmos(0x0B);
    int pm = h & 0x80; h &= 0x7F;
    if (!(b & 4)) {
        s = (s & 15) + (s >> 4) * 10; m = (m & 15) + (m >> 4) * 10; h = (h & 15) + (h >> 4) * 10;
        d = (d & 15) + (d >> 4) * 10; mo = (mo & 15) + (mo >> 4) * 10; y = (y & 15) + (y >> 4) * 10;
    }
    if (!(b & 2) && pm) { h = (h % 12) + 12; }
    t->s = s; t->m = m; t->h = h % 24; t->d = d; t->mo = mo; t->y = 2000 + y;
#endif
}

/* ------------------------------------------------------------------ */
/* Graphics                                                            */
/* ------------------------------------------------------------------ */
static int clx0 = 0, cly0 = 0, clx1 = W, cly1 = H;
static void clip_set(int x, int y, int w, int h) {
    clx0 = x < 0 ? 0 : x; cly0 = y < 0 ? 0 : y;
    clx1 = x + w > W ? W : x + w; cly1 = y + h > H ? H : y + h;
}
static void clip_reset(void) { clx0 = 0; cly0 = 0; clx1 = W; cly1 = H; }

static inline void pset(int x, int y, u32 c) {
    if (x >= clx0 && x < clx1 && y >= cly0 && y < cly1) back[y * W + x] = c;
}
static void rect(int x, int y, int w, int h, u32 c) {
    int x0 = x < clx0 ? clx0 : x, y0 = y < cly0 ? cly0 : y;
    int x1 = x + w > clx1 ? clx1 : x + w, y1 = y + h > cly1 ? cly1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *row = &back[j * W];
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}
static void frame(int x, int y, int w, int h, u32 c) {
    rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c);
}
static u32 mix(u32 a, u32 b, int t, int max) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int r = ar + (br - ar) * t / max, g = ag + (bg - ag) * t / max, bl = ab + (bb - ab) * t / max;
    return RGB(r, g, bl);
}
static void gradient_v(int x, int y, int w, int h, u32 c1, u32 c2) {
    for (int j = 0; j < h; j++) rect(x, y + j, w, 1, mix(c1, c2, j, h > 1 ? h - 1 : 1));
}
static void line(int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        pset(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static int isqrt(int n) { int r = 0; while ((r + 1) * (r + 1) <= n) r++; return r; }
static void fill_circle(int cx, int cy, int r, u32 c) {
    for (int dy = -r; dy <= r; dy++) { int dx = isqrt(r * r - dy * dy); rect(cx - dx, cy + dy, 2 * dx + 1, 1, c); }
}
static void draw_char_s(int x, int y, char ch, u32 c, int s) {
    if ((u8)ch < 32 || (u8)ch > 127) ch = '?';
    const u8 *g = font8[ch - 32];
    for (int r = 0; r < 8; r++)
        for (int b = 0; b < 8; b++)
            if (g[r] & (0x80 >> b)) {
                if (s == 1) pset(x + b, y + r, c); else rect(x + b * s, y + r * s, s, s, c);
            }
}
static void draw_char(int x, int y, char ch, u32 c) { draw_char_s(x, y, ch, c, 1); }
static int draw_str_s(int x, int y, const char *s, u32 c, int sc) {
    for (; *s; s++, x += 8 * sc) draw_char_s(x, y, *s, c, sc);
    return x;
}
static int draw_str(int x, int y, const char *s, u32 c) { return draw_str_s(x, y, s, c, 1); }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void draw_str_c(int cx, int y, const char *s, u32 c) { draw_str(cx - slen(s) * 4, y, s, c); }
static void drawf(int x, int y, u32 c, const char *f, ...) {
    char b[96]; va_list ap; va_start(ap, f); kvsnprintf(b, sizeof b, f, ap); va_end(ap);
    draw_str(x, y, b, c);
}

/* XP-ish palette */
#define C_FACE   RGB(236, 233, 216)
#define C_LIGHT  RGB(255, 255, 255)
#define C_DARK   RGB(113, 111, 100)
#define C_BLACK  RGB(0, 0, 0)
#define C_TITLE1 RGB(0, 88, 230)
#define C_TITLE2 RGB(58, 129, 243)
#define C_FRAME  RGB(8, 49, 217)
#define C_TEXT   RGB(0, 0, 0)

static void bevel(int x, int y, int w, int h, int pressed) {
    rect(x, y, w, h, C_FACE);
    u32 a = pressed ? C_DARK : C_LIGHT, b = pressed ? C_LIGHT : C_DARK;
    rect(x, y, w, 1, a); rect(x, y, 1, h, a);
    rect(x, y + h - 1, w, 1, b); rect(x + w - 1, y, 1, h, b);
}
static void button(int x, int y, int w, int h, const char *label, int pressed) {
    bevel(x, y, w, h, pressed);
    draw_str(x + (w - slen(label) * 8) / 2 + pressed, y + (h - 8) / 2 + pressed, label, C_TEXT);
}
static int inrect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void flip(void) {
#ifndef HOST
    u32 n = W * H;
    const u32 *src = back; volatile u32 *dst = lfb;
    __asm__ volatile("rep movsl" : "+S"(src), "+D"(dst), "+c"(n) : : "memory");
#endif
}

/* ------------------------------------------------------------------ */
/* Video mode (Bochs / QEMU std VGA, VirtualBox VBoxVGA)               */
/* ------------------------------------------------------------------ */
static void vbe_write(u16 idx, u16 val) { outw(0x1CE, idx); outw(0x1CF, val); }
static u16 vbe_read(u16 idx) { outw(0x1CE, idx); return inw(0x1CF); }

static u32 pci_read(int bus, int dev, int fn, int off) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}
static void text_fatal(const char *msg) {
#ifndef HOST
    volatile u16 *v = (volatile u16 *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) v[i] = 0x1F20;
    for (int i = 0; msg[i]; i++) v[i] = 0x1F00 | (u8)msg[i];
    for (;;) __asm__ volatile("cli; hlt");
#else
    (void)msg;
#endif
}
static void video_init(void) {
    u32 base = 0;
    for (int bus = 0; bus < 2 && !base; bus++)
        for (int dev = 0; dev < 32; dev++) {
            u32 id = pci_read(bus, dev, 0, 0);
            if (id == 0x11111234u || id == 0xBEEF80EEu) {
                u32 bar = pci_read(bus, dev, 0, 0x10);
                if (!(bar & 1)) { base = bar & ~0xFu; break; }
            }
        }
    if (!base) {
        /* No PCI match: if the VBE registers answer, assume the classic address. */
        if (vbe_read(0) >= 0xB0C0) base = 0xFD000000u;
        else text_fatal("LeactedOS XP needs a Bochs/QEMU standard VGA adapter (qemu -vga std).");
    }
    lfb = (volatile u32 *)(uintptr_t)base;
    vbe_write(4, 0);          /* disable */
    vbe_write(1, W);          /* xres */
    vbe_write(2, H);          /* yres */
    vbe_write(3, 32);         /* bpp */
    vbe_write(6, W);          /* virt width */
    vbe_write(7, H);          /* virt height */
    vbe_write(8, 0); vbe_write(9, 0);
    vbe_write(4, 0x41);       /* enable + LFB */
}

/* ------------------------------------------------------------------ */
/* PS/2 keyboard + mouse (polled)                                      */
/* ------------------------------------------------------------------ */
#define K_UP    0x100
#define K_DOWN  0x101
#define K_LEFT  0x102
#define K_RIGHT 0x103
#define K_DEL   0x104
#define K_ESC   27

static const char sc_lo[] = "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char sc_hi[] = "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

static int kq[64], kq_h, kq_t;
static int k_shift, k_ctrl, k_caps, k_ext;

static void kq_push(int k) {
    int n = (kq_t + 1) & 63;
    if (n != kq_h) { kq[kq_t] = k; kq_t = n; }
}
static int kq_pop(void) {
    if (kq_h == kq_t) return 0;
    int k = kq[kq_h]; kq_h = (kq_h + 1) & 63;
    return k;
}

static void kbd_scancode(u8 sc) {
    if (sc == 0xE0) { k_ext = 1; return; }
    int rel = sc & 0x80; u8 code = sc & 0x7F;
    int ext = k_ext; k_ext = 0;
    if (ext) {
        if (rel) return;
        switch (code) {
        case 0x48: kq_push(K_UP); break;
        case 0x50: kq_push(K_DOWN); break;
        case 0x4B: kq_push(K_LEFT); break;
        case 0x4D: kq_push(K_RIGHT); break;
        case 0x53: kq_push(K_DEL); break;
        case 0x1D: k_ctrl = 1; break;
        case 0x35: kq_push('/'); break;
        case 0x1C: kq_push('\n'); break;
        }
        return;
    }
    if (code == 0x2A || code == 0x36) { k_shift = !rel; return; }
    if (code == 0x1D) { k_ctrl = !rel; return; }
    if (rel) return;
    if (code == 0x3A) { k_caps = !k_caps; return; }
    if (code >= sizeof(sc_lo) - 1) return;
    char c = (k_shift ? sc_hi : sc_lo)[code];
    if (!c) return;
    if (k_caps) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    if (k_ctrl) {
        char l = klower(c);
        if (l >= 'a' && l <= 'z') { kq_push(l - 'a' + 1); return; }
    }
    kq_push((u8)c);
}

static int mx = W / 2, my = H / 2, mbtn;
static u8 mpkt[3]; static int mcyc;

static void mouse_byte(u8 b) {
    if (mcyc == 0 && !(b & 8)) return;           /* out of sync: wait for a flags byte */
    mpkt[mcyc++] = b;
    if (mcyc < 3) return;
    mcyc = 0;
    mbtn = mpkt[0] & 3;
    if (mpkt[0] & 0xC0) return;                  /* overflow: ignore the motion */
    int dx = mpkt[1], dy = mpkt[2];
    if (mpkt[0] & 0x10) dx -= 256;
    if (mpkt[0] & 0x20) dy -= 256;
    mx += dx; my -= dy;
    if (mx < 0) mx = 0;
    if (mx > W - 1) mx = W - 1;
    if (my < 0) my = 0;
    if (my > H - 1) my = H - 1;
}

static void input_poll(void) {
#ifndef HOST
    for (int n = 0; n < 64; n++) {
        u8 st = inb(0x64);
        if (!(st & 1)) break;
        u8 d = inb(0x60);
        if (st & 0x20) mouse_byte(d); else kbd_scancode(d);
    }
#endif
}

#ifndef HOST
static int ps2_wait_in(void) { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int ps2_wait_out(void) { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return 1; return 0; }
static void ps2_mouse_cmd(u8 c) {
    ps2_wait_in(); outb(0x64, 0xD4);
    ps2_wait_in(); outb(0x60, c);
    if (ps2_wait_out()) inb(0x60);               /* ACK */
}
#endif
static void ps2_init(void) {
#ifndef HOST
    while (inb(0x64) & 1) inb(0x60);             /* drain */
    ps2_wait_in(); outb(0x64, 0xA8);             /* enable aux port */
    ps2_wait_in(); outb(0x64, 0x20);
    u8 cfg = ps2_wait_out() ? inb(0x60) : 0x47;
    cfg &= ~0x03;                                /* IRQs off: we poll */
    cfg &= ~0x30;                                /* clocks on */
    ps2_wait_in(); outb(0x64, 0x60);
    ps2_wait_in(); outb(0x60, cfg);
    ps2_mouse_cmd(0xF6);                         /* defaults */
    ps2_mouse_cmd(0xF4);                         /* streaming on */
    while (inb(0x64) & 1) inb(0x60);
#endif
}

/* ------------------------------------------------------------------ */
/* ATA PIO (primary master, LBA28)                                     */
/* ------------------------------------------------------------------ */
static int disk_present;
static u32 disk_sectors;
static char disk_model[41];
#ifdef HOST
#define FAKE_SECT 4096
static u8 fakedisk[FAKE_SECT * 512];
#endif

#ifndef HOST
static int ata_wait_ready(void) {
    for (int i = 0; i < 1000000; i++) { u8 s = inb(0x1F7); if (!(s & 0x80)) return s; }
    return -1;
}
static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        u8 s = inb(0x1F7);
        if (s & 0x21) return 0;                  /* ERR or DF */
        if (!(s & 0x80) && (s & 0x08)) return 1;
    }
    return 0;
}
#endif

static void ata_identify(void) {
    disk_present = 0; disk_sectors = 0; disk_model[0] = 0;
#ifdef HOST
    disk_present = 1; disk_sectors = FAKE_SECT; kstrcpy(disk_model, "HOST FAKE DISK", sizeof disk_model);
#else
    if (inb(0x1F7) == 0xFF) return;              /* floating bus: no controller */
    outb(0x1F6, 0xA0);
    for (int i = 0; i < 4; i++) inb(0x3F6);
    outb(0x1F2, 0); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    if (inb(0x1F7) == 0) return;
    if (ata_wait_ready() < 0) return;
    if (inb(0x1F4) || inb(0x1F5)) return;        /* ATAPI/SATA signature: not plain ATA */
    if (!ata_wait_drq()) return;
    u16 id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(0x1F0);
    disk_sectors = id[60] | ((u32)id[61] << 16);
    for (int i = 0; i < 20; i++) { disk_model[i * 2] = id[27 + i] >> 8; disk_model[i * 2 + 1] = id[27 + i] & 255; }
    disk_model[40] = 0;
    for (int i = 39; i >= 0 && disk_model[i] == ' '; i--) disk_model[i] = 0;
    disk_present = disk_sectors > 0;
#endif
}

static int ata_read(u32 lba, int count, void *buf) {
    if (!disk_present || lba + count > disk_sectors) return 0;
#ifdef HOST
    memcpy(buf, &fakedisk[lba * 512], count * 512); return 1;
#else
    u16 *p = buf;
    if (ata_wait_ready() < 0) return 0;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, count); outb(0x1F3, lba); outb(0x1F4, lba >> 8); outb(0x1F5, lba >> 16);
    outb(0x1F7, 0x20);
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq()) return 0;
        for (int i = 0; i < 256; i++) *p++ = inw(0x1F0);
    }
    return 1;
#endif
}
static int ata_write(u32 lba, int count, const void *buf) {
    if (!disk_present || lba + count > disk_sectors) return 0;
#ifdef HOST
    memcpy(&fakedisk[lba * 512], buf, count * 512); return 1;
#else
    const u16 *p = buf;
    if (ata_wait_ready() < 0) return 0;
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, count); outb(0x1F3, lba); outb(0x1F4, lba >> 8); outb(0x1F5, lba >> 16);
    outb(0x1F7, 0x30);
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq()) return 0;
        for (int i = 0; i < 256; i++) outw(0x1F0, *p++);
    }
    outb(0x1F7, 0xE7);                           /* flush cache */
    return ata_wait_ready() >= 0;
#endif
}

/* ------------------------------------------------------------------ */
/* LeactedFS                                                           */
/*  sector 0 : MBR with one partition (type 0xDA, non-FS data)         */
/*  sector 1 : superblock "LEACTEDFS" 0x01, total sectors at +0x10     */
/*  sector 2 : directory "LDIR" + 15 x { name[24], size, used }        */
/*  sector 8+: file slots, 8 sectors (4 KB) each                       */
/* ------------------------------------------------------------------ */
#define FS_MAX 15
#define FS_SLOT 4096
#define FS_NAME 24
#define FS_DATA_LBA 8
struct fent { char name[FS_NAME]; u32 size; u32 used; };
static struct fent fsdir[FS_MAX];
static u8 fsdata[FS_MAX][FS_SLOT];
static int fs_on_disk;                           /* LeactedFS volume present */

static void fs_norm(char *d, const char *s) {
    int i = 0;
    for (; s[i] && i < FS_NAME - 1; i++) d[i] = kupper(s[i]);
    d[i] = 0;
}
static int fs_find(const char *name) {
    char n[FS_NAME]; fs_norm(n, name);
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used && !kstrcmp(fsdir[i].name, n)) return i;
    return -1;
}
static int fs_count(void) { int c = 0; for (int i = 0; i < FS_MAX; i++) c += fsdir[i].used ? 1 : 0; return c; }

static int fs_flush_dir(void) {
    if (!fs_on_disk) return 1;
    u8 sec[512]; memset(sec, 0, 512);
    memcpy(sec, "LDIR", 4);
    memcpy(sec + 4, fsdir, sizeof(fsdir));
    return ata_write(2, 1, sec);
}
static int fs_flush_file(int i) {
    if (!fs_on_disk) return 1;
    int secs = (fsdir[i].size + 511) / 512;
    if (secs && !ata_write(FS_DATA_LBA + i * 8, secs, fsdata[i])) return 0;
    return fs_flush_dir();
}
/* returns slot or -1 (full) */
static int fs_create(const char *name) {
    int i = fs_find(name);
    if (i >= 0) return i;
    for (i = 0; i < FS_MAX; i++) if (!fsdir[i].used) {
        memset(&fsdir[i], 0, sizeof fsdir[i]);
        fs_norm(fsdir[i].name, name);
        fsdir[i].used = 1;
        memset(fsdata[i], 0, FS_SLOT);
        return i;
    }
    return -1;
}
/* 0 = ok in RAM+disk, 1 = ok RAM only (no disk), -1 = error */
static int fs_store(int i, const void *data, u32 len) {
    if (len > FS_SLOT) len = FS_SLOT;
    memcpy(fsdata[i], data, len);
    fsdir[i].size = len;
    if (!fs_on_disk) return 1;
    return fs_flush_file(i) ? 0 : -1;
}
static int fs_delete(int i) {
    memset(&fsdir[i], 0, sizeof fsdir[i]);
    if (!fs_on_disk) return 1;
    return fs_flush_dir() ? 0 : -1;
}
static void fs_defaults(void) {
    static const char readme[] =
        "Welcome to LeactedOS XP Edition 2.0!\n"
        "Open Terminal and type help.\n"
        "Files live in RAM until you install\n"
        "a disk with the Installer.\n";
    int i = fs_create("README.TXT");
    if (i >= 0) { memcpy(fsdata[i], readme, sizeof readme - 1); fsdir[i].size = sizeof readme - 1; }
}
static int fs_check(void) {
    u8 sec[512];
    fs_on_disk = 0;
    if (!disk_present || !ata_read(1, 1, sec)) return 0;
    if (memcmp(sec, "LEAC", 4) != 0) return 0;
    fs_on_disk = 1;
    return 1;
}
static void fs_load(void) {
    memset(fsdir, 0, sizeof fsdir);
    if (fs_on_disk) {
        u8 sec[512];
        if (ata_read(2, 1, sec) && !memcmp(sec, "LDIR", 4)) {
            memcpy(fsdir, sec + 4, sizeof fsdir);
            for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) {
                if (fsdir[i].size > FS_SLOT) fsdir[i].size = FS_SLOT;
                fsdir[i].name[FS_NAME - 1] = 0;
                int secs = (fsdir[i].size + 511) / 512;
                if (secs) ata_read(FS_DATA_LBA + i * 8, secs, fsdata[i]);
            }
            return;
        }
        fs_defaults();
        fs_flush_file(0);
        return;
    }
    fs_defaults();
}
static int fs_install(void) {
    if (!disk_present || disk_sectors < FS_DATA_LBA + FS_MAX * 8) return 0;
    u8 sec[512];
    memset(sec, 0, 512);
    /* partition entry: type 0xDA, LBA 8, rest of disk */
    sec[0x1BE] = 0x00; sec[0x1BE + 4] = 0xDA;
    u32 start = 8, len = disk_sectors - 8;
    memcpy(sec + 0x1BE + 8, &start, 4); memcpy(sec + 0x1BE + 12, &len, 4);
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!ata_write(0, 1, sec)) return 0;
    memset(sec, 0, 512);
    memcpy(sec, "LEACTEDFS\x01", 10);
    memcpy(sec + 0x10, &disk_sectors, 4);
    if (!ata_write(1, 1, sec)) return 0;
    fs_on_disk = 1;
    /* keep the files that exist in RAM: write them all out */
    if (!fs_flush_dir()) { fs_on_disk = 0; return 0; }
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used && !fs_flush_file(i)) { fs_on_disk = 0; return 0; }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Power                                                               */
/* ------------------------------------------------------------------ */
static void reboot_now(void) {
#ifndef HOST
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) ;
    outb(0x64, 0xFE);
    struct idt_ptr z = { 0, 0 };
    __asm__ volatile("lidt %0; int3" ::"m"(z));
    for (;;) __asm__ volatile("hlt");
#endif
}
static void power_off(void) {
#ifndef HOST
    outw(0x604, 0x2000); outw(0xB004, 0x2000); outw(0x4004, 0x3400);
    rect(0, 0, W, H, 0);
    draw_str_c(W / 2, H / 2 - 4, "It is now safe to turn off your computer.", RGB(255, 140, 0));
    flip();
    for (;;) __asm__ volatile("cli; hlt");
#endif
}

/* ------------------------------------------------------------------ */
/* Window manager                                                      */
/* ------------------------------------------------------------------ */
enum { A_TERM, A_NOTE, A_PAINT, A_FILES, A_CLOCK, A_ABOUT, A_INSTALL, N_APPS };

struct win { int open, min, x, y, w, h; const char *title; };
static struct win wins[N_APPS] = {
    [A_TERM]    = { 0, 0, 90, 24, 468, 340, "Terminal" },
    [A_NOTE]    = { 0, 0, 150, 50, 420, 300, "Notepad" },
    [A_PAINT]   = { 0, 0, 60, 20, 464, 372, "Paint" },
    [A_FILES]   = { 0, 0, 110, 40, 420, 320, "File Manager" },
    [A_CLOCK]   = { 0, 0, 330, 40, 260, 310, "Clock" },
    [A_ABOUT]   = { 0, 0, 200, 80, 340, 230, "About LeactedOS" },
    [A_INSTALL] = { 0, 0, 140, 70, 380, 250, "LeactedOS Installer" },
};
static const char *app_names[N_APPS] = { "Terminal", "Notepad", "Paint", "File Manager", "Clock", "About", "Installer" };
static int zo[N_APPS], nz;

#define TITLE_H 22
#define CX(a) (wins[a].x + 3)
#define CY(a) (wins[a].y + 3 + TITLE_H)
#define CW(a) (wins[a].w - 6)
#define CH(a) (wins[a].h - 6 - TITLE_H)

static int top_app(void) {
    for (int i = nz - 1; i >= 0; i--) if (!wins[zo[i]].min) return zo[i];
    return -1;
}
static void z_remove(int a) {
    int j = 0;
    for (int i = 0; i < nz; i++) if (zo[i] != a) zo[j++] = zo[i];
    nz = j;
}
static void raise_app(int a) { z_remove(a); zo[nz++] = a; wins[a].min = 0; }

static void app_init(int a);
static void open_app(int a) {
    if (!wins[a].open) { wins[a].open = 1; app_init(a); }
    raise_app(a);
}
static void close_app(int a) { wins[a].open = 0; wins[a].min = 0; z_remove(a); }

/* ------------------------------------------------------------------ */
/* Terminal                                                            */
/* ------------------------------------------------------------------ */
#define TC 56
#define TR 29
#define ILMAX 48
static char tg[TR][TC + 1];
static int tcol, trow;
static char iline[ILMAX + 1]; static int ilen;
static char hist[8][ILMAX + 1]; static int nhist, hpos;
static int is_root;

static void tclear(void) { memset(tg, 0, sizeof tg); tcol = 0; trow = 0; }
static void tnl(void) {
    tcol = 0; trow++;
    if (trow >= TR) { memmove(tg[0], tg[1], (TR - 1) * (TC + 1)); memset(tg[TR - 1], 0, TC + 1); trow = TR - 1; }
}
static void tputc(char c) {
    if (c == '\n') { tnl(); return; }
    if (c == '\r') return;
    if (c == '\t') c = ' ';
    if (tcol >= TC) tnl();
    tg[trow][tcol++] = c; tg[trow][tcol] = 0;
}
static void tputs(const char *s) { while (*s) tputc(*s++); }
static void tprintf(const char *f, ...) {
    char b[160]; va_list ap; va_start(ap, f); kvsnprintf(b, sizeof b, f, ap); va_end(ap); tputs(b);
}
static const char *prompt(void) { return is_root ? "leacted# " : "leacted> "; }

/* Notepad state is needed by the terminal (edit command) */
#define NOTE_MAX 4000
static char nbuf[NOTE_MAX + 1]; static int nlen;
static char nname[FS_NAME] = "UNTITLED.TXT";
static char nmsg[40];
static int ndirty;
static void note_open_file(const char *name) {
    open_app(A_NOTE);
    char n[FS_NAME]; fs_norm(n, name);
    kstrcpy(nname, n, FS_NAME);
    nlen = 0; nbuf[0] = 0; ndirty = 0; nmsg[0] = 0;
    int i = fs_find(n);
    if (i >= 0) { nlen = fsdir[i].size < NOTE_MAX ? fsdir[i].size : NOTE_MAX; memcpy(nbuf, fsdata[i], nlen); nbuf[nlen] = 0; }
}

static int split_args(char *s, char **argv, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        argv[n++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = 0;
    }
    return n;
}
static int parse_int(const char *s, i32 *out) {
    int neg = 0; i32 v = 0;
    if (*s == '-') { neg = 1; s++; }
    if (!*s) return 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return 0; v = v * 10 + (*s - '0'); }
    *out = neg ? -v : v;
    return 1;
}
static void print_store_result(int r) {
    if (r == 0) tputs("Saved to disk.\n");
    else if (r == 1) tputs("Saved (RAM only - run the Installer to keep files).\n");
    else tputs("Disk write failed!\n");
}
static void cmd_ls(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) { tprintf(" %s %u bytes\n", fsdir[i].name, fsdir[i].size); n++; }
    tprintf("%d file(s), %s\n", n, fs_on_disk ? "stored on disk" : "RAM only");
}
static void cmd_echo_redirect(char *line) {
    /* echo TEXT > FILE   or   echo TEXT >> FILE */
    char *gt = line; while (*gt && *gt != '>') gt++;
    if (!*gt) { tputs(line); tputc('\n'); return; }
    int append = gt[1] == '>';
    char *name = gt + (append ? 2 : 1);
    *gt = 0;
    while (*name == ' ') name++;
    char *e = name; while (*e && *e != ' ') e++; *e = 0;
    if (!*name) { tputs("Usage: echo text > file\n"); return; }
    int tl = slen(line); while (tl > 0 && line[tl - 1] == ' ') line[--tl] = 0;
    int i = fs_create(name);
    if (i < 0) { tputs("Disk full (15 files max).\n"); return; }
    char buf[FS_SLOT + 2]; u32 len = 0;
    if (append) { len = fsdir[i].size; memcpy(buf, fsdata[i], len); }
    for (int k = 0; line[k] && len < FS_SLOT - 1; k++) buf[len++] = line[k];
    buf[len++] = '\n';
    print_store_result(fs_store(i, buf, len));
}
static void run_cmd(char *line);
static void cmd_help(void) {
    tputs("Commands:\n");
    tputs(" help clear about ver uptime date mem disk\n");
    tputs(" ls            list files\n");
    tputs(" cat FILE      show a file\n");
    tputs(" edit FILE     open in Notepad\n");
    tputs(" echo T > FILE write text (>> appends)\n");
    tputs(" rm FILE       delete (needs super)\n");
    tputs(" calc A op B   + - * / %\n");
    tputs(" beep [hz]     PC speaker\n");
    tputs(" open APP      term note paint files clock\n");
    tputs("               about install\n");
    tputs(" super         toggle root mode\n");
    tputs(" reboot  shutdown  exit\n");
}
static void run_cmd(char *line) {
    char work[ILMAX + 2]; kstrcpy(work, line, sizeof work);
    char copy[ILMAX + 2]; kstrcpy(copy, line, sizeof copy);
    char *av[6]; int ac = split_args(work, av, 6);
    if (!ac) return;
    for (char *p = av[0]; *p; p++) *p = klower(*p);
    const char *c = av[0];

    if (!kstrcmp(c, "help") || !kstrcmp(c, "?")) cmd_help();
    else if (!kstrcmp(c, "clear") || !kstrcmp(c, "cls")) tclear();
    else if (!kstrcmp(c, "about") || !kstrcmp(c, "ver")) {
        tputs("LeactedOS XP Edition 2.0 (640x480)\nFrom-scratch hobby OS, rebuilt & upgraded.\n");
    }
    else if (!kstrcmp(c, "echo")) {
        char *t = copy + 4; while (*t == ' ') t++;
        cmd_echo_redirect(t);
    }
    else if (!kstrcmp(c, "ls") || !kstrcmp(c, "dir")) cmd_ls();
    else if (!kstrcmp(c, "cat") || !kstrcmp(c, "type")) {
        if (ac < 2) { tputs("Usage: cat FILE\n"); return; }
        int i = fs_find(av[1]);
        if (i < 0) { tputs("File not found.\n"); return; }
        for (u32 k = 0; k < fsdir[i].size; k++) tputc(fsdata[i][k]);
        if (fsdir[i].size && fsdata[i][fsdir[i].size - 1] != '\n') tputc('\n');
    }
    else if (!kstrcmp(c, "edit") || !kstrcmp(c, "notepad")) {
        if (ac >= 2) note_open_file(av[1]); else open_app(A_NOTE);
    }
    else if (!kstrcmp(c, "rm") || !kstrcmp(c, "del")) {
        if (!is_root) { tputs("Permission denied. Use 'super' first.\n"); return; }
        if (ac < 2) { tputs("Usage: rm FILE\n"); return; }
        int i = fs_find(av[1]);
        if (i < 0) { tputs("File not found.\n"); return; }
        int r = fs_delete(i);
        tputs(r < 0 ? "Disk write failed!\n" : "Deleted.\n");
    }
    else if (!kstrcmp(c, "calc")) {
        i32 a, b;
        if (ac < 4 || !parse_int(av[1], &a) || !parse_int(av[3], &b)) { tputs("Usage: calc 12 * 7\n"); return; }
        char o = av[2][0]; i32 r = 0;
        if (o == '+') r = a + b; else if (o == '-') r = a - b; else if (o == '*') r = a * b;
        else if (o == '/' || o == '%') {
            if (b == 0) { tputs("Division by zero.\n"); return; }
            r = o == '/' ? a / b : a % b;
        } else { tputs("Operators: + - * / %\n"); return; }
        tprintf("= %d\n", r);
    }
    else if (!kstrcmp(c, "date") || !kstrcmp(c, "time")) {
        struct rtc t; rtc_read(&t);
        tprintf("%04d-%02d-%02d %02d:%02d:%02d (RTC)\n", t.y, t.mo, t.d, t.h, t.m, t.s);
    }
    else if (!kstrcmp(c, "uptime")) {
        u32 s = ticks / 100;
        tprintf("up %ud %02u:%02u:%02u\n", s / 86400, (s / 3600) % 24, (s / 60) % 60, s % 60);
    }
    else if (!kstrcmp(c, "mem")) {
        if (mem_kb) tprintf("Memory: %u KB (%u MB)\n", mem_kb, mem_kb / 1024);
        else tputs("Memory size unknown (no multiboot info).\n");
        tprintf("Kernel back buffer: %u KB\n", (u32)(sizeof back / 1024));
    }
    else if (!kstrcmp(c, "disk")) {
        if (!disk_present) { tputs("No disk detected.\nStart QEMU with: -drive file=disk.img,format=raw,if=ide\n"); return; }
        tprintf("Disk: %s\n%u sectors (%u MB)\n", disk_model, disk_sectors, disk_sectors / 2048);
        tprintf("Filesystem: %s\n", fs_on_disk ? "LeactedFS v1" : "none (use the Installer)");
    }
    else if (!kstrcmp(c, "beep")) {
        i32 hz = 880; if (ac > 1) parse_int(av[1], &hz);
        beep(hz, 200);
    }
    else if (!kstrcmp(c, "open")) {
        if (ac < 2) { tputs("Usage: open term|note|paint|files|clock|about|install\n"); return; }
        const char *a = av[1];
        if (!kstrcmp(a, "term")) open_app(A_TERM);
        else if (!kstrcmp(a, "note")) open_app(A_NOTE);
        else if (!kstrcmp(a, "paint")) open_app(A_PAINT);
        else if (!kstrcmp(a, "files")) open_app(A_FILES);
        else if (!kstrcmp(a, "clock")) open_app(A_CLOCK);
        else if (!kstrcmp(a, "about")) open_app(A_ABOUT);
        else if (!kstrcmp(a, "install")) open_app(A_INSTALL);
        else tputs("Unknown app.\n");
    }
    else if (!kstrcmp(c, "install")) open_app(A_INSTALL);
    else if (!kstrcmp(c, "super")) { is_root = !is_root; tputs(is_root ? "Super mode ON\n" : "Super mode OFF\n"); }
    else if (!kstrcmp(c, "reboot")) { tputs("Rebooting...\n"); reboot_now(); }
    else if (!kstrcmp(c, "shutdown") || !kstrcmp(c, "halt")) power_off();
    else if (!kstrcmp(c, "exit")) close_app(A_TERM);
    else tprintf("Unknown: %s\n", av[0]);
}

static void term_commit(void) {
    tputs(prompt()); tputs(iline); tputc('\n');
    char cmd[ILMAX + 1]; kstrcpy(cmd, iline, sizeof cmd);
    if (ilen) {
        if (nhist == 8) { memmove(hist[0], hist[1], 7 * (ILMAX + 1)); nhist--; }
        kstrcpy(hist[nhist++], iline, ILMAX + 1);
    }
    hpos = nhist; ilen = 0; iline[0] = 0;
    run_cmd(cmd);
}
static void term_key(int k) {
    if (k >= 32 && k < 127) { if (ilen < ILMAX) { iline[ilen++] = k; iline[ilen] = 0; } }
    else if (k == '\b') { if (ilen) iline[--ilen] = 0; }
    else if (k == '\n') term_commit();
    else if (k == 12) tclear();
    else if (k == 3) { tputs(prompt()); tputs(iline); tputs("^C\n"); ilen = 0; iline[0] = 0; hpos = nhist; }
    else if (k == K_UP) { if (hpos > 0) { hpos--; kstrcpy(iline, hist[hpos], ILMAX + 1); ilen = slen(iline); } }
    else if (k == K_DOWN) {
        if (hpos < nhist - 1) { hpos++; kstrcpy(iline, hist[hpos], ILMAX + 1); ilen = slen(iline); }
        else { hpos = nhist; ilen = 0; iline[0] = 0; }
    }
}
static void term_draw(int x, int y, int w, int h) {
    rect(x, y, w, h, RGB(12, 12, 12));
    for (int r = 0; r < TR && r <= trow; r++)
        if (r < trow) draw_str(x + 6, y + 6 + r * 10, tg[r], RGB(200, 200, 200));
    /* current output row may hold partial text (no newline yet) */
    if (tcol) draw_str(x + 6, y + 6 + trow * 10, tg[trow], RGB(200, 200, 200));
    int px = x + 6 + tcol * 8, py = y + 6 + trow * 10;
    px = draw_str(px, py, prompt(), RGB(80, 255, 80));
    px = draw_str(px, py, iline, RGB(230, 230, 230));
    if ((ticks / 50) & 1) rect(px, py, 8, 8, RGB(230, 230, 230));
}

/* ------------------------------------------------------------------ */
/* Notepad                                                             */
/* ------------------------------------------------------------------ */
static int note_layout(int tx, int ty, int cols, int rows, int skip, int draw, int *ccol, int *crow) {
    int col = 0, row = 0;
    for (int i = 0; i < nlen; i++) {
        char c = nbuf[i];
        if (c == '\n') { row++; col = 0; continue; }
        if (col >= cols) { row++; col = 0; }
        if (draw && row >= skip && row - skip < rows) draw_char(tx + col * 8, ty + (row - skip) * 10, c, C_BLACK);
        col++;
    }
    if (col >= cols) { row++; col = 0; }
    if (ccol) *ccol = col;
    if (crow) *crow = row;
    return row + 1;
}
static void note_save(void) {
    int i = fs_create(nname);
    if (i < 0) { kstrcpy(nmsg, "Disk full!", sizeof nmsg); return; }
    int r = fs_store(i, nbuf, nlen);
    kstrcpy(nmsg, r == 0 ? "Saved to disk." : r == 1 ? "Saved (RAM only)." : "Disk error!", sizeof nmsg);
    if (r >= 0) ndirty = 0;
}
static void note_draw(int x, int y, int w, int h) {
    int tw = w - 12, th = h - 42;
    rect(x + 6, y + 6, tw, th, C_LIGHT);
    frame(x + 5, y + 5, tw + 2, th + 2, C_DARK);
    int cols = (tw - 8) / 8, rows = (th - 8) / 10;
    int total = note_layout(0, 0, cols, rows, 0, 0, NULL, NULL);
    int skip = total > rows ? total - rows : 0;
    int cc, cr;
    note_layout(x + 10, y + 10, cols, rows, skip, 1, &cc, &cr);
    if ((ticks / 50) & 1) rect(x + 10 + cc * 8, y + 10 + (cr - skip) * 10, 7, 9, RGB(0, 0, 120));
    button(x + 6, y + h - 28, 60, 22, "Save", 0);
    button(x + 72, y + h - 28, 60, 22, "Close", 0);
    drawf(x + 142, y + h - 24, C_TEXT, "%s%s", nname, ndirty ? "*" : "");
    if (nmsg[0]) draw_str(x + 142, y + h - 14, nmsg, RGB(0, 100, 0));
}
static void note_key(int k) {
    if (k == 19) { note_save(); return; }          /* ctrl+S */
    if (k >= 32 && k < 127) { if (nlen < NOTE_MAX) { nbuf[nlen++] = k; nbuf[nlen] = 0; ndirty = 1; nmsg[0] = 0; } }
    else if (k == '\n') { if (nlen < NOTE_MAX) { nbuf[nlen++] = '\n'; nbuf[nlen] = 0; ndirty = 1; } }
    else if (k == '\b') { if (nlen) { nbuf[--nlen] = 0; ndirty = 1; } }
}
static void note_click(int rx, int ry) {
    int h = CH(A_NOTE);
    if (inrect(rx, ry, 6, h - 28, 60, 22)) note_save();
    else if (inrect(rx, ry, 72, h - 28, 60, 22)) close_app(A_NOTE);
}

/* ------------------------------------------------------------------ */
/* Paint                                                               */
/* ------------------------------------------------------------------ */
#define PW 400
#define PH 280
static u8 canvas[PH][PW];
static const u32 pal[10] = {
    RGB(0, 0, 0), RGB(255, 255, 255), RGB(128, 128, 128), RGB(224, 32, 32), RGB(240, 128, 32),
    RGB(240, 208, 32), RGB(32, 160, 32), RGB(32, 192, 208), RGB(32, 64, 224), RGB(128, 48, 192)
};
static int pcol = 0, psize = 3, plastx = -1, plasty = -1;
static const int psizes[4] = { 1, 3, 5, 9 };

static void stamp(int cx, int cy) {
    int s = psize, o = s / 2;
    for (int j = 0; j < s; j++) for (int i = 0; i < s; i++) {
        int px = cx - o + i, py = cy - o + j;
        if (px >= 0 && px < PW && py >= 0 && py < PH) canvas[py][px] = pcol;
    }
}
static void paint_stroke(int cx, int cy) {
    if (plastx < 0) { stamp(cx, cy); }
    else {
        int x0 = plastx, y0 = plasty;
        int dx = cx > x0 ? cx - x0 : x0 - cx, dy = cy > y0 ? y0 - cy : cy - y0;
        int sx = x0 < cx ? 1 : -1, sy = y0 < cy ? 1 : -1, err = dx + dy;
        for (;;) {
            stamp(x0, y0);
            if (x0 == cx && y0 == cy) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    plastx = cx; plasty = cy;
}
static void paint_draw(int x, int y, int w, int h) {
    (void)w; (void)h;
    frame(x + 7, y + 7, PW + 2, PH + 2, C_DARK);
    for (int j = 0; j < PH; j++) {
        u32 *row = &back[(y + 8 + j) * W + x + 8];
        if (y + 8 + j >= cly1 || y + 8 + j < cly0) continue;
        for (int i = 0; i < PW; i++) if (x + 8 + i >= clx0 && x + 8 + i < clx1) row[i] = pal[canvas[j][i]];
    }
    for (int i = 0; i < 10; i++) {
        rect(x + 8 + i * 28, y + 298, 24, 24, pal[i]);
        frame(x + 8 + i * 28, y + 298, 24, 24, i == pcol ? RGB(255, 0, 255) : C_DARK);
    }
    for (int i = 0; i < 4; i++) {
        char b[4]; ksnprintf(b, sizeof b, "%d", psizes[i]);
        button(x + 416, y + 8 + i * 24, 36, 20, b, psize == psizes[i]);
    }
    button(x + 416, y + 110, 36, 20, "Ers", 0);
    button(x + 416, y + 134, 36, 20, "Clr", 0);
    drawf(x + 300, y + 306, C_TEXT, "Color %d  Brush %d", pcol + 1, psize);
}
static void paint_click(int rx, int ry) {
    for (int i = 0; i < 10; i++) if (inrect(rx, ry, 8 + i * 28, 298, 24, 24)) { pcol = i; return; }
    for (int i = 0; i < 4; i++) if (inrect(rx, ry, 416, 8 + i * 24, 36, 20)) { psize = psizes[i]; return; }
    if (inrect(rx, ry, 416, 110, 36, 20)) { pcol = 1; psize = 9; return; }
    if (inrect(rx, ry, 416, 134, 36, 20)) { memset(canvas, 1, sizeof canvas); return; }
    plastx = -1;
}
static void paint_drag(int rx, int ry) {
    int cx = rx - 8, cy = ry - 8;
    if (cx >= 0 && cx < PW && cy >= 0 && cy < PH) paint_stroke(cx, cy); else plastx = -1;
}

/* ------------------------------------------------------------------ */
/* Clock                                                               */
/* ------------------------------------------------------------------ */
static void clock_draw(int x, int y, int w, int h) {
    (void)h;
    struct rtc t; rtc_read(&t);
    int cx = x + w / 2, cy = y + 105;
    fill_circle(cx, cy, 92, C_DARK);
    fill_circle(cx, cy, 90, C_LIGHT);
    for (int i = 0; i < 60; i++) {
        int x0 = cx + sin60[i] * 82 / 256, y0 = cy - sin60[(i + 15) % 60] * 82 / 256;
        int x1 = cx + sin60[i] * 88 / 256, y1 = cy - sin60[(i + 15) % 60] * 88 / 256;
        if (i % 5 == 0) { x0 = cx + sin60[i] * 76 / 256; y0 = cy - sin60[(i + 15) % 60] * 76 / 256; line(x0, y0, x1, y1, C_BLACK); line(x0 + 1, y0, x1 + 1, y1, C_BLACK); }
        else pset(x1, y1, C_DARK);
    }
    int hi = ((t.h % 12) * 5 + t.m / 12) % 60;
    line(cx, cy, cx + sin60[hi] * 50 / 256, cy - sin60[(hi + 15) % 60] * 50 / 256, C_BLACK);
    line(cx + 1, cy, cx + 1 + sin60[hi] * 50 / 256, cy - sin60[(hi + 15) % 60] * 50 / 256, C_BLACK);
    line(cx, cy, cx + sin60[t.m] * 72 / 256, cy - sin60[(t.m + 15) % 60] * 72 / 256, RGB(0, 0, 160));
    line(cx, cy, cx + sin60[t.s] * 80 / 256, cy - sin60[(t.s + 15) % 60] * 80 / 256, RGB(220, 0, 0));
    fill_circle(cx, cy, 3, C_BLACK);
    char b[24]; ksnprintf(b, sizeof b, "%02d:%02d:%02d", t.h, t.m, t.s);
    draw_str_s(cx - 96, y + 212, b, C_TEXT, 3);
    ksnprintf(b, sizeof b, "%04d-%02d-%02d (RTC)", t.y, t.mo, t.d);
    draw_str_c(cx, y + 244, b, C_TEXT);
    u32 s = ticks / 100;
    ksnprintf(b, sizeof b, "Uptime %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    draw_str_c(cx, y + 260, b, C_DARK);
}

/* ------------------------------------------------------------------ */
/* File manager                                                        */
/* ------------------------------------------------------------------ */
static int fsel = -1;
static u32 flast_tick; static int flast_row = -1;
static void files_draw(int x, int y, int w, int h) {
    draw_str(x + 8, y + 6, "Physical Drives", C_TEXT);
    rect(x + 6, y + 20, w - 12, 34, C_LIGHT); frame(x + 6, y + 20, w - 12, 34, C_DARK);
    rect(x + 12, y + 28, 24, 18, RGB(150, 150, 160)); rect(x + 14, y + 40, 6, 3, RGB(60, 220, 60));
    draw_str(x + 44, y + 24, "Local Disk (C:)", C_TEXT);
    if (!disk_present) draw_str(x + 44, y + 38, "No drive - files are RAM only", RGB(160, 0, 0));
    else if (fs_on_disk) drawf(x + 44, y + 38, RGB(0, 110, 0), "LeactedFS v1 - %u MB, saved to disk", disk_sectors / 2048);
    else draw_str(x + 44, y + 38, "Unformatted - use the Installer", RGB(160, 100, 0));
    rect(x + 6, y + 60, w - 12, 12, C_DARK);
    draw_str(x + 10, y + 62, "Name", C_LIGHT); draw_str(x + w - 90, y + 62, "Size", C_LIGHT);
    int row = 0;
    for (int i = 0; i < FS_MAX; i++) {
        if (!fsdir[i].used) continue;
        int ry = y + 74 + row * 11;
        if (i == fsel) rect(x + 6, ry - 1, w - 12, 11, RGB(49, 106, 197));
        u32 fg = i == fsel ? C_LIGHT : C_TEXT;
        draw_str(x + 10, ry, fsdir[i].name, fg);
        drawf(x + w - 90, ry, fg, "%u B", fsdir[i].size);
        row++;
    }
    button(x + 6, y + h - 28, 56, 22, "Open", 0);
    button(x + 68, y + h - 28, 56, 22, "New", 0);
    button(x + 130, y + h - 28, 56, 22, "Delete", 0);
    drawf(x + 196, y + h - 20, C_DARK, "%d/%d files", fs_count(), FS_MAX);
}
static void files_click(int rx, int ry) {
    int h = CH(A_FILES);
    if (inrect(rx, ry, 6, h - 28, 56, 22)) { if (fsel >= 0 && fsdir[fsel].used) note_open_file(fsdir[fsel].name); return; }
    if (inrect(rx, ry, 68, h - 28, 56, 22)) {
        char nm[FS_NAME];
        for (int n = 1; n < 100; n++) {
            ksnprintf(nm, sizeof nm, "NEW%d.TXT", n);
            if (fs_find(nm) < 0) break;
        }
        int i = fs_create(nm);
        if (i >= 0) { fs_store(i, "", 0); fsel = i; note_open_file(nm); }
        return;
    }
    if (inrect(rx, ry, 130, h - 28, 56, 22)) { if (fsel >= 0 && fsdir[fsel].used) { fs_delete(fsel); fsel = -1; } return; }
    int row = 0;
    for (int i = 0; i < FS_MAX; i++) {
        if (!fsdir[i].used) continue;
        if (inrect(rx, ry, 6, 74 + row * 11 - 1, CW(A_FILES) - 12, 11)) {
            if (i == fsel && flast_row == i && ticks - flast_tick < 60) note_open_file(fsdir[i].name);
            fsel = i; flast_row = i; flast_tick = ticks;
            return;
        }
        row++;
    }
}

/* ------------------------------------------------------------------ */
/* About / Installer                                                   */
/* ------------------------------------------------------------------ */
static int inst_state;      /* 0 idle, 1 confirm, 2 ok, 3 fail */
static void about_draw(int x, int y, int w, int h) {
    rect(x + 12, y + 12, 22, 22, RGB(240, 80, 40)); rect(x + 36, y + 12, 22, 22, RGB(100, 190, 40));
    rect(x + 12, y + 36, 22, 22, RGB(40, 140, 240)); rect(x + 36, y + 36, 22, 22, RGB(250, 200, 20));
    draw_str_s(x + 70, y + 16, "LeactedOS", C_TEXT, 2);
    draw_str(x + 70, y + 36, "XP Edition 2.0", RGB(0, 70, 190));
    draw_str(x + 70, y + 48, "From-scratch hobby OS", C_DARK);
    rect(x + 10, y + 70, w - 20, 1, C_DARK);
    u32 s = ticks / 100;
    drawf(x + 14, y + 82, C_TEXT, "Display : 640x480, 32-bit color");
    if (mem_kb) drawf(x + 14, y + 96, C_TEXT, "Memory  : %u MB", mem_kb / 1024);
    else drawf(x + 14, y + 96, C_TEXT, "Memory  : unknown");
    drawf(x + 14, y + 110, C_TEXT, "Disk    : %s", disk_present ? "detected" : "none detected");
    drawf(x + 14, y + 124, C_TEXT, "FS      : %s", fs_on_disk ? "LeactedFS v1" : "none");
    drawf(x + 14, y + 138, C_TEXT, "Uptime  : %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    drawf(x + 14, y + 152, C_TEXT, "Mode    : %s", is_root ? "super (root)" : "normal");
    button(x + w - 74, y + h - 30, 62, 22, "OK", 0);
}
static void about_click(int rx, int ry) {
    if (inrect(rx, ry, CW(A_ABOUT) - 74, CH(A_ABOUT) - 30, 62, 22)) close_app(A_ABOUT);
}
static void install_draw(int x, int y, int w, int h) {
    (void)h;
    draw_str(x + 12, y + 12, "Install LeactedFS to disk", C_TEXT);
    rect(x + 10, y + 30, w - 20, 1, C_DARK);
    if (!disk_present) {
        draw_str(x + 12, y + 44, "No disk detected!", RGB(180, 0, 0));
        draw_str(x + 12, y + 62, "Attach a drive when starting QEMU:", C_TEXT);
        draw_str(x + 12, y + 76, "-drive file=disk.img,format=raw,if=ide", C_DARK);
        draw_str(x + 12, y + 92, "Create one with:", C_TEXT);
        draw_str(x + 12, y + 106, "qemu-img create -f raw disk.img 64M", C_DARK);
        return;
    }
    draw_str(x + 12, y + 40, "Target disk:", C_TEXT);
    drawf(x + 12, y + 54, C_DARK, "%s", disk_model);
    drawf(x + 12, y + 66, C_DARK, "%u sectors (%u MB)", disk_sectors, disk_sectors / 2048);
    if (fs_on_disk) draw_str(x + 12, y + 84, "Already has LeactedFS (will overwrite)", RGB(160, 100, 0));
    else draw_str(x + 12, y + 84, "Disk is empty / unformatted", C_TEXT);
    draw_str(x + 12, y + 100, "This will erase the partition table.", C_TEXT);
    draw_str(x + 12, y + 112, "Files in RAM are copied to the disk.", C_TEXT);
    button(x + w / 2 - 70, y + 134, 140, 26, inst_state == 1 ? "Click again to confirm" : "Install Now", 0);
    if (inst_state == 2) draw_str_c(x + w / 2, y + 172, "Install successful!", RGB(0, 130, 0));
    if (inst_state == 3) draw_str_c(x + w / 2, y + 172, "Install FAILED", RGB(200, 0, 0));
}
static void install_click(int rx, int ry) {
    int w = CW(A_INSTALL);
    if (!disk_present) return;
    if (inrect(rx, ry, w / 2 - 70, 134, 140, 26)) {
        if (inst_state != 1) { inst_state = 1; return; }
        inst_state = fs_install() ? 2 : 3;
    }
}

/* ------------------------------------------------------------------ */
/* Per-app dispatch                                                    */
/* ------------------------------------------------------------------ */
static void app_init(int a) {
    if (a == A_TERM) {
        tclear(); ilen = 0; iline[0] = 0; nhist = 0; hpos = 0;
        tputs("LeactedOS Terminal (640x480)\nType help for commands.\n");
    } else if (a == A_NOTE) {
        nlen = 0; nbuf[0] = 0; ndirty = 0; nmsg[0] = 0; kstrcpy(nname, "UNTITLED.TXT", FS_NAME);
    } else if (a == A_PAINT) {
        static int first = 1;
        if (first) { memset(canvas, 1, sizeof canvas); first = 0; }
        plastx = -1;
    } else if (a == A_INSTALL) inst_state = 0;
}
static void app_draw(int a, int x, int y, int w, int h) {
    clip_set(x, y, w, h);
    rect(x, y, w, h, C_FACE);
    switch (a) {
    case A_TERM: term_draw(x, y, w, h); break;
    case A_NOTE: note_draw(x, y, w, h); break;
    case A_PAINT: paint_draw(x, y, w, h); break;
    case A_FILES: files_draw(x, y, w, h); break;
    case A_CLOCK: clock_draw(x, y, w, h); break;
    case A_ABOUT: about_draw(x, y, w, h); break;
    case A_INSTALL: install_draw(x, y, w, h); break;
    }
    clip_reset();
}
static void app_click(int a, int rx, int ry) {
    switch (a) {
    case A_NOTE: note_click(rx, ry); break;
    case A_PAINT: paint_click(rx, ry); paint_drag(rx, ry); break;
    case A_FILES: files_click(rx, ry); break;
    case A_ABOUT: about_click(rx, ry); break;
    case A_INSTALL: install_click(rx, ry); break;
    }
}
static void app_key(int a, int k) {
    if (a == A_TERM) term_key(k);
    else if (a == A_NOTE) note_key(k);
}

/* ------------------------------------------------------------------ */
/* Desktop, taskbar, start menu                                        */
/* ------------------------------------------------------------------ */
static void build_wallpaper(void) {
    for (int y = 0; y < H; y++) {
        u32 sky = mix(RGB(30, 94, 214), RGB(176, 214, 250), y > 330 ? 330 : y, 330);
        for (int x = 0; x < W; x++) {
            /* rolling green hill, "Bliss"-style */
            int dx = x - 260;
            int hill = 340 + (dx * dx) / 520 - 0;
            hill = 335 + ((dx * dx) >> 9) / 2;
            if (hill > 470) hill = 470;
            u32 c = sky;
            if (y >= hill) c = mix(RGB(96, 176, 52), RGB(30, 110, 24), y - hill > 140 ? 140 : y - hill, 140);
            bgimg[y * W + x] = c;
        }
    }
}

static const char *icon_label[8] = { "Terminal", "Notepad", "Paint", "Files", "Clock", "About", "Install", "Reboot" };
#define ICON_X(i) (14 + ((i) / 4) * 84)
#define ICON_Y(i) (12 + ((i) % 4) * 82)
static int icon_sel = -1;
static u32 icon_tick; static int icon_last = -1;

static void draw_icon(int i, int x, int y) {   /* x,y = top-left of 32x32 */
    switch (i) {
    case 0: rect(x, y + 2, 32, 28, RGB(60, 60, 70)); rect(x + 2, y + 6, 28, 22, C_BLACK); draw_str(x + 5, y + 12, ">_", RGB(80, 255, 80)); break;
    case 1: rect(x + 4, y, 24, 32, C_LIGHT); rect(x + 4, y, 24, 5, RGB(60, 110, 220)); for (int k = 0; k < 4; k++) rect(x + 8, y + 10 + k * 5, 16, 1, C_DARK); break;
    case 2: fill_circle(x + 16, y + 16, 15, RGB(220, 190, 140)); fill_circle(x + 10, y + 11, 3, RGB(220, 30, 30)); fill_circle(x + 19, y + 9, 3, RGB(30, 90, 220)); fill_circle(x + 23, y + 17, 3, RGB(240, 210, 30)); fill_circle(x + 15, y + 22, 3, RGB(30, 170, 40)); break;
    case 3: rect(x, y + 8, 14, 5, RGB(230, 190, 60)); rect(x, y + 12, 32, 18, RGB(250, 210, 80)); rect(x, y + 12, 32, 2, RGB(255, 235, 140)); break;
    case 4: fill_circle(x + 16, y + 16, 15, C_DARK); fill_circle(x + 16, y + 16, 13, C_LIGHT); line(x + 16, y + 16, x + 16, y + 7, C_BLACK); line(x + 16, y + 16, x + 23, y + 18, C_BLACK); break;
    case 5: fill_circle(x + 16, y + 16, 15, RGB(40, 100, 220)); draw_str_s(x + 11, y + 9, "i", C_LIGHT, 2); break;
    case 6: rect(x, y + 6, 32, 20, RGB(150, 150, 160)); rect(x + 3, y + 18, 26, 4, RGB(60, 60, 70)); rect(x + 24, y + 11, 5, 3, RGB(60, 230, 60)); break;
    case 7: fill_circle(x + 16, y + 16, 15, RGB(210, 60, 30)); fill_circle(x + 16, y + 16, 8, RGB(255, 255, 255)); fill_circle(x + 16, y + 16, 6, RGB(210, 60, 30)); rect(x + 14, y + 3, 4, 14, C_LIGHT); break;
    }
}
static void draw_desktop(void) {
    memcpy(back, bgimg, sizeof back);
    for (int i = 0; i < 8; i++) {
        int ix = ICON_X(i), iy = ICON_Y(i);
        draw_icon(i, ix + 16, iy);
        int tw = slen(icon_label[i]) * 8, tx = ix + 32 - tw / 2, ty = iy + 38;
        if (i == icon_sel) rect(tx - 2, ty - 2, tw + 4, 12, RGB(49, 106, 197));
        draw_str(tx + 1, ty + 1, icon_label[i], C_BLACK);
        draw_str(tx, ty, icon_label[i], C_LIGHT);
    }
}

static void draw_window(int a) {
    struct win *w = &wins[a];
    int focus = top_app() == a;
    rect(w->x, w->y, w->w, w->h, focus ? C_FRAME : RGB(122, 150, 223));
    gradient_v(w->x + 3, w->y + 3, w->w - 6, TITLE_H, focus ? C_TITLE2 : RGB(157, 180, 232), focus ? C_TITLE1 : RGB(122, 150, 223));
    draw_str(w->x + 9, w->y + 11, w->title, focus ? C_LIGHT : RGB(220, 228, 248));
    /* minimize + close */
    int bx = w->x + w->w - 24, by = w->y + 6;
    rect(bx, by, 18, 18, RGB(217, 83, 58)); frame(bx, by, 18, 18, C_LIGHT);
    line(bx + 5, by + 5, bx + 12, by + 12, C_LIGHT); line(bx + 12, by + 5, bx + 5, by + 12, C_LIGHT);
    line(bx + 6, by + 5, bx + 13, by + 12, C_LIGHT); line(bx + 13, by + 5, bx + 6, by + 12, C_LIGHT);
    bx -= 22;
    rect(bx, by, 18, 18, RGB(50, 110, 230)); frame(bx, by, 18, 18, C_LIGHT);
    rect(bx + 4, by + 11, 8, 3, C_LIGHT);
    app_draw(a, CX(a), CY(a), CW(a), CH(a));
}

static int start_open;
#define SM_W 200
#define SM_ITEM 24
#define SM_HEAD 30
#define SM_N 9
#define SM_H (SM_HEAD + SM_N * SM_ITEM + 6)
#define SM_X 2
#define SM_Y (H - TB_H - SM_H)
static struct rtc cached_rtc; static u32 rtc_tick = 0xFFFFFFFF;

static void draw_taskbar(void) {
    gradient_v(0, H - TB_H, W, TB_H, RGB(60, 130, 245), RGB(30, 88, 220));
    rect(0, H - TB_H, W, 1, RGB(120, 170, 255));
    gradient_v(0, H - TB_H, 68, TB_H, RGB(74, 170, 74), RGB(44, 130, 44));
    rect(0, H - TB_H, 68, 1, RGB(140, 220, 140));
    draw_str(10, H - TB_H + 10, "start", C_LIGHT);
    int bx = 74;
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        int active = top_app() == a;
        gradient_v(bx, H - TB_H + 3, 104, TB_H - 6, active ? RGB(30, 80, 190) : RGB(70, 140, 250), active ? RGB(40, 100, 220) : RGB(50, 110, 235));
        frame(bx, H - TB_H + 3, 104, TB_H - 6, RGB(20, 60, 160));
        char b[14]; kstrcpy(b, wins[a].title, 13);
        draw_str(bx + 6, H - TB_H + 10, b, C_LIGHT);
        bx += 108;
    }
    if (rtc_tick == 0xFFFFFFFF || ticks - rtc_tick >= 100) { rtc_read(&cached_rtc); rtc_tick = ticks; }
    rect(W - 64, H - TB_H, 64, TB_H, RGB(22, 120, 232));
    drawf(W - 52, H - TB_H + 10, C_LIGHT, "%02d:%02d", cached_rtc.h, cached_rtc.m);
}
static void draw_startmenu(void) {
    rect(SM_X, SM_Y, SM_W, SM_H, C_LIGHT); frame(SM_X, SM_Y, SM_W, SM_H, C_FRAME);
    gradient_v(SM_X + 1, SM_Y + 1, SM_W - 2, SM_HEAD - 1, RGB(30, 90, 210), RGB(60, 130, 245));
    draw_str(SM_X + 10, SM_Y + 11, "LeactedOS XP", C_LIGHT);
    for (int i = 0; i < SM_N; i++) {
        int iy = SM_Y + SM_HEAD + i * SM_ITEM;
        const char *t = i < N_APPS ? app_names[i] : i == 7 ? "Restart" : "Shut Down";
        if (i == 7) rect(SM_X + 8, iy - 1, SM_W - 16, 1, C_DARK);
        int hot = inrect(mx, my, SM_X, iy, SM_W, SM_ITEM);
        if (hot) rect(SM_X + 2, iy, SM_W - 4, SM_ITEM, RGB(49, 106, 197));
        if (i < N_APPS) { static const u32 bc[7] = { RGB(60,60,70), RGB(60,110,220), RGB(220,60,50), RGB(240,200,60), RGB(120,120,130), RGB(40,100,220), RGB(80,160,80) }; rect(SM_X + 10, iy + 7, 10, 10, bc[i]); frame(SM_X + 10, iy + 7, 10, 10, C_DARK); }
        draw_str(SM_X + 34, iy + 8, t, hot ? C_LIGHT : C_TEXT);
    }
}

/* mouse arrow */
static const char *cur_shape[16] = {
    "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ", "X.....X    ", "X......X   ",
    "X.......X  ", "X....XXXXX ", "X..X..X    ", "X.X X..X   ", "XX  X..X   ", "X    X..X  ", "     X..X  ", "      XX   ",
};
static void draw_cursor(void) {
    for (int j = 0; j < 16; j++) for (int i = 0; cur_shape[j][i]; i++) {
        char c = cur_shape[j][i];
        if (c == 'X') pset(mx + i, my + j, C_BLACK); else if (c == '.') pset(mx + i, my + j, C_LIGHT);
    }
}

/* --------------------------- events -------------------------------- */
static int drag_app = -1, drag_dx, drag_dy, press_app = -1;

static void start_item(int i) {
    if (i < N_APPS) open_app(i);
    else if (i == 7) reboot_now();
    else power_off();
}
static void on_press(int x, int y) {
    int was_open = start_open;
    if (start_open) {
        start_open = 0;
        if (inrect(x, y, SM_X, SM_Y + SM_HEAD, SM_W, SM_N * SM_ITEM)) { start_item((y - SM_Y - SM_HEAD) / SM_ITEM); return; }
    }
    if (y >= H - TB_H) {
        if (x < 68) { start_open = !was_open; return; }
        int bx = 74;
        for (int a = 0; a < N_APPS; a++) {
            if (!wins[a].open) continue;
            if (inrect(x, y, bx, H - TB_H + 3, 104, TB_H - 6)) {
                if (wins[a].min || top_app() != a) raise_app(a); else wins[a].min = 1;
                return;
            }
            bx += 108;
        }
        return;
    }
    for (int i = nz - 1; i >= 0; i--) {
        int a = zo[i]; struct win *w = &wins[a];
        if (w->min || !inrect(x, y, w->x, w->y, w->w, w->h)) continue;
        raise_app(a);
        if (inrect(x, y, w->x + w->w - 24, w->y + 6, 18, 18)) { close_app(a); return; }
        if (inrect(x, y, w->x + w->w - 46, w->y + 6, 18, 18)) { w->min = 1; return; }
        if (y < w->y + 3 + TITLE_H) { drag_app = a; drag_dx = x - w->x; drag_dy = y - w->y; return; }
        press_app = a;
        app_click(a, x - CX(a), y - CY(a));
        return;
    }
    /* desktop icons */
    icon_sel = -1;
    for (int i = 0; i < 8; i++) {
        if (inrect(x, y, ICON_X(i), ICON_Y(i), 64, 56)) {
            int dbl = icon_last == i && ticks - icon_tick < 60;
            icon_sel = i; icon_last = i; icon_tick = ticks;
            if (dbl) {
                if (i < 7) open_app(i == 0 ? A_TERM : i == 1 ? A_NOTE : i == 2 ? A_PAINT : i == 3 ? A_FILES : i == 4 ? A_CLOCK : i == 5 ? A_ABOUT : A_INSTALL);
                else reboot_now();
            }
            return;
        }
    }
}
static void on_release(void) {
    drag_app = -1; press_app = -1; plastx = -1;
}
static void on_move_held(int x, int y) {
    if (drag_app >= 0) {
        struct win *w = &wins[drag_app];
        w->x = x - drag_dx; w->y = y - drag_dy;
        if (w->y < 0) w->y = 0;
        if (w->y > H - TB_H - 26) w->y = H - TB_H - 26;
        if (w->x < 80 - w->w) w->x = 80 - w->w;
        if (w->x > W - 80) w->x = W - 80;
    } else if (press_app == A_PAINT && wins[A_PAINT].open) {
        paint_drag(x - CX(A_PAINT), y - CY(A_PAINT));
    }
}

/* ------------------------------------------------------------------ */
/* Frame / main                                                        */
/* ------------------------------------------------------------------ */
static void os_draw(void) {
    clip_reset();
    draw_desktop();
    for (int i = 0; i < nz; i++) if (!wins[zo[i]].min) draw_window(zo[i]);
    draw_taskbar();
    if (start_open) draw_startmenu();
    draw_cursor();
}

static int prev_btn;
static void os_events(void) {
    input_poll();
    int k;
    while ((k = kq_pop())) {
        int a = top_app();
        if (a >= 0) app_key(a, k);
    }
    int left = mbtn & 1;
    if (left && !prev_btn) on_press(mx, my);
    else if (left) on_move_held(mx, my);
    else if (prev_btn) on_release();
    prev_btn = left;
}

static void os_init(u32 magic, const u32 *mbi) {
    boot_via_multiboot = magic == 0x2BADB002;
    if (boot_via_multiboot && mbi && (mbi[0] & 1)) mem_kb = 1024 + mbi[2];
#ifdef HOST
    mem_kb = 262144;
#endif
    video_init();
    build_wallpaper();
    memset(canvas, 1, sizeof canvas);
    ps2_init();
    ata_identify();
    fs_check();
    fs_load();
    irq_init();
    open_app(A_TERM);
}

void kernel_main(u32 magic, const u32 *mbi) {
    os_init(magic, mbi);
    u32 next = ticks;
    for (;;) {
        os_events();
        os_draw();
        flip();
        next += 3;                                   /* ~33 fps */
        while ((i32)(ticks - next) < 0) { wait_tick(); input_poll(); }
        if ((i32)(ticks - next) > 30) next = ticks;  /* fell behind: resync */
    }
}
