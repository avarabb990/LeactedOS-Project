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
#include "logo.h"

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
static void blit_argb(int x, int y, int w, int h, const u32 *src) {
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < cly0 || py >= cly1) continue;
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < clx0 || px >= clx1) continue;
            u32 v = src[j * w + i], a = v >> 24;
            if (!a) continue;
            if (a == 255) { back[py * W + px] = v & 0xFFFFFF; continue; }
            u32 d = back[py * W + px];
            u32 r = (((v >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
            u32 g = (((v >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
            u32 b = ((v & 255) * a + (d & 255) * (255 - a)) / 255;
            back[py * W + px] = (r << 16) | (g << 8) | b;
        }
    }
}
static void drawf(int x, int y, u32 c, const char *f, ...) {
    char b[96]; va_list ap; va_start(ap, f); kvsnprintf(b, sizeof b, f, ap); va_end(ap);
    draw_str(x, y, b, c);
}

/* XP-ish palette */
/* ---- themes ---- */
struct theme {
    const char *name, *menu_title, *blurb;
    u32 face;
    u32 ta1, ta2, ti1, ti2;            /* title bar gradient: active / inactive */
    u32 frame_a, frame_i;
    u32 txt_a, txt_i;
    u32 menu_bg, menu_fg, menu_hl, menu_hl_fg, menu_h1, menu_h2, menu_edge;
};
enum { TH_XP, TH_UNITY, TH_KDE, N_THEMES };
static const struct theme themes[N_THEMES] = {
    { "Windows XP", "LeactedOS XP", "Blue title bars, green Start",
      RGB(236, 233, 216),
      RGB(58, 129, 243), RGB(0, 88, 230), RGB(157, 180, 232), RGB(122, 150, 223),
      RGB(8, 49, 217), RGB(122, 150, 223), RGB(255, 255, 255), RGB(220, 228, 248),
      RGB(255, 255, 255), RGB(0, 0, 0), RGB(49, 106, 197), RGB(255, 255, 255), RGB(30, 90, 210), RGB(60, 130, 245), RGB(8, 49, 217) },
    { "Ubuntu Unity", "Dash", "Dark panel and left launcher",
      RGB(242, 241, 240),
      RGB(74, 73, 69), RGB(52, 51, 47), RGB(92, 91, 86), RGB(72, 71, 67),
      RGB(52, 51, 47), RGB(72, 71, 67), RGB(223, 219, 210), RGB(150, 147, 140),
      RGB(44, 40, 38), RGB(235, 232, 228), RGB(221, 72, 20), RGB(255, 255, 255), RGB(94, 40, 76), RGB(60, 24, 50), RGB(20, 12, 18) },
    { "KDE Classic", "K Menu", "Blue-gray panel, K menu",
      RGB(238, 238, 238),
      RGB(116, 164, 224), RGB(68, 118, 198), RGB(196, 204, 216), RGB(170, 180, 196),
      RGB(60, 96, 160), RGB(150, 160, 178), RGB(255, 255, 255), RGB(90, 100, 120),
      RGB(244, 244, 246), RGB(20, 20, 30), RGB(60, 110, 190), RGB(255, 255, 255), RGB(92, 142, 210), RGB(50, 100, 180), RGB(60, 96, 160) },
};
static int th_id;
static const struct theme *th = &themes[0];
static int show_icons = 1;
static int term_color_idx;
#define C_FACE   (th->face)
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
        "Welcome to LeactedOS XP Edition 3.0!\n"
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
enum { A_TERM, A_NOTE, A_PAINT, A_FILES, A_CLOCK, A_ABOUT, A_INSTALL, A_SETTINGS, A_TASK, A_CALC, N_APPS };
static void theme_apply(int id);
static void settings_save(void);

struct win { int open, min, x, y, w, h; const char *title; };
static struct win wins[N_APPS] = {
    [A_TERM]    = { 0, 0, 90, 24, 468, 340, "Terminal" },
    [A_NOTE]    = { 0, 0, 150, 50, 420, 300, "Notepad" },
    [A_PAINT]   = { 0, 0, 60, 20, 464, 372, "Paint" },
    [A_FILES]   = { 0, 0, 110, 40, 420, 320, "File Manager" },
    [A_CLOCK]   = { 0, 0, 330, 40, 260, 310, "Clock" },
    [A_ABOUT]   = { 0, 0, 200, 60, 340, 288, "About LeactedOS" },
    [A_INSTALL] = { 0, 0, 140, 70, 380, 250, "LeactedOS Installer" },
    [A_SETTINGS] = { 0, 0, 120, 50, 380, 300, "Settings" },
    [A_TASK]    = { 0, 0, 100, 30, 380, 320, "Task Manager" },
    [A_CALC]    = { 0, 0, 260, 40, 232, 300, "Calculator" },
};
static const char *app_names[N_APPS] = { "Terminal", "Notepad", "Paint", "File Manager", "Clock", "About", "Installer", "Settings", "Task Manager", "Calculator" };
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
    tputs("               about install settings task calc\n");
    tputs(" theme NAME    xp | unity | kde\n");
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
        tputs("LeactedOS XP Edition 3.0 (640x480)\nFrom-scratch hobby OS, rebuilt & upgraded.\n");
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
        else if (!kstrcmp(a, "settings")) open_app(A_SETTINGS);
        else if (!kstrcmp(a, "task")) open_app(A_TASK);
        else if (!kstrcmp(a, "calc")) open_app(A_CALC);
        else tputs("Unknown app.\n");
    }
    else if (!kstrcmp(c, "install")) open_app(A_INSTALL);
    else if (!kstrcmp(c, "theme")) {
        int t = -1;
        if (ac >= 2) {
            if (!kstrcmp(av[1], "xp")) t = TH_XP;
            else if (!kstrcmp(av[1], "unity") || !kstrcmp(av[1], "ubuntu")) t = TH_UNITY;
            else if (!kstrcmp(av[1], "kde")) t = TH_KDE;
        }
        if (t < 0) { tprintf("Current theme: %s\nUsage: theme xp | unity | kde\n", themes[th_id].name); return; }
        theme_apply(t); settings_save();
        tprintf("Theme: %s\n", themes[t].name);
    }
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
static const u32 term_cols[4] = { RGB(120, 230, 120), RGB(210, 210, 210), RGB(255, 190, 60), RGB(100, 220, 240) };
static void term_draw(int x, int y, int w, int h) {
    rect(x, y, w, h, RGB(12, 12, 12));
    u32 tfg = term_cols[term_color_idx];
    for (int r = 0; r < TR && r <= trow; r++)
        if (r < trow) draw_str(x + 6, y + 6 + r * 10, tg[r], tfg);
    /* current output row may hold partial text (no newline yet) */
    if (tcol) draw_str(x + 6, y + 6 + trow * 10, tg[trow], tfg);
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
    blit_argb(x + (w - logo96_W) / 2, y + 4, logo96_W, logo96_H, logo96);
    draw_str_c(x + w / 2, y + 106, "XP Edition 3.0", RGB(0, 70, 190));
    draw_str_c(x + w / 2, y + 118, "From-scratch hobby OS", C_DARK);
    rect(x + 10, y + 132, w - 20, 1, C_DARK);
    u32 s = ticks / 100;
    drawf(x + 14, y + 142, C_TEXT, "Display : 640x480, 32-bit color");
    if (mem_kb) drawf(x + 14, y + 156, C_TEXT, "Memory  : %u MB", mem_kb / 1024);
    else drawf(x + 14, y + 156, C_TEXT, "Memory  : unknown");
    drawf(x + 14, y + 170, C_TEXT, "Disk    : %s", disk_present ? "detected" : "none detected");
    drawf(x + 14, y + 184, C_TEXT, "FS      : %s", fs_on_disk ? "LeactedFS v1" : "none");
    drawf(x + 14, y + 198, C_TEXT, "Uptime  : %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    drawf(x + 14, y + 212, C_TEXT, "Mode    : %s", is_root ? "super (root)" : "normal");
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
/* Settings                                                            */
/* ------------------------------------------------------------------ */
static char set_msg[40];

static void settings_save(void) {
    char b[64];
    int n = ksnprintf(b, sizeof b, "theme=%d\nicons=%d\nterm=%d\n", th_id, show_icons, term_color_idx);
    int i = fs_create("SETTINGS.CFG");
    if (i < 0) { kstrcpy(set_msg, "Not saved (15 files max)", sizeof set_msg); return; }
    int r = fs_store(i, b, n);
    kstrcpy(set_msg, r == 0 ? "Saved to disk." : r == 1 ? "Applied (RAM only)." : "Disk error!", sizeof set_msg);
}
static void settings_load(void) {
    int i = fs_find("SETTINGS.CFG");
    if (i < 0) return;
    char b[65]; u32 n = fsdir[i].size > 64 ? 64 : fsdir[i].size;
    memcpy(b, fsdata[i], n); b[n] = 0;
    for (char *p = b; *p;) {
        char v = (p[0] && p[1] && p[2] && p[3] && p[4] && p[5]) ? p[6] : 0;
        if (!memcmp(p, "theme=", 6) && v >= '0' && v < '0' + N_THEMES) th_id = v - '0';
        else if (!memcmp(p, "icons=", 6) && (v == '0' || v == '1')) show_icons = v - '0';
        else if (!memcmp(p, "term=", 5) && p[5] >= '0' && p[5] <= '3') term_color_idx = p[5] - '0';
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
}
static void theme_preview(int x, int y, int id) {
    const struct theme *t = &themes[id];
    rect(x, y, 64, 36, t->face); frame(x, y, 64, 36, t->frame_a);
    if (id == TH_XP) {
        gradient_v(x + 1, y + 1, 62, 9, t->ta1, t->ta2);
        gradient_v(x + 1, y + 28, 62, 7, RGB(60, 130, 245), RGB(30, 88, 220));
        rect(x + 1, y + 28, 18, 7, RGB(60, 160, 60));
    } else if (id == TH_UNITY) {
        rect(x + 1, y + 1, 62, 5, RGB(52, 51, 47));
        rect(x + 1, y + 6, 10, 29, RGB(48, 42, 40));
        for (int k = 0; k < 3; k++) rect(x + 3, y + 9 + k * 8, 6, 6, k == 0 ? RGB(221, 72, 20) : RGB(150, 140, 130));
        gradient_v(x + 14, y + 10, 46, 8, t->ta1, t->ta2);
        fill_circle(x + 18, y + 14, 2, RGB(221, 72, 20));
    } else {
        gradient_v(x + 1, y + 1, 62, 9, t->ta1, t->ta2);
        gradient_v(x + 1, y + 28, 62, 7, RGB(214, 220, 232), RGB(160, 170, 190));
        rect(x + 1, y + 28, 12, 7, RGB(60, 110, 190));
    }
}
#define SET_ROW(i) (24 + (i) * 46)
static void settings_draw(int x, int y, int w, int h) {
    draw_str(x + 8, y + 8, "Appearance", C_TEXT);
    for (int i = 0; i < N_THEMES; i++) {
        int ry = y + SET_ROW(i);
        int sel = th_id == i;
        rect(x + 6, ry, w - 12, 42, sel ? RGB(255, 255, 255) : C_FACE);
        frame(x + 6, ry, w - 12, 42, sel ? RGB(49, 106, 197) : C_DARK);
        theme_preview(x + 12, ry + 3, i);
        draw_str(x + 88, ry + 8, themes[i].name, C_TEXT);
        draw_str(x + 88, ry + 22, themes[i].blurb, C_DARK);
        fill_circle(x + w - 24, ry + 21, 6, C_DARK);
        fill_circle(x + w - 24, ry + 21, 5, C_LIGHT);
        if (sel) fill_circle(x + w - 24, ry + 21, 3, RGB(49, 106, 197));
    }
    frame(x + 12, y + 168, 14, 14, C_DARK); rect(x + 13, y + 169, 12, 12, C_LIGHT);
    if (show_icons) { line(x + 15, y + 175, x + 18, y + 179, C_BLACK); line(x + 18, y + 179, x + 24, y + 171, C_BLACK); line(x + 15, y + 174, x + 18, y + 178, C_BLACK); line(x + 18, y + 178, x + 24, y + 170, C_BLACK); }
    draw_str(x + 34, y + 171, "Show desktop icons", C_TEXT);
    draw_str(x + 8, y + 194, "Terminal text color", C_TEXT);
    for (int i = 0; i < 4; i++) {
        rect(x + 12 + i * 40, y + 208, 32, 18, term_cols[i]);
        frame(x + 12 + i * 40, y + 208, 32, 18, i == term_color_idx ? RGB(255, 0, 255) : C_DARK);
    }
    drawf(x + 8, y + h - 20, C_DARK, "%s", set_msg[0] ? set_msg : "Settings are kept in SETTINGS.CFG");
    button(x + w - 74, y + h - 30, 62, 22, "Close", 0);
}
static void settings_click(int rx, int ry) {
    int w = CW(A_SETTINGS), h = CH(A_SETTINGS);
    for (int i = 0; i < N_THEMES; i++)
        if (inrect(rx, ry, 6, SET_ROW(i), w - 12, 42)) { theme_apply(i); settings_save(); return; }
    if (inrect(rx, ry, 12, 168, 150, 14)) { show_icons = !show_icons; settings_save(); return; }
    for (int i = 0; i < 4; i++)
        if (inrect(rx, ry, 12 + i * 40, 208, 32, 18)) { term_color_idx = i; settings_save(); return; }
    if (inrect(rx, ry, w - 74, h - 30, 62, 22)) close_app(A_SETTINGS);
}

/* ------------------------------------------------------------------ */
/* Task Manager                                                        */
/* ------------------------------------------------------------------ */
static int tsel = -1;
static u32 frame_count, fps_t, fps_f, fps;

static void task_draw(int x, int y, int w, int h) {
    (void)h;
    draw_str(x + 8, y + 6, "Applications", C_TEXT);
    rect(x + 6, y + 20, w - 12, 128, C_LIGHT); frame(x + 6, y + 20, w - 12, 128, C_DARK);
    rect(x + 7, y + 21, w - 14, 12, C_DARK);
    draw_str(x + 10, y + 23, "Task", C_LIGHT); draw_str(x + 220, y + 23, "Status", C_LIGHT);
    int row = 0, n = 0;
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        n++;
        int ry = y + 35 + row * 11;
        if (a == tsel) rect(x + 7, ry - 1, w - 14, 11, RGB(49, 106, 197));
        u32 fg = a == tsel ? C_LIGHT : C_TEXT;
        draw_str(x + 10, ry, wins[a].title, fg);
        draw_str(x + 220, ry, wins[a].min ? "Minimized" : top_app() == a ? "Active" : "Running", fg);
        row++;
    }
    button(x + 6, y + 154, 76, 22, "End Task", 0);
    button(x + 88, y + 154, 76, 22, "Switch To", 0);
    drawf(x + 176, y + 162, C_DARK, "%d task(s) running", n);
    rect(x + 6, y + 184, w - 12, 1, C_DARK);
    draw_str(x + 8, y + 190, "System", C_TEXT);
    u32 s = ticks / 100;
    drawf(x + 12, y + 204, C_TEXT, "Uptime  : %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    u32 used_kb = (u32)((sizeof back + sizeof bgimg + sizeof fsdata + sizeof canvas + sizeof tg + 65536) / 1024);
    drawf(x + 12, y + 218, C_TEXT, "Kernel  : %u KB of %u KB", used_kb, mem_kb ? mem_kb : used_kb);
    int bw = w - 24, fill = mem_kb ? (int)((u32)bw * used_kb / mem_kb) : bw;
    if (fill < 2) fill = 2;
    rect(x + 12, y + 230, bw, 8, C_LIGHT); frame(x + 12, y + 230, bw, 8, C_DARK);
    rect(x + 13, y + 231, fill > bw - 2 ? bw - 2 : fill, 6, RGB(60, 170, 70));
    drawf(x + 12, y + 244, C_TEXT, "Files   : %d / %d", fs_count(), FS_MAX);
    drawf(x + 12, y + 258, C_TEXT, "Disk    : %s", disk_present ? (fs_on_disk ? "LeactedFS" : "unformatted") : "none");
    drawf(x + 12, y + 272, C_TEXT, "Display : 640x480  %u fps  [%s]", fps, themes[th_id].name);
}
static void task_click(int rx, int ry) {
    int w = CW(A_TASK);
    int row = 0;
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        if (inrect(rx, ry, 7, 35 + row * 11 - 1, w - 14, 11)) { tsel = a; return; }
        row++;
    }
    if (inrect(rx, ry, 6, 154, 76, 22)) { if (tsel >= 0 && wins[tsel].open) { close_app(tsel); tsel = -1; } return; }
    if (inrect(rx, ry, 88, 154, 76, 22)) { if (tsel >= 0 && wins[tsel].open) raise_app(tsel); return; }
}

/* ------------------------------------------------------------------ */
/* Calculator (fixed point, 2 decimals)                                */
/* ------------------------------------------------------------------ */
static i32 c_acc, c_ent;
static char c_op;
static int c_have, c_neg, c_frac = -1, c_after, c_err;

static void calc_reset(void) { c_acc = 0; c_ent = 0; c_op = 0; c_have = 0; c_neg = 0; c_frac = -1; c_after = 0; c_err = 0; }
static int calc_math(i32 a, char op, i32 b, i32 *r) {
    switch (op) {
    case '+': return !__builtin_add_overflow(a, b, r);
    case '-': return !__builtin_sub_overflow(a, b, r);
    case '*': {
        i32 q = a / 100, m = a % 100, t1, t2a, t2b;
        if (__builtin_mul_overflow(q, b, &t1)) return 0;
        if (__builtin_mul_overflow(m, b / 100, &t2a)) return 0;
        if (__builtin_mul_overflow(m, b % 100, &t2b)) return 0;
        i32 t2 = t2a + t2b / 100;
        return !__builtin_add_overflow(t1, t2, r);
    }
    case '/': {
        if (b == 0) return 0;
        i32 q = a / b, rem = a % b, t1, t2;
        if (__builtin_mul_overflow(q, 100, &t1)) return 0;
        if (__builtin_mul_overflow(rem, 100, &t2)) t2 = (b >= 100 || b <= -100) ? rem / (b / 100) : 0;
        else t2 = t2 / b;
        return !__builtin_add_overflow(t1, t2, r);
    }
    }
    return 0;
}
static void calc_fmt(i32 v, char *o) {
    u32 u;
    if (v < 0) { *o++ = '-'; u = (u32)0 - (u32)v; } else u = (u32)v;
    o += ksnprintf(o, 14, "%u", u / 100);
    u32 fp = u % 100;
    if (fp) { *o++ = '.'; *o++ = '0' + fp / 10; if (fp % 10) *o++ = '0' + fp % 10; }
    *o = 0;
}
static void calc_fmt_entry(char *o) {
    if (c_neg) *o++ = '-';
    o += ksnprintf(o, 14, "%u", (u32)c_ent / 100);
    if (c_frac >= 0) {
        *o++ = '.';
        if (c_frac >= 1) *o++ = '0' + (c_ent % 100) / 10;
        if (c_frac >= 2) *o++ = '0' + c_ent % 10;
    }
    *o = 0;
}
static void calc_commit(void) {
    i32 v = c_neg ? -c_ent : c_ent;
    if (c_op) { i32 r; if (!calc_math(c_acc, c_op, v, &r)) { c_err = 1; return; } c_acc = r; }
    else c_acc = v;
    c_have = 0; c_ent = 0; c_neg = 0; c_frac = -1;
}
static void calc_start_entry(void) { if (!c_have) { c_have = 1; c_ent = 0; c_neg = 0; c_frac = -1; } }
static void calc_press(char k) {
    if (c_err && k != 'C') return;
    if (k >= '0' && k <= '9') {
        if (c_after) calc_reset();
        calc_start_entry();
        int d = k - '0';
        if (c_frac < 0) { if (c_ent / 100 < 1000000) c_ent = (c_ent / 100 * 10 + d) * 100; }
        else if (c_frac == 0) { c_ent += d * 10; c_frac = 1; }
        else if (c_frac == 1) { c_ent += d; c_frac = 2; }
        return;
    }
    switch (k) {
    case 'C': calc_reset(); break;
    case '.': if (c_after) calc_reset(); calc_start_entry(); if (c_frac < 0) c_frac = 0; break;
    case '<':
        if (c_have) {
            if (c_frac >= 0) { if (c_frac == 2) c_ent -= c_ent % 10; else if (c_frac == 1) c_ent -= c_ent % 100; c_frac--; }
            else c_ent = (c_ent / 100 / 10) * 100;
        }
        break;
    case 'N': if (c_have) c_neg = !c_neg; else if (c_acc != (i32)0x80000000) c_acc = -c_acc; break;
    case '%': if (c_have) { c_ent /= 100; c_frac = 2; } break;
    case '+': case '-': case '*': case '/':
        if (c_have) calc_commit();
        if (!c_err) { c_op = k; c_after = 0; }
        break;
    case '=':
        if (c_have) calc_commit();
        c_op = 0; c_after = 1;
        break;
    }
}
static const char *calc_lbl[20] = { "C", "<", "%", "/", "7", "8", "9", "x", "4", "5", "6", "-", "1", "2", "3", "+", "+/-", "0", ".", "=" };
static const char calc_map[20] = { 'C', '<', '%', '/', '7', '8', '9', '*', '4', '5', '6', '-', '1', '2', '3', '+', 'N', '0', '.', '=' };

static void calc_draw(int x, int y, int w, int h) {
    (void)h;
    int ox = x + (w - 196) / 2;
    rect(ox, y + 8, 196, 40, C_LIGHT); frame(ox, y + 8, 196, 40, C_DARK);
    char b[24];
    if (c_err) kstrcpy(b, "Error", sizeof b);
    else if (c_have) calc_fmt_entry(b);
    else calc_fmt(c_acc, b);
    int len = slen(b), sc = len * 16 <= 184 ? 2 : 1, tw = len * 8 * sc;
    draw_str_s(ox + 196 - 6 - tw, y + 8 + (40 - 8 * sc) / 2, b, c_err ? RGB(190, 0, 0) : C_TEXT, sc);
    if (c_op) draw_char(ox + 6, y + 12, c_op, RGB(0, 70, 190));
    for (int i = 0; i < 20; i++) {
        int bx = ox + (i % 4) * 50, by = y + 58 + (i / 4) * 42;
        char k = calc_map[i];
        if (k == '=') { bevel(bx, by, 46, 38, 0); rect(bx + 2, by + 2, 42, 34, RGB(60, 130, 245)); }
        else bevel(bx, by, 46, 38, 0);
        u32 col = k == '=' ? C_LIGHT : k == 'C' ? RGB(200, 20, 20) : (k >= '0' && k <= '9') || k == '.' ? C_BLACK : RGB(0, 70, 190);
        if (slen(calc_lbl[i]) == 1) draw_str_s(bx + 15, by + 11, calc_lbl[i], col, 2);
        else draw_str(bx + 11, by + 15, calc_lbl[i], col);
    }
}
static void calc_click(int rx, int ry) {
    int ox = (CW(A_CALC) - 196) / 2;
    for (int i = 0; i < 20; i++)
        if (inrect(rx, ry, ox + (i % 4) * 50, 58 + (i / 4) * 42, 46, 38)) { calc_press(calc_map[i]); return; }
}
static void calc_key(int k) {
    if (k >= '0' && k <= '9') calc_press(k);
    else if (k == '+' || k == '-' || k == '*' || k == '/' || k == '.' || k == '%') calc_press(k);
    else if (k == 'x' || k == 'X') calc_press('*');
    else if (k == '=' || k == '\n') calc_press('=');
    else if (k == '\b') calc_press('<');
    else if (k == 'c' || k == 'C' || k == K_ESC) calc_press('C');
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
    else if (a == A_SETTINGS) set_msg[0] = 0;
    else if (a == A_TASK) tsel = -1;
    else if (a == A_CALC) calc_reset();
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
    case A_SETTINGS: settings_draw(x, y, w, h); break;
    case A_TASK: task_draw(x, y, w, h); break;
    case A_CALC: calc_draw(x, y, w, h); break;
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
    case A_SETTINGS: settings_click(rx, ry); break;
    case A_TASK: task_click(rx, ry); break;
    case A_CALC: calc_click(rx, ry); break;
    }
}
static void app_key(int a, int k) {
    if (a == A_TERM) term_key(k);
    else if (a == A_NOTE) note_key(k);
    else if (a == A_CALC) calc_key(k);
}

/* ------------------------------------------------------------------ */
/* Desktop, themes, panels, start menu                                 */
/* ------------------------------------------------------------------ */
#define UBAR_H 24            /* Unity top panel */
#define LAUNCH_W 52          /* Unity left launcher */
#define LAUNCH_SLOT 40
#define START_W 84
#define TB_X0 90
#define LAUNCH_TOP (UBAR_H + 52)

static int area_x0, area_y0, area_y1;   /* usable desktop area for windows */
static int sm_x, sm_y;                  /* start menu / dash position */

static void wp_xp(void) {
    for (int y = 0; y < H; y++) {
        u32 sky = mix(RGB(30, 94, 214), RGB(176, 214, 250), y > 330 ? 330 : y, 330);
        for (int x = 0; x < W; x++) {
            int dx = x - 260;
            int hill = 335 + ((dx * dx) >> 9) / 2;
            if (hill > 470) hill = 470;
            u32 c = sky;
            if (y >= hill) c = mix(RGB(96, 176, 52), RGB(30, 110, 24), y - hill > 140 ? 140 : y - hill, 140);
            bgimg[y * W + x] = c;
        }
    }
}
static void wp_unity(void) {            /* aubergine with a warm glow */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            u32 c = mix(RGB(52, 10, 40), RGB(118, 36, 68), y, H - 1);
            int dx = x - 540, dy = y - 450, d2 = dx * dx + dy * dy;
            if (d2 < 90000) c = mix(c, RGB(244, 120, 44), ((90000 - d2) / 352) * 3 / 4, 255);
            dx = x - 90; dy = y - 60; d2 = dx * dx + dy * dy;
            if (d2 < 40000) c = mix(c, RGB(160, 70, 120), ((40000 - d2) / 157) / 3, 255);
            bgimg[y * W + x] = c;
        }
}
static void wp_kde(void) {              /* deep blue with a bright burst */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            u32 c = mix(RGB(8, 34, 104), RGB(72, 142, 216), y, H - 1);
            int dx = x - 470, dy = y - 110, d2 = dx * dx + dy * dy;
            if (d2 < 160000) c = mix(c, RGB(200, 230, 255), ((160000 - d2) / 628) / 2, 255);
            bgimg[y * W + x] = c;
        }
}
static void build_wallpaper(void) {
    if (th_id == TH_UNITY) wp_unity();
    else if (th_id == TH_KDE) wp_kde();
    else wp_xp();
}

static void clamp_win(struct win *w) {
    if (w->y < area_y0) w->y = area_y0;
    if (w->y > area_y1 - 26) w->y = area_y1 - 26;
    if (w->x < area_x0 + 80 - w->w) w->x = area_x0 + 80 - w->w;
    if (w->x > W - 80) w->x = W - 80;
}
static int start_open;
#define SM_W 200
#define SM_ITEM 24
#define SM_HEAD 30
#define SM_N (N_APPS + 2)
#define SM_H (SM_HEAD + SM_N * SM_ITEM + 6)

static void theme_apply(int id) {
    if (id < 0 || id >= N_THEMES) id = 0;
    th_id = id; th = &themes[id];
    if (id == TH_UNITY) { area_x0 = LAUNCH_W; area_y0 = UBAR_H; area_y1 = H; sm_x = LAUNCH_W; sm_y = UBAR_H; }
    else { area_x0 = 0; area_y0 = 0; area_y1 = H - TB_H; sm_x = 2; sm_y = H - TB_H - SM_H; }
    start_open = 0;
    build_wallpaper();
    for (int a = 0; a < N_APPS; a++) clamp_win(&wins[a]);
}

static const char *icon_label[11] = { "Terminal", "Notepad", "Paint", "Files", "Clock", "About", "Install", "Settings", "Tasks", "Calc", "Reboot" };
#define N_ICONS 11
static int icon_x(int i) { return area_x0 + 14 + (i / 4) * 84; }
static int icon_y(int i) { return area_y0 + 12 + (i % 4) * 82; }
static int icon_sel = -1;
static u32 icon_tick; static int icon_last = -1;

/* icon ids 0..8 are the app ids, 9 = reboot. 32x32 at x,y */
static void draw_icon(int i, int x, int y) {
    switch (i) {
    case 0: rect(x, y + 2, 32, 28, RGB(60, 60, 70)); rect(x + 2, y + 6, 28, 22, C_BLACK); draw_str(x + 5, y + 12, ">_", RGB(80, 255, 80)); break;
    case 1: rect(x + 4, y, 24, 32, C_LIGHT); rect(x + 4, y, 24, 5, RGB(60, 110, 220)); for (int k = 0; k < 4; k++) rect(x + 8, y + 10 + k * 5, 16, 1, C_DARK); break;
    case 2: fill_circle(x + 16, y + 16, 15, RGB(220, 190, 140)); fill_circle(x + 10, y + 11, 3, RGB(220, 30, 30)); fill_circle(x + 19, y + 9, 3, RGB(30, 90, 220)); fill_circle(x + 23, y + 17, 3, RGB(240, 210, 30)); fill_circle(x + 15, y + 22, 3, RGB(30, 170, 40)); break;
    case 3: rect(x, y + 8, 14, 5, RGB(230, 190, 60)); rect(x, y + 12, 32, 18, RGB(250, 210, 80)); rect(x, y + 12, 32, 2, RGB(255, 235, 140)); break;
    case 4: fill_circle(x + 16, y + 16, 15, C_DARK); fill_circle(x + 16, y + 16, 13, C_LIGHT); line(x + 16, y + 16, x + 16, y + 7, C_BLACK); line(x + 16, y + 16, x + 23, y + 18, C_BLACK); break;
    case 5: fill_circle(x + 16, y + 16, 15, RGB(40, 100, 220)); draw_str_s(x + 11, y + 9, "i", C_LIGHT, 2); break;
    case 6: rect(x, y + 6, 32, 20, RGB(150, 150, 160)); rect(x + 3, y + 18, 26, 4, RGB(60, 60, 70)); rect(x + 24, y + 11, 5, 3, RGB(60, 230, 60)); break;
    case 7:  /* settings: gear */
        for (int k = 0; k < 8; k++) {
            int tx = x + 16 + sin60[k * 7 % 60] * 13 / 256, ty = y + 16 - sin60[(k * 7 + 15) % 60] * 13 / 256;
            rect(tx - 3, ty - 3, 6, 6, RGB(120, 126, 140));
        }
        fill_circle(x + 16, y + 16, 11, RGB(140, 146, 160)); fill_circle(x + 16, y + 16, 5, RGB(236, 233, 216)); break;
    case 8:  /* task manager: window with bars */
        rect(x, y + 2, 32, 28, C_LIGHT); frame(x, y + 2, 32, 28, C_DARK); rect(x + 1, y + 3, 30, 4, RGB(60, 110, 220));
        rect(x + 5, y + 20, 5, 7, RGB(60, 170, 70)); rect(x + 13, y + 14, 5, 13, RGB(60, 170, 70)); rect(x + 21, y + 17, 5, 10, RGB(220, 160, 40)); break;
    case 9:  /* calculator */
        rect(x + 3, y, 26, 32, RGB(120, 128, 142)); rect(x + 5, y + 3, 22, 8, RGB(150, 220, 150));
        for (int k = 0; k < 9; k++) { rect(x + 6 + (k % 3) * 7, y + 14 + (k / 3) * 6, 5, 4, k == 8 ? RGB(60, 130, 245) : RGB(225, 228, 235)); }
        break;
    case 10: fill_circle(x + 16, y + 16, 15, RGB(210, 60, 30)); fill_circle(x + 16, y + 16, 8, RGB(255, 255, 255)); fill_circle(x + 16, y + 16, 6, RGB(210, 60, 30)); rect(x + 14, y + 3, 4, 14, C_LIGHT); break;
    }
}
static void draw_desktop(void) {
    memcpy(back, bgimg, sizeof back);
    if (!show_icons) return;
    for (int i = 0; i < N_ICONS; i++) {
        int ix = icon_x(i), iy = icon_y(i);
        draw_icon(i, ix + 16, iy);
        int tw = slen(icon_label[i]) * 8, tx = ix + 32 - tw / 2, ty = iy + 38;
        if (i == icon_sel) rect(tx - 2, ty - 2, tw + 4, 12, th_id == TH_UNITY ? RGB(221, 72, 20) : RGB(49, 106, 197));
        draw_str(tx + 1, ty + 1, icon_label[i], C_BLACK);
        draw_str(tx, ty, icon_label[i], C_LIGHT);
    }
}

/* ---- window decoration ---- */
static void close_rect(int a, int *x, int *y, int *w, int *h) {
    struct win *wn = &wins[a];
    if (th_id == TH_UNITY) { *x = wn->x + 9; *y = wn->y + 7; *w = 16; *h = 16; }
    else { *x = wn->x + wn->w - 24; *y = wn->y + 6; *w = 18; *h = 18; }
}
static void min_rect(int a, int *x, int *y, int *w, int *h) {
    struct win *wn = &wins[a];
    if (th_id == TH_UNITY) { *x = wn->x + 29; *y = wn->y + 7; *w = 16; *h = 16; }
    else { *x = wn->x + wn->w - 46; *y = wn->y + 6; *w = 18; *h = 18; }
}
static void draw_window(int a) {
    struct win *w = &wins[a];
    int focus = top_app() == a;
    rect(w->x, w->y, w->w, w->h, focus ? th->frame_a : th->frame_i);
    gradient_v(w->x + 3, w->y + 3, w->w - 6, TITLE_H, focus ? th->ta1 : th->ti1, focus ? th->ta2 : th->ti2);
    u32 tc = focus ? th->txt_a : th->txt_i;
    int bx, by, bw, bh;
    close_rect(a, &bx, &by, &bw, &bh);
    if (th_id == TH_UNITY) {
        draw_str(w->x + 54, w->y + 11, w->title, tc);
        fill_circle(bx + 8, by + 8, 7, focus ? RGB(221, 72, 20) : RGB(110, 108, 100));
        line(bx + 5, by + 5, bx + 11, by + 11, C_LIGHT); line(bx + 11, by + 5, bx + 5, by + 11, C_LIGHT);
        min_rect(a, &bx, &by, &bw, &bh);
        fill_circle(bx + 8, by + 8, 7, RGB(110, 108, 100));
        rect(bx + 4, by + 9, 8, 2, C_LIGHT);
    } else if (th_id == TH_KDE) {
        draw_str(w->x + 9, w->y + 11, w->title, tc);
        rect(bx, by, bw, bh, RGB(228, 232, 240)); frame(bx, by, bw, bh, th->frame_a);
        line(bx + 5, by + 5, bx + 12, by + 12, RGB(150, 30, 30)); line(bx + 12, by + 5, bx + 5, by + 12, RGB(150, 30, 30));
        line(bx + 6, by + 5, bx + 13, by + 12, RGB(150, 30, 30)); line(bx + 13, by + 5, bx + 6, by + 12, RGB(150, 30, 30));
        min_rect(a, &bx, &by, &bw, &bh);
        rect(bx, by, bw, bh, RGB(228, 232, 240)); frame(bx, by, bw, bh, th->frame_a);
        rect(bx + 4, by + 11, 9, 3, RGB(40, 60, 110));
    } else {
        draw_str(w->x + 9, w->y + 11, w->title, tc);
        rect(bx, by, bw, bh, RGB(217, 83, 58)); frame(bx, by, bw, bh, C_LIGHT);
        line(bx + 5, by + 5, bx + 12, by + 12, C_LIGHT); line(bx + 12, by + 5, bx + 5, by + 12, C_LIGHT);
        line(bx + 6, by + 5, bx + 13, by + 12, C_LIGHT); line(bx + 13, by + 5, bx + 6, by + 12, C_LIGHT);
        min_rect(a, &bx, &by, &bw, &bh);
        rect(bx, by, bw, bh, RGB(50, 110, 230)); frame(bx, by, bw, bh, C_LIGHT);
        rect(bx + 4, by + 11, 8, 3, C_LIGHT);
    }
    app_draw(a, CX(a), CY(a), CW(a), CH(a));
}

/* ---- panels ---- */
static struct rtc cached_rtc; static u32 rtc_tick = 0xFFFFFFFF;
static int tb_count(void) { int n = 0; for (int a = 0; a < N_APPS; a++) n += wins[a].open ? 1 : 0; return n; }
static int tb_bw(void) {
    int n = tb_count(), avail = W - TB_X0 - 70;
    if (n <= 0) return 104;
    int bw = avail / n - 4;
    return bw > 104 ? 104 : bw;
}
static void clock_refresh(void) {
    if (rtc_tick == 0xFFFFFFFF || ticks - rtc_tick >= 100) { rtc_read(&cached_rtc); rtc_tick = ticks; }
}
static void bar_title(const char *t, int bw, char *out) {
    int n = bw / 8 - 1; if (n > 20) n = 20;
    kstrcpy(out, t, n > 1 ? n : 2);
}
static void bar_xp(void) {
    gradient_v(0, H - TB_H, W, TB_H, RGB(60, 130, 245), RGB(30, 88, 220));
    rect(0, H - TB_H, W, 1, RGB(120, 170, 255));
    gradient_v(0, H - TB_H, START_W, TB_H, RGB(74, 170, 74), RGB(44, 130, 44));
    rect(0, H - TB_H, START_W, 1, RGB(140, 220, 140));
    rect(2, H - TB_H + 2, 32, 24, C_LIGHT);
    blit_argb(4, H - TB_H + 3, logoflag_W, logoflag_H, logoflag);
    draw_str(40, H - TB_H + 10, "start", C_LIGHT);
    int bx = TB_X0, bw = tb_bw();
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        int active = top_app() == a;
        gradient_v(bx, H - TB_H + 3, bw, TB_H - 6, active ? RGB(30, 80, 190) : RGB(70, 140, 250), active ? RGB(40, 100, 220) : RGB(50, 110, 235));
        frame(bx, H - TB_H + 3, bw, TB_H - 6, RGB(20, 60, 160));
        char b[24]; bar_title(wins[a].title, bw, b);
        draw_str(bx + 6, H - TB_H + 10, b, C_LIGHT);
        bx += bw + 4;
    }
    clock_refresh();
    rect(W - 64, H - TB_H, 64, TB_H, RGB(22, 120, 232));
    drawf(W - 52, H - TB_H + 10, C_LIGHT, "%02d:%02d", cached_rtc.h, cached_rtc.m);
}
static void bar_kde(void) {
    gradient_v(0, H - TB_H, W, TB_H, RGB(214, 220, 232), RGB(158, 168, 190));
    rect(0, H - TB_H, W, 1, C_LIGHT);
    gradient_v(0, H - TB_H, START_W, TB_H, RGB(100, 150, 218), RGB(52, 102, 182));
    rect(START_W, H - TB_H, 1, TB_H, RGB(60, 96, 160));
    fill_circle(16, H - TB_H / 2, 10, C_LIGHT);
    draw_str(12, H - TB_H / 2 - 4, "K", RGB(40, 90, 180));
    draw_str(34, H - TB_H + 10, "Menu", C_LIGHT);
    int bx = TB_X0, bw = tb_bw();
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        int active = top_app() == a;
        if (active) { rect(bx, H - TB_H + 3, bw, TB_H - 6, RGB(190, 202, 226)); frame(bx, H - TB_H + 3, bw, TB_H - 6, RGB(60, 96, 160)); }
        else bevel(bx, H - TB_H + 3, bw, TB_H - 6, 0);
        char b[24]; bar_title(wins[a].title, bw, b);
        draw_str(bx + 6, H - TB_H + 10, b, RGB(20, 24, 40));
        bx += bw + 4;
    }
    clock_refresh();
    bevel(W - 64, H - TB_H + 3, 62, TB_H - 6, 1);
    drawf(W - 52, H - TB_H + 10, RGB(20, 24, 40), "%02d:%02d", cached_rtc.h, cached_rtc.m);
}
static void bar_unity(void) {
    /* left launcher */
    rect(0, UBAR_H, LAUNCH_W, H - UBAR_H, RGB(48, 42, 40));
    rect(LAUNCH_W - 1, UBAR_H, 1, H - UBAR_H, RGB(24, 18, 20));
    gradient_v(0, UBAR_H, LAUNCH_W - 1, 52, RGB(240, 110, 50), RGB(200, 60, 20));
    fill_circle(25, UBAR_H + 26, 15, C_LIGHT);
    fill_circle(25, UBAR_H + 26, 10, RGB(220, 80, 30));
    fill_circle(25, UBAR_H + 26, 5, C_LIGHT);
    for (int i = 0; i < N_APPS; i++) {
        int ty = LAUNCH_TOP + i * LAUNCH_SLOT;
        if (wins[i].open) {
            rect(2, ty + 1, LAUNCH_W - 4, LAUNCH_SLOT - 2, top_app() == i ? RGB(100, 92, 86) : RGB(72, 66, 62));
            rect(0, ty + 14, 3, 10, C_LIGHT);
        }
        draw_icon(i, 10, ty + 4);
    }
    /* top panel */
    gradient_v(0, 0, W, UBAR_H, RGB(70, 69, 65), RGB(40, 39, 36));
    rect(0, UBAR_H - 1, W, 1, RGB(20, 18, 16));
    int a = top_app();
    draw_str(LAUNCH_W + 10, 8, a >= 0 ? wins[a].title : "LeactedOS", RGB(235, 232, 226));
    clock_refresh();
    drawf(W - 52, 8, RGB(235, 232, 226), "%02d:%02d", cached_rtc.h, cached_rtc.m);
    rect(W - 66, 6, 6, 12, RGB(190, 186, 178)); rect(W - 74, 10, 4, 8, RGB(190, 186, 178));
}
static void draw_bar(void) {
    if (th_id == TH_UNITY) bar_unity();
    else if (th_id == TH_KDE) bar_kde();
    else bar_xp();
}

static void draw_startmenu(void) {
    rect(sm_x, sm_y, SM_W, SM_H, th->menu_bg); frame(sm_x, sm_y, SM_W, SM_H, th->menu_edge);
    gradient_v(sm_x + 1, sm_y + 1, SM_W - 2, SM_HEAD - 1, th->menu_h1, th->menu_h2);
    rect(sm_x + 6, sm_y + 3, 32, 24, C_LIGHT);
    blit_argb(sm_x + 8, sm_y + 4, logoflag_W, logoflag_H, logoflag);
    draw_str(sm_x + 46, sm_y + 11, th->menu_title, C_LIGHT);
    static const u32 bc[N_APPS] = { RGB(60, 60, 70), RGB(60, 110, 220), RGB(220, 60, 50), RGB(240, 200, 60), RGB(120, 120, 130),
                                    RGB(40, 100, 220), RGB(80, 160, 80), RGB(140, 146, 160), RGB(60, 170, 70), RGB(90, 100, 120) };
    for (int i = 0; i < SM_N; i++) {
        int iy = sm_y + SM_HEAD + i * SM_ITEM;
        const char *t = i < N_APPS ? app_names[i] : i == N_APPS ? "Restart" : "Shut Down";
        if (i == N_APPS) rect(sm_x + 8, iy - 1, SM_W - 16, 1, th->menu_edge);
        int hot = inrect(mx, my, sm_x, iy, SM_W, SM_ITEM);
        if (hot) rect(sm_x + 2, iy, SM_W - 4, SM_ITEM, th->menu_hl);
        if (i < N_APPS) { rect(sm_x + 10, iy + 7, 10, 10, bc[i]); frame(sm_x + 10, iy + 7, 10, 10, C_DARK); }
        draw_str(sm_x + 34, iy + 8, t, hot ? th->menu_hl_fg : th->menu_fg);
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
    else if (i == N_APPS) reboot_now();
    else power_off();
}
static void toggle_app(int a) {
    if (!wins[a].open) open_app(a);
    else if (wins[a].min || top_app() != a) raise_app(a);
    else wins[a].min = 1;
}
/* returns 1 if the click landed on a panel */
static int panel_press(int x, int y, int was_open) {
    if (th_id == TH_UNITY) {
        if (y < UBAR_H) return 1;
        if (x < LAUNCH_W) {
            if (y < LAUNCH_TOP) { start_open = !was_open; return 1; }
            int i = (y - LAUNCH_TOP) / LAUNCH_SLOT;
            if (i >= 0 && i < N_APPS) toggle_app(i);
            return 1;
        }
        return 0;
    }
    if (y < H - TB_H) return 0;
    if (x < START_W) { start_open = !was_open; return 1; }
    int bx = TB_X0, bw = tb_bw();
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        if (inrect(x, y, bx, H - TB_H + 3, bw, TB_H - 6)) { toggle_app(a); return 1; }
        bx += bw + 4;
    }
    return 1;
}
static void on_press(int x, int y) {
    int was_open = start_open;
    if (start_open) {
        start_open = 0;
        if (inrect(x, y, sm_x, sm_y + SM_HEAD, SM_W, SM_N * SM_ITEM)) { start_item((y - sm_y - SM_HEAD) / SM_ITEM); return; }
    }
    if (panel_press(x, y, was_open)) return;
    for (int i = nz - 1; i >= 0; i--) {
        int a = zo[i]; struct win *w = &wins[a];
        if (w->min || !inrect(x, y, w->x, w->y, w->w, w->h)) continue;
        raise_app(a);
        int bx, by, bw, bh;
        close_rect(a, &bx, &by, &bw, &bh);
        if (inrect(x, y, bx, by, bw, bh)) { close_app(a); return; }
        min_rect(a, &bx, &by, &bw, &bh);
        if (inrect(x, y, bx, by, bw, bh)) { w->min = 1; return; }
        if (y < w->y + 3 + TITLE_H) { drag_app = a; drag_dx = x - w->x; drag_dy = y - w->y; return; }
        press_app = a;
        app_click(a, x - CX(a), y - CY(a));
        return;
    }
    /* desktop icons */
    icon_sel = -1;
    if (!show_icons) return;
    for (int i = 0; i < N_ICONS; i++) {
        if (inrect(x, y, icon_x(i), icon_y(i), 64, 56)) {
            int dbl = icon_last == i && ticks - icon_tick < 60;
            icon_sel = i; icon_last = i; icon_tick = ticks;
            if (dbl) { if (i < N_APPS) open_app(i); else reboot_now(); }
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
        clamp_win(w);
    } else if (press_app == A_PAINT && wins[A_PAINT].open) {
        paint_drag(x - CX(A_PAINT), y - CY(A_PAINT));
    }
}

/* ------------------------------------------------------------------ */
/* Frame / main                                                        */
/* ------------------------------------------------------------------ */
static void os_draw(void) {
    frame_count++;
    if (ticks - fps_t >= 100) { fps = (frame_count - fps_f) * 100 / (ticks - fps_t); fps_f = frame_count; fps_t = ticks; }
    clip_reset();
    draw_desktop();
    for (int i = 0; i < nz; i++) if (!wins[zo[i]].min) draw_window(zo[i]);
    draw_bar();
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

static void splash(int pct, const char *msg) {
    clip_reset();
    rect(0, 0, W, H, RGB(255, 255, 255));
    blit_argb((W - logo192_W) / 2, 60, logo192_W, logo192_H, logo192);
    draw_str_c(W / 2, 276, "Starting LeactedOS...", RGB(70, 70, 70));
    int bx = (W - 240) / 2;
    rect(bx, 300, 240, 12, RGB(230, 230, 230)); frame(bx, 300, 240, 12, RGB(120, 120, 120));
    rect(bx + 2, 302, 236 * pct / 100, 8, RGB(210, 30, 30));
    draw_str_c(W / 2, 322, msg, RGB(120, 120, 120));
    flip();
}
static void os_init(u32 magic, const u32 *mbi) {
    boot_via_multiboot = magic == 0x2BADB002;
    if (boot_via_multiboot && mbi && (mbi[0] & 1)) mem_kb = 1024 + mbi[2];
#ifdef HOST
    mem_kb = 262144;
#endif
    video_init();
    irq_init();
    splash(5, "Video");
    memset(canvas, 1, sizeof canvas);
    ps2_init();
    splash(30, "Keyboard and mouse");
    ata_identify();
    splash(55, "Disk");
    fs_check();
    fs_load();
    settings_load();
    splash(80, "Files and settings");
    theme_apply(th_id);
    splash(100, "Desktop");
    sleep_ticks(70);
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
