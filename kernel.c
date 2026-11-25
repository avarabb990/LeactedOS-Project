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

#include "sdk/lapp_load.h"      /* LAPP app format: header check + relocation */

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
        int zero = 0, wd = 0, left = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        if (*f == '*') { wd = va_arg(ap, int); if (wd < 0) wd = 0; f++; }
        else while (*f >= '0' && *f <= '9') { wd = wd * 10 + (*f - '0'); f++; }
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
            if (!left) for (int i = l; i < wd; i++) PUT(' ');
            while (*s) { PUT(*s); s++; }
            if (left) for (int i = l; i < wd; i++) PUT(' ');
            break; }
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

/* ------------------------------------------------------------------ */
/* Network card: Realtek RTL8139 (qemu ... -nic user,model=rtl8139)    */
/* ------------------------------------------------------------------ */
static int nic_send(const u8 *f, int n);
static int nic_recv(u8 *b, int max);
static u32 net_ms(void);
static u32 net_rand(void);
#include "net.h"

static void pci_write(int bus, int dev, int fn, int off, u32 v) {
    outl(0xCF8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (off & 0xFC));
    outl(0xCFC, v);
}
static u32 net_rng = 0x2545F491u;
static u32 net_rand(void) {
    net_rng ^= ticks * 2654435761u + 0x9E3779B9u;
    net_rng ^= net_rng << 13; net_rng ^= net_rng >> 17; net_rng ^= net_rng << 5;
    return net_rng;
}
static u32 net_ms(void) { return ticks * 10u; }

#ifdef HOST
static int host_nic_send(const u8 *f, int n);
static int host_nic_recv(u8 *b, int max);
static int host_nic_on;
static int nic_init(void) {
    static const u8 m[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    if (!host_nic_on) return 0;
    memcpy(net_mac, m, 6);
    return 1;
}
static int nic_send(const u8 *f, int n) { return host_nic_send(f, n); }
static int nic_recv(u8 *b, int max) { return host_nic_recv(b, max); }
#else
#define RTL_RXLEN 8192
static u16 rtl_io;
static u8 rtl_rx[RTL_RXLEN + 16 + 2048] __attribute__((aligned(16)));
static u8 rtl_tx[4][2048] __attribute__((aligned(16)));
static u32 rtl_off;
static int rtl_txd, rtl_txbusy[4];
static void rtl_rx_setup(void) {
    outb(rtl_io + 0x37, 0x04);                               /* receiver off, transmitter on */
    outl(rtl_io + 0x30, (u32)(unsigned long)rtl_rx);
    outw(rtl_io + 0x3C, 0);                                  /* no interrupts: we poll */
    outl(rtl_io + 0x44, 0x8F);                               /* accept broadcast, multicast, ours; WRAP */
    outb(rtl_io + 0x37, 0x0C);
    rtl_off = 0;
}
static int nic_init(void) {
    for (int bus = 0; bus < 2; bus++)
        for (int dev = 0; dev < 32; dev++) {
            if (pci_read(bus, dev, 0, 0) != 0x813910ECu) continue;
            u32 bar = pci_read(bus, dev, 0, 0x10);
            if (!(bar & 1)) continue;
            rtl_io = (u16)(bar & ~3u);
            pci_write(bus, dev, 0, 4, pci_read(bus, dev, 0, 4) | 7);      /* I/O, memory, bus master */
            outb(rtl_io + 0x52, 0);                                       /* power on */
            outb(rtl_io + 0x37, 0x10);                                    /* reset */
            for (int i = 0; i < 200000 && (inb(rtl_io + 0x37) & 0x10); i++) { }
            for (int i = 0; i < 6; i++) net_mac[i] = inb(rtl_io + i);
            rtl_rx_setup();
            return 1;
        }
    return 0;
}
static int nic_recv(u8 *buf, int max) {
    if (!rtl_io || (inb(rtl_io + 0x37) & 1)) return 0;               /* receive buffer empty */
    const u8 *h = rtl_rx + rtl_off;
    u32 st = (u32)h[0] | (u32)h[1] << 8 | (u32)h[2] << 16 | (u32)h[3] << 24;
    int len = (int)(st >> 16);
    if (!(st & 1) || len < 18 || len > 1800) { rtl_rx_setup(); return 0; }   /* damaged: start over */
    int n = len - 4;                                                  /* drop the CRC */
    if (n > max) n = max;
    memcpy(buf, h + 4, (size_t)n);
    rtl_off = (rtl_off + (u32)len + 4 + 3) & ~3u;
    outw(rtl_io + 0x38, (u16)(rtl_off - 16));
    rtl_off %= RTL_RXLEN;
    return n;
}
static int nic_send(const u8 *f, int n) {
    if (!rtl_io || n > 1792) return -1;
    int d = rtl_txd;
    if (rtl_txbusy[d]) { for (int i = 0; i < 200000 && !(inl(rtl_io + 0x10 + 4 * d) & 0x2000); i++) { } rtl_txbusy[d] = 0; }
    memcpy(rtl_tx[d], f, (size_t)n);
    outl(rtl_io + 0x20 + 4 * d, (u32)(unsigned long)rtl_tx[d]);
    outl(rtl_io + 0x10 + 4 * d, (u32)(n < 60 ? 60 : n));
    rtl_txbusy[d] = 1;
    rtl_txd = (d + 1) & 3;
    return n;
}
#endif
static void net_init(void) {
    net_have_nic = nic_init();
    http_tcp_up = net_have_nic;
    http_proxy_on = 1;                                      /* QEMU: the PC is 10.0.2.2, see tools/leact-gateway.py */
    if (net_have_nic) dhcp_start();
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
#define K_PGUP  0x105
#define K_PGDN  0x106
#define K_HOME  0x107
#define K_END   0x108
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
        case 0x49: kq_push(K_PGUP); break;
        case 0x51: kq_push(K_PGDN); break;
        case 0x47: kq_push(K_HOME); break;
        case 0x4F: kq_push(K_END); break;
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
/* ATA PIO: up to four IDE drives (C: D: E: F:), LBA28                 */
/* ------------------------------------------------------------------ */
enum { DT_NONE, DT_EMPTY, DT_LEACTED, DT_FAT };
#define N_DRIVES 4
struct drive { int present; u32 sectors; char model[41]; int type; int fatbits; };
static struct drive drv[N_DRIVES];
#ifdef HOST
static u8 *fakedisk[N_DRIVES];
static u32 fakesect[N_DRIVES];
#endif

static int n_drives(void) { int n = 0; for (int d = 0; d < N_DRIVES; d++) n += drv[d].present ? 1 : 0; return n; }
static void dtype_str(int d, char *o, int n) {
    if (!drv[d].present) kstrcpy(o, "-", n);
    else if (drv[d].type == DT_LEACTED) kstrcpy(o, "LeactedFS", n);
    else if (drv[d].type == DT_FAT) ksnprintf(o, n, "FAT%d", drv[d].fatbits);
    else kstrcpy(o, "Unformatted", n);
}

#ifndef HOST
static const u16 ata_base[2] = { 0x1F0, 0x170 };
static const u16 ata_ctl[2] = { 0x3F6, 0x376 };
static int ata_wait_ready(u16 base) {
    for (int i = 0; i < 1000000; i++) { u8 s = inb(base + 7); if (!(s & 0x80)) return s; }
    return -1;
}
static int ata_wait_drq(u16 base) {
    for (int i = 0; i < 1000000; i++) {
        u8 s = inb(base + 7);
        if (s & 0x21) return 0;                  /* ERR or DF */
        if (!(s & 0x80) && (s & 0x08)) return 1;
    }
    return 0;
}
static void ata_delay(int ch) { for (int i = 0; i < 4; i++) inb(ata_ctl[ch]); }
#endif

static void ata_identify_all(void) {
    for (int d = 0; d < N_DRIVES; d++) {
        memset(&drv[d], 0, sizeof drv[d]);
#ifdef HOST
        if (fakedisk[d] && fakesect[d]) {
            drv[d].present = 1; drv[d].sectors = fakesect[d];
            ksnprintf(drv[d].model, sizeof drv[d].model, "HOST DISK %d", d);
        }
#else
        int ch = d >> 1; u16 base = ata_base[ch];
        if (inb(base + 7) == 0xFF) continue;     /* floating bus: no controller here */
        outb(base + 6, 0xA0 | ((d & 1) << 4)); ata_delay(ch);
        outb(base + 2, 0); outb(base + 3, 0); outb(base + 4, 0); outb(base + 5, 0);
        outb(base + 7, 0xEC); ata_delay(ch);
        if (inb(base + 7) == 0) continue;        /* nothing answers */
        if (ata_wait_ready(base) < 0) continue;
        if (inb(base + 7) & 1) continue;         /* error: not an ATA disk */
        if (inb(base + 4) || inb(base + 5)) continue;   /* ATAPI (CD-ROM) signature */
        if (!ata_wait_drq(base)) continue;
        u16 id[256];
        for (int i = 0; i < 256; i++) id[i] = inw(base);
        u32 sec = id[60] | ((u32)id[61] << 16);
        if (!sec) continue;
        drv[d].sectors = sec;
        for (int i = 0; i < 20; i++) { drv[d].model[i * 2] = id[27 + i] >> 8; drv[d].model[i * 2 + 1] = id[27 + i] & 255; }
        drv[d].model[40] = 0;
        for (int i = 39; i >= 0 && drv[d].model[i] == ' '; i--) drv[d].model[i] = 0;
        drv[d].present = 1;
#endif
    }
}

static int ata_read(int d, u32 lba, int count, void *buf) {
    if (d < 0 || d >= N_DRIVES || !drv[d].present || count < 1 || count > 255 || lba + count > drv[d].sectors) return 0;
#ifdef HOST
    memcpy(buf, fakedisk[d] + (size_t)lba * 512, (size_t)count * 512); return 1;
#else
    int ch = d >> 1; u16 base = ata_base[ch];
    u16 *p = buf;
    if (ata_wait_ready(base) < 0) return 0;
    outb(base + 6, 0xE0 | ((d & 1) << 4) | ((lba >> 24) & 0x0F)); ata_delay(ch);
    outb(base + 2, count); outb(base + 3, lba); outb(base + 4, lba >> 8); outb(base + 5, lba >> 16);
    outb(base + 7, 0x20);
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq(base)) return 0;
        for (int i = 0; i < 256; i++) *p++ = inw(base);
    }
    return 1;
#endif
}
#ifdef HOST
static int host_fail_after = -1;                 /* tests: fail every write after this many good ones */
#endif
static int ata_write(int d, u32 lba, int count, const void *buf) {
    if (d < 0 || d >= N_DRIVES || !drv[d].present || count < 1 || count > 255 || lba + count > drv[d].sectors) return 0;
#ifdef HOST
    if (host_fail_after == 0) return 0;
    if (host_fail_after > 0) host_fail_after--;
    memcpy(fakedisk[d] + (size_t)lba * 512, buf, (size_t)count * 512); return 1;
#else
    int ch = d >> 1; u16 base = ata_base[ch];
    const u16 *p = buf;
    if (ata_wait_ready(base) < 0) return 0;
    outb(base + 6, 0xE0 | ((d & 1) << 4) | ((lba >> 24) & 0x0F)); ata_delay(ch);
    outb(base + 2, count); outb(base + 3, lba); outb(base + 4, lba >> 8); outb(base + 5, lba >> 16);
    outb(base + 7, 0x30);
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq(base)) return 0;
        for (int i = 0; i < 256; i++) outw(base, *p++);
    }
    outb(base + 7, 0xE7);                        /* flush cache */
    return ata_wait_ready(base) >= 0;
#endif
}

/* ------------------------------------------------------------------ */
/* FAT12 / FAT16 / FAT32 (read only, 512-byte sectors, long names)     */
/* ------------------------------------------------------------------ */
struct fatvol {
    int drive, type;                              /* type: 12, 16 or 32 */
    u32 base, spc, nfats, fat_lba, fatsz, root_lba, root_sectors, root_cluster, data_lba, clusters;
};
struct dent { char name[44]; u32 size; u32 cluster; u8 isdir; };
#define MAX_DENT 200

static u8 fat_cache[512]; static int fat_cache_drive = -1; static u32 fat_cache_lba;
static const u8 *fat_sector(int d, u32 lba) {
    if (fat_cache_drive == d && fat_cache_lba == lba) return fat_cache;
    if (!ata_read(d, lba, 1, fat_cache)) { fat_cache_drive = -1; return NULL; }
    fat_cache_drive = d; fat_cache_lba = lba;
    return fat_cache;
}
static u32 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static int is_bpb(const u8 *s) {
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (s[0] != 0xEB && s[0] != 0xE9) return 0;
    u32 bps = rd16(s + 11), spc = s[13], rsvd = rd16(s + 14), nf = s[16];
    if (bps != 512 || !spc || (spc & (spc - 1)) || !rsvd || nf < 1 || nf > 2) return 0;
    return 1;
}
static int fat_mount(int d, struct fatvol *v) {
    u8 s[512]; u32 base = 0;
    if (!ata_read(d, 0, 1, s)) return 0;
    if (s[510] != 0x55 || s[511] != 0xAA) return 0;
    if (!is_bpb(s)) {                             /* maybe an MBR: look for a FAT partition */
        int found = 0;
        for (int i = 0; i < 4 && !found; i++) {
            const u8 *e = s + 0x1BE + i * 16;
            u8 t = e[4]; u32 lba = rd32(e + 8);
            if ((t == 1 || t == 4 || t == 6 || t == 0xB || t == 0xC || t == 0xE) && lba > 0) { base = lba; found = 1; }
        }
        if (!found || !ata_read(d, base, 1, s) || !is_bpb(s)) return 0;
    }
    u32 spc = s[13], rsvd = rd16(s + 14), nf = s[16], rootent = rd16(s + 17);
    u32 tot = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32);
    u32 fatsz = rd16(s + 22) ? rd16(s + 22) : rd32(s + 36);
    if (!fatsz || !tot) return 0;
    u32 rootsec = (rootent * 32 + 511) / 512;
    u32 meta = rsvd + nf * fatsz + rootsec;
    if (tot <= meta) return 0;
    u32 clusters = (tot - meta) / spc;
    v->drive = d; v->base = base; v->spc = spc; v->nfats = nf; v->fatsz = fatsz;
    v->fat_lba = base + rsvd; v->root_sectors = rootsec;
    v->root_lba = base + rsvd + nf * fatsz;
    v->data_lba = base + meta; v->clusters = clusters;
    v->type = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
    v->root_cluster = 0;
    if (v->type == 32) { v->root_cluster = rd32(s + 44); if (v->root_cluster < 2) return 0; }
    return 1;
}
/* next cluster in a chain, or 0 at the end / on a bad entry */
static u32 fat_next(const struct fatvol *v, u32 c) {
    u32 off = v->type == 12 ? c + c / 2 : v->type == 16 ? c * 2 : c * 4;
    u32 lba = v->fat_lba + off / 512, o = off % 512, val, eoc;
    const u8 *s = fat_sector(v->drive, lba);
    if (!s) return 0;
    if (v->type == 12) {
        u32 lo = s[o], hi;
        if (o == 511) { const u8 *s2 = fat_sector(v->drive, lba + 1); if (!s2) return 0; hi = s2[0]; }
        else hi = s[o + 1];
        val = lo | (hi << 8);
        val = (c & 1) ? val >> 4 : val & 0x0FFF;
        eoc = 0xFF8;
    } else if (v->type == 16) { val = rd16(s + o); eoc = 0xFFF8; }
    else { val = rd32(s + o) & 0x0FFFFFFF; eoc = 0x0FFFFFF8; }
    if (val < 2 || val >= eoc || val >= v->clusters + 2) return 0;
    return val;
}
/* read the idx-th sector of a directory (cluster 0 = root); 0 when past the end */
static int fat_dir_sector(const struct fatvol *v, u32 cluster, u32 idx, u8 *buf) {
    if (cluster == 0 && v->type != 32) {
        if (idx >= v->root_sectors) return 0;
        return ata_read(v->drive, v->root_lba + idx, 1, buf);
    }
    if (cluster == 0) cluster = v->root_cluster;
    u32 hops = idx / v->spc, c = cluster;
    while (hops--) { c = fat_next(v, c); if (!c) return 0; }
    return ata_read(v->drive, v->data_lba + (c - 2) * v->spc + (idx % v->spc), 1, buf);
}
static int fat_readdir(const struct fatvol *v, u32 cluster, struct dent *out, int max) {
    static const u8 lpos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    u8 s[512]; int n = 0, lfn_ok = 0; u8 lfn_chk = 0; char lfn[64];
    for (u32 idx = 0; idx < 100000; idx++) {
        if (!fat_dir_sector(v, cluster, idx, s)) break;
        for (int o = 0; o < 512; o += 32) {
            const u8 *e = s + o;
            if (e[0] == 0) return n;
            if (e[0] == 0xE5) { lfn_ok = 0; continue; }
            u8 attr = e[11];
            if (attr == 0x0F) {                   /* long-name fragment */
                u8 seq = e[0]; int ord = seq & 0x1F;
                if (seq & 0x40) { memset(lfn, 0, sizeof lfn); lfn_ok = 1; lfn_chk = e[13]; }
                if (!lfn_ok || ord < 1) continue;
                for (int k = 0; k < 13; k++) {
                    u32 cp = rd16(e + lpos[k]);
                    int at = (ord - 1) * 13 + k;
                    if (at < (int)sizeof lfn - 1) lfn[at] = (cp == 0 || cp == 0xFFFF) ? 0 : cp < 128 ? (char)cp : '?';
                }
                continue;
            }
            if (attr & 0x08) { lfn_ok = 0; continue; }    /* volume label */
            char nm[16]; int p = 0;
            for (int i = 0; i < 8 && e[i] != ' '; i++) {
                char c = e[i]; if (i == 0 && (u8)c == 0x05) c = '?';
                nm[p++] = (e[12] & 0x08) ? klower(c) : c;
            }
            if (e[8] != ' ') {
                nm[p++] = '.';
                for (int i = 8; i < 11 && e[i] != ' '; i++) nm[p++] = (e[12] & 0x10) ? klower(e[i]) : e[i];
            }
            nm[p] = 0;
            u8 sum = 0;
            for (int i = 0; i < 11; i++) sum = (u8)(((sum & 1) << 7) + (sum >> 1) + e[i]);
            int use_lfn = lfn_ok && sum == lfn_chk && lfn[0];
            lfn_ok = 0;
            if (!kstrcmp(nm, ".") || !kstrcmp(nm, "..")) continue;
            if (n >= max) return n;
            struct dent *d = &out[n++];
            kstrcpy(d->name, use_lfn ? lfn : nm, sizeof d->name);
            d->size = rd32(e + 28);
            d->cluster = ((u32)rd16(e + 20) << 16) | rd16(e + 26);
            if (v->type != 32) d->cluster &= 0xFFFF;
            d->isdir = (attr & 0x10) ? 1 : 0;
        }
    }
    return n;
}
static u32 fat_read_file(const struct fatvol *v, u32 cluster, u32 size, u8 *buf, u32 maxlen) {
    static u8 cbuf[65536];
    u32 csz = v->spc * 512, got = 0, guard = v->clusters + 2;
    if (size > maxlen) size = maxlen;
    if (csz > sizeof cbuf) return 0;
    while (got < size && cluster >= 2 && guard--) {
        if (!ata_read(v->drive, v->data_lba + (cluster - 2) * v->spc, v->spc, cbuf)) break;
        u32 n = size - got < csz ? size - got : csz;
        memcpy(buf + got, cbuf, n); got += n;
        cluster = fat_next(v, cluster);
    }
    return got;
}

/* ------------------------------------------------------------------ */
/* LeactedFS (lives on one drive: fs_drive)                            */
/*  v2 layout (new disks, up to 64 files of 128 KB)                    */
/*   sector 0      MBR with one partition (type 0xDA, non-FS data)     */
/*   sector 1      superblock "LEACTEDFS" 0x02, total sectors at +0x10 */
/*   sectors 3-7   directory "LDIR" + 64 x { name[24], size, used }    */
/*   sectors 16+   2048 sectors reserved for a boot loader + kernel    */
/*   sector 2064+  file slots of 256 sectors (128 KB) each             */
/*  v1 layout (older disks, still readable): 15 files of 4 KB,         */
/*   directory in sector 2, file slots from sector 8.                  */
/* ------------------------------------------------------------------ */
#define FS_MAX 64
#define FS_SLOT 131072u
#define FS_NAME 24
#define FS2_DIR_LBA 3
#define FS2_DIR_SECS 5
#define FS2_BOOT_LBA 16
#define FS2_BOOT_SECS 2048
#define FS2_DATA_LBA 2064
#define FS2_SLOT_SECS 256
#define FS2_MIN_SECTORS (FS2_DATA_LBA + FS_MAX * FS2_SLOT_SECS)
#define FS1_MAX 15
#define FS1_SLOT 4096u
#define FS1_DATA_LBA 8
struct fent { char name[FS_NAME]; u32 size; u32 used; };
static struct fent fsdir[FS_MAX];
static u8 fsdata[FS_MAX][FS_SLOT];
static int fs_on_disk;                           /* LeactedFS volume present */
static int fs_drive = -1;                        /* which drive holds it */
static int fs_ver = 2;                           /* 1 = old small layout, 2 = current */
static int fs_max_files = FS_MAX;                /* limits of the mounted volume */
static u32 fs_slot_limit = FS_SLOT;

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

static u32 fs_data_lba(int i) { return fs_ver == 1 ? (u32)(FS1_DATA_LBA + i * 8) : (u32)(FS2_DATA_LBA + i * FS2_SLOT_SECS); }
/* read/write many sectors on the LeactedFS drive (ata_* handle at most 255 per call) */
static int fs_io(int wr, u32 lba, u32 secs, void *buf) {
    u8 *p = buf;
    while (secs) {
        int n = secs > 128 ? 128 : (int)secs;
        if (!(wr ? ata_write(fs_drive, lba, n, p) : ata_read(fs_drive, lba, n, p))) return 0;
        lba += n; secs -= n; p += n * 512;
    }
    return 1;
}
static u8 fs_dirbuf[FS2_DIR_SECS * 512];
static int fs_flush_dir(void) {
    if (!fs_on_disk) return 1;
    memset(fs_dirbuf, 0, sizeof fs_dirbuf);
    memcpy(fs_dirbuf, "LDIR", 4);
    if (fs_ver == 1) {
        memcpy(fs_dirbuf + 4, fsdir, FS1_MAX * sizeof(struct fent));
        return ata_write(fs_drive, 2, 1, fs_dirbuf);
    }
    memcpy(fs_dirbuf + 16, fsdir, sizeof fsdir);
    return fs_io(1, FS2_DIR_LBA, FS2_DIR_SECS, fs_dirbuf);
}
static int fs_flush_file(int i) {
    if (!fs_on_disk) return 1;
    u32 secs = (fsdir[i].size + 511) / 512;
    if (secs && !fs_io(1, fs_data_lba(i), secs, fsdata[i])) return 0;
    return fs_flush_dir();
}
/* returns slot or -1 (full) */
static int fs_create(const char *name) {
    int i = fs_find(name);
    if (i >= 0) return i;
    for (i = 0; i < fs_max_files; i++) if (!fsdir[i].used) {
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
    if (len > fs_slot_limit) len = fs_slot_limit;
    if (fsdir[i].size > len) memset(fsdata[i] + len, 0, fsdir[i].size - len);
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
/* 0 = ok, 1 = ok RAM only, -1 = error */
static int fs_rename(int i, const char *name) {
    fs_norm(fsdir[i].name, name);
    if (!fs_on_disk) return 1;
    return fs_flush_dir() ? 0 : -1;
}
static void fs_defaults(void) {
    static const char readme[] =
        "Welcome to LeactedOS XP Edition!\n"
        "Open Terminal and type help.\n"
        "Files live in RAM until you install\n"
        "a disk with the Installer.\n";
    int i = fs_create("README.TXT");
    if (i >= 0) { memcpy(fsdata[i], readme, sizeof readme - 1); fsdir[i].size = sizeof readme - 1; }
}
/* classify one drive: LeactedFS, FAT or unformatted */
static void drive_probe(int d) {
    struct drive *v = &drv[d];
    v->type = DT_NONE; v->fatbits = 0;
    if (!v->present) return;
    v->type = DT_EMPTY;
    u8 sec[512];
    if (ata_read(d, 1, 1, sec) && !memcmp(sec, "LEAC", 4)) { v->type = DT_LEACTED; return; }
    struct fatvol fv;
    if (fat_mount(d, &fv)) { v->type = DT_FAT; v->fatbits = fv.type; }
}
static int fs_check(void) {
    fs_on_disk = 0; fs_drive = -1;
    for (int d = 0; d < N_DRIVES; d++)
        if (drv[d].present && drv[d].type == DT_LEACTED) { fs_on_disk = 1; fs_drive = d; return 1; }
    return 0;
}
static void fs_load(void) {
    memset(fsdir, 0, sizeof fsdir);
    fs_ver = 2; fs_max_files = FS_MAX; fs_slot_limit = FS_SLOT;
    if (fs_on_disk) {
        u8 sb[512];
        int ver = ata_read(fs_drive, 1, 1, sb) ? sb[9] : 0;
        if (ver != 1 && ver != 2) { fs_on_disk = 0; fs_drive = -1; fs_defaults(); return; }   /* unknown (newer?) layout: do not touch it */
        int ok = 0;
        if (ver == 1) {
            fs_ver = 1; fs_max_files = FS1_MAX; fs_slot_limit = FS1_SLOT;
            ok = ata_read(fs_drive, 2, 1, fs_dirbuf) && !memcmp(fs_dirbuf, "LDIR", 4);
            if (ok) memcpy(fsdir, fs_dirbuf + 4, FS1_MAX * sizeof(struct fent));
        } else {
            ok = fs_io(0, FS2_DIR_LBA, FS2_DIR_SECS, fs_dirbuf) && !memcmp(fs_dirbuf, "LDIR", 4);
            if (ok) memcpy(fsdir, fs_dirbuf + 16, sizeof fsdir);
        }
        if (ok) {
            for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) {
                if (i >= fs_max_files) { memset(&fsdir[i], 0, sizeof fsdir[i]); continue; }
                if (fsdir[i].size > fs_slot_limit) fsdir[i].size = fs_slot_limit;
                fsdir[i].name[FS_NAME - 1] = 0;
                u32 secs = (fsdir[i].size + 511) / 512;
                if (secs) fs_io(0, fs_data_lba(i), secs, fsdata[i]);
            }
            return;
        }
        fs_defaults();
        fs_flush_file(0);
        return;
    }
    fs_defaults();
}
static int fs_write_super(int d, u32 sectors) {
    u8 sec[512];
    memset(sec, 0, 512);
    memcpy(sec, "LEACTEDFS\x02", 10);
    u32 v[5] = { sectors, FS_MAX, FS2_SLOT_SECS, FS2_BOOT_LBA, FS2_BOOT_SECS };
    memcpy(sec + 0x10, v, sizeof v);
    return ata_write(d, 1, 1, sec);
}
/* format drive d as LeactedFS (v2) and copy the files that are in RAM onto it */
static int fs_install(int d) {
    if (d < 0 || d >= N_DRIVES || !drv[d].present || drv[d].sectors < FS2_MIN_SECTORS) return 0;
    u8 sec[512];
    memset(sec, 0, 512);
    /* partition entry: type 0xDA, from sector 16 to the end of the disk */
    sec[0x1BE] = 0x00; sec[0x1BE + 4] = 0xDA;
    u32 start = FS2_BOOT_LBA, len = drv[d].sectors - FS2_BOOT_LBA;
    memcpy(sec + 0x1BE + 8, &start, 4); memcpy(sec + 0x1BE + 12, &len, 4);
    sec[510] = 0x55; sec[511] = 0xAA;
    fat_cache_drive = -1;
    if (!ata_write(d, 0, 1, sec)) return 0;
    if (!fs_write_super(d, drv[d].sectors)) return 0;
    fs_on_disk = 1; fs_drive = d; fs_ver = 2; fs_max_files = FS_MAX; fs_slot_limit = FS_SLOT;
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used && !fs_flush_file(i)) { fs_on_disk = 0; fs_drive = -1; return 0; }
    if (!fs_flush_dir()) { fs_on_disk = 0; fs_drive = -1; return 0; }
    drive_probe(d);
    return 1;
}
/* Move a mounted v1 volume to the v2 layout. Files are written first, the
 * superblock last, so a power cut leaves the old volume intact.
 * Returns 1 = upgraded, 0 = failed / too small, -1 = nothing to do. */
static int fs_upgrade(void) {
    if (!fs_on_disk || fs_ver != 1) return -1;
    if (drv[fs_drive].sectors < FS2_MIN_SECTORS) return 0;
    fs_ver = 2; fs_max_files = FS_MAX; fs_slot_limit = FS_SLOT;
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) {
        u32 secs = (fsdir[i].size + 511) / 512;
        if (secs && !fs_io(1, fs_data_lba(i), secs, fsdata[i])) goto fail;
    }
    if (!fs_flush_dir()) goto fail;
    if (!fs_write_super(fs_drive, drv[fs_drive].sectors)) goto fail;
    return 1;
fail:
    fs_ver = 1; fs_max_files = FS1_MAX; fs_slot_limit = FS1_SLOT;
    return 0;
}

/* ------------------------------------------------------------------ */
/* File references (what the File Manager hands to viewers)            */
/* ------------------------------------------------------------------ */
struct fref { int kind; int drive; u32 cluster, size; char name[48]; };   /* kind: 0 none, 1 LeactedFS slot, 2 FAT */
static u32 fref_load(const struct fref *f, u8 *buf, u32 maxlen) {
    if (f->kind == 1) {
        int i = (int)f->cluster;
        if (i < 0 || i >= FS_MAX || !fsdir[i].used) return 0;
        u32 n = fsdir[i].size < maxlen ? fsdir[i].size : maxlen;
        memcpy(buf, fsdata[i], n);
        return n;
    }
    if (f->kind == 2) {
        struct fatvol v;
        if (!fat_mount(f->drive, &v)) return 0;
        return fat_read_file(&v, f->cluster, f->size, buf, maxlen);
    }
    return 0;
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
enum { A_TERM, A_NOTE, A_PAINT, A_FILES, A_CLOCK, A_ABOUT, A_INSTALL, A_SETTINGS, A_TASK, A_CALC, A_BROWSER, A_OPENWITH, A_HEX, A_IMG, A_EXT0, A_EXT1, A_EXT2, A_EXT3, N_APPS };
#define N_EXT 4                  /* loaded apps (.lapp) that can run at once */
#define N_PIN A_OPENWITH         /* apps shown in menus, launcher and on the desktop */
static void theme_apply(int id);
static void settings_save(void);

struct win { int open, min, x, y, w, h; const char *title; };
static struct win wins[N_APPS] = {
    [A_TERM]    = { 0, 0, 90, 24, 468, 340, "Terminal" },
    [A_NOTE]    = { 0, 0, 150, 50, 420, 300, "Notepad" },
    [A_PAINT]   = { 0, 0, 60, 20, 464, 372, "Paint" },
    [A_FILES]   = { 0, 0, 50, 30, 540, 360, "File Manager" },
    [A_CLOCK]   = { 0, 0, 330, 40, 260, 310, "Clock" },
    [A_ABOUT]   = { 0, 0, 200, 60, 340, 288, "About LeactedOS" },
    [A_INSTALL] = { 0, 0, 140, 70, 380, 250, "LeactedOS Installer" },
    [A_SETTINGS] = { 0, 0, 120, 50, 380, 300, "Settings" },
    [A_TASK]    = { 0, 0, 100, 30, 380, 336, "Task Manager" },
    [A_CALC]    = { 0, 0, 260, 40, 232, 300, "Calculator" },
    [A_BROWSER] = { 0, 0, 20, 8, 600, 412, "Browser" },
    [A_OPENWITH] = { 0, 0, 170, 70, 300, 310, "Open With" },
    [A_HEX]     = { 0, 0, 120, 50, 380, 330, "Hex Viewer" },
    [A_IMG]     = { 0, 0, 90, 40, 440, 360, "Image Viewer" },
};
static const char *app_names[N_APPS] = { "Terminal", "Notepad", "Paint", "File Manager", "Clock", "About", "Installer", "Settings", "Task Manager", "Calculator", "Browser", "Open With", "Hex Viewer", "Image Viewer" };
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
static void xapp_on_close(int a);
static int br_wait;
static void http_cancel(void);
static void close_app(int a) {
    if (a == A_BROWSER && br_wait) { http_cancel(); br_wait = 0; }
    if (a >= A_EXT0 && wins[a].open) xapp_on_close(a);
    wins[a].open = 0; wins[a].min = 0; z_remove(a);
}

/* ------------------------------------------------------------------ */
/* Loadable apps (.lapp): slots, kernel API, event dispatch            */
/* ------------------------------------------------------------------ */
#define APP_FILE_MAX (LAPP_HDR_SIZE + LAPP_MAX_MEM + LAPP_MAX_RELOC * 4u)
typedef int (*lapp_entry_fn)(const struct leact_api *, const struct leact_event *);
struct xapp { int used, want_close; u32 entry; u8 *mem; char title[25]; lapp_entry_fn native; };
static struct xapp xapp[N_EXT];
static u8 app_mem[N_EXT][LAPP_MAX_MEM] __attribute__((aligned(16)));
static u8 app_tmp[APP_FILE_MAX];
static int xdraw, xox, xoy, xcur = -1;        /* drawing allowed / origin / running slot */
static const char *lapp_run_fref(const struct fref *f);
static void tputc(char c);

static u32 xrng = 2463534242u; static int xrng_seeded;
static u32 x_rand(void) {
    if (!xrng_seeded) { xrng ^= ticks * 2654435761u; if (!xrng) xrng = 1; xrng_seeded = 1; }
    xrng ^= xrng << 13; xrng ^= xrng >> 17; xrng ^= xrng << 5;
    return xrng;
}
static void x_rect(int x, int y, int w, int h, u32 c) { if (xdraw) rect(xox + x, xoy + y, w, h, c); }
static void x_frame(int x, int y, int w, int h, u32 c) { if (xdraw) frame(xox + x, xoy + y, w, h, c); }
static void x_line(int x0, int y0, int x1, int y1, u32 c) {
    if (xdraw && x0 > -4000 && x0 < 4000 && y0 > -4000 && y0 < 4000 && x1 > -4000 && x1 < 4000 && y1 > -4000 && y1 < 4000)
        line(xox + x0, xoy + y0, xox + x1, xoy + y1, c);
}
static void x_circle(int cx, int cy, int r, u32 c) { if (xdraw && r >= 0 && r <= 300) fill_circle(xox + cx, xoy + cy, r, c); }
static void x_pixel(int x, int y, u32 c) { if (xdraw) pset(xox + x, xoy + y, c); }
static int x_text(int x, int y, const char *s, u32 c) { if (!xdraw) return x; return draw_str_s(xox + x, xoy + y, s, c, 1) - xox; }
static int x_text_scaled(int x, int y, const char *s, u32 c, int sc) {
    if (!xdraw) return x;
    if (sc < 1) sc = 1;
    if (sc > 8) sc = 8;
    return draw_str_s(xox + x, xoy + y, s, c, sc) - xox;
}
static void x_button(int x, int y, int w, int h, const char *l, int pr) { if (xdraw) button(xox + x, xoy + y, w, h, l, pr); }
static void x_blit(int x, int y, int w, int h, const u32 *src) {
    if (xdraw && src && w > 0 && h > 0 && w <= 640 && h <= 480) blit_argb(xox + x, xoy + y, w, h, src);
}
static u32 x_ticks(void) { return ticks; }
static void x_beep(u32 hz, u32 ms) { beep(hz, ms > 1000 ? 1000 : ms); }
static void x_set_title(const char *t) { if (xcur >= 0 && t) kstrcpy(xapp[xcur].title, t, sizeof xapp[xcur].title); }
static void x_close(void) { if (xcur >= 0) xapp[xcur].want_close = 1; }
static void x_log(const char *t) { if (t) for (int i = 0; t[i] && i < 200; i++) tputc(t[i]); }
static int x_file_read(const char *name, void *buf, u32 max) {
    int i = name ? fs_find(name) : -1;
    if (i < 0 || !buf) return -1;
    u32 n = fsdir[i].size < max ? fsdir[i].size : max;
    memcpy(buf, fsdata[i], n);
    return (int)n;
}
static int x_file_write(const char *name, const void *buf, u32 len) {
    if (!name || !*name || (len && !buf) || len > fs_slot_limit) return -1;
    int i = fs_create(name);
    if (i < 0) return -1;
    return fs_store(i, buf, len) < 0 ? -1 : 0;
}
static int x_file_delete(const char *name) {
    int i = name ? fs_find(name) : -1;
    if (i < 0) return -1;
    return fs_delete(i) < 0 ? -1 : 0;
}
static int x_file_name(int idx, char *out, u32 max) {
    int k = 0;
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) {
        if (k++ != idx) continue;
        if (out && max) kstrcpy(out, fsdir[i].name, (int)max);
        return (int)fsdir[i].size;
    }
    return -1;
}
static struct leact_api xapi = {
    .size = sizeof(struct leact_api), .version = LAPP_API_VERSION,
    .rect = x_rect, .frame = x_frame, .line = x_line, .circle = x_circle, .pixel = x_pixel,
    .text = x_text, .text_scaled = x_text_scaled, .button = x_button, .blit = x_blit,
    .ticks = x_ticks, .rand = x_rand, .beep = x_beep, .set_title = x_set_title, .close = x_close, .log = x_log,
    .file_read = x_file_read, .file_write = x_file_write, .file_delete = x_file_delete, .file_name = x_file_name,
};

/* deliver one event to the app living in window slot a */
static int xcall(int a, int type, int x, int y, int key, int btn) {
    int s = a - A_EXT0;
    if (s < 0 || s >= N_EXT || !xapp[s].used) return 0;
    struct xapp *p = &xapp[s];
    struct leact_event ev = { type, x, y, key, btn, CW(a), CH(a) };
    xapi.c_face = C_FACE; xapi.c_text = C_TEXT; xapi.c_light = C_LIGHT; xapi.c_dark = C_DARK;
    int prev = xcur, r = 0;
    xcur = s;
    if (p->native) r = p->native(&xapi, &ev);
#ifndef HOST
    else r = ((lapp_entry_fn)(uintptr_t)(p->mem + p->entry))(&xapi, &ev);
#endif
    xcur = prev;
    return r;
}
static void xapp_on_close(int a) {
    int s = a - A_EXT0;
    if (s < 0 || s >= N_EXT || !xapp[s].used) return;
    xcall(a, EV_CLOSE, 0, 0, 0, 0);
    xapp[s].used = 0; xapp[s].want_close = 0; xapp[s].native = 0;
}
static void xapp_reap(void) {
    for (int s = 0; s < N_EXT; s++) if (xapp[s].used && xapp[s].want_close) close_app(A_EXT0 + s);
}
static void xapp_tick(void) {
    for (int s = 0; s < N_EXT; s++) if (xapp[s].used && wins[A_EXT0 + s].open) xcall(A_EXT0 + s, EV_TICK, 0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* Terminal                                                            */
/* ------------------------------------------------------------------ */
#define TC 56
#define TR 29                    /* visible rows */
#define TB 300                   /* rows kept (scrollback) */
#define ILMAX 96
#define NHIST 32
static char tg[TB][TC + 1];
static int tcol, trow, tscroll;
static char iline[ILMAX + 1]; static int ilen;
static char hist[NHIST][ILMAX + 1]; static int nhist, hpos;
static int is_root;

static void tclear(void) { memset(tg, 0, sizeof tg); tcol = 0; trow = 0; tscroll = 0; }
static void tnl(void) {
    tcol = 0; trow++; tscroll = 0;
    if (trow >= TB) { memmove(tg[0], tg[1], (TB - 1) * (TC + 1)); memset(tg[TB - 1], 0, TC + 1); trow = TB - 1; }
}
static void tputc(char c) {
    if (c == '\n') { tnl(); return; }
    if (c == '\r') return;
    if (c == '\t') c = ' ';
    if ((u8)c < 32 || (u8)c > 126) c = '.';
    if (tcol >= TC) tnl();
    tg[trow][tcol++] = c; tg[trow][tcol] = 0;
}
static void tputs(const char *s) { while (*s) tputc(*s++); }
static void tprintf(const char *f, ...) {
    char b[160]; va_list ap; va_start(ap, f); kvsnprintf(b, sizeof b, f, ap); va_end(ap); tputs(b);
}

/* ------------------------------------------------------------------ */
/* Paths for the terminal: drives C:-F:, folders on FAT, LeactedFS     */
/*  drive -1 = the mounted LeactedFS volume (or the RAM disk, R:)      */
/* ------------------------------------------------------------------ */
#define FM_RAM 4                        /* pseudo drive R: (files held in RAM) */
#define TP_DEPTH 12
struct tpath { int drive, depth; u32 stack[TP_DEPTH + 1]; char rel[64]; };
static struct tpath tcwd = { -1, 0, { 0 }, "" };
static struct dent tp_ents[MAX_DENT];
static u8 tbuf[FS_SLOT + 1];

static char tp_letter(int drive) { return drive < 0 ? (fs_on_disk ? (char)('C' + fs_drive) : 'R') : (char)('C' + drive); }
static void tp_root(struct tpath *p, int drive) { p->drive = drive; p->depth = 0; p->stack[0] = 0; p->rel[0] = 0; }
static void tp_show(const struct tpath *p, char *out, int max) { ksnprintf(out, max, "%c:\\%s", tp_letter(p->drive), p->rel); }
static int tp_push(struct tpath *p, const char *name, u32 cluster) {
    int rl = slen(p->rel), nl = slen(name);
    if (p->depth >= TP_DEPTH || rl + nl + 2 >= (int)sizeof p->rel) return 0;
    p->stack[++p->depth] = cluster;
    memcpy(p->rel + rl, name, nl); p->rel[rl + nl] = '\\'; p->rel[rl + nl + 1] = 0;
    return 1;
}
static void tp_pop(struct tpath *p) {
    if (!p->depth) return;
    p->depth--;
    int rl = slen(p->rel) - 1;                    /* drop the trailing backslash, then the last name */
    while (rl > 0 && p->rel[rl - 1] != '\\') rl--;
    p->rel[rl > 0 ? rl : 0] = 0;
}
static int tp_ieq(const char *a, const char *b) {
    for (; *a && kupper(*a) == kupper(*b); a++, b++) ;
    return *a == *b;
}
static int tp_less(const struct dent *a, const struct dent *b) {      /* folders first, then by name */
    if (a->isdir != b->isdir) return a->isdir;
    for (int i = 0;; i++) {
        char x = kupper(a->name[i]), y = kupper(b->name[i]);
        if (x != y) return x < y;
        if (!x) return 0;
    }
}
static void tp_sort(struct dent *e, int n) {
    for (int i = 1; i < n; i++) {
        struct dent t = e[i]; int j = i - 1;
        while (j >= 0 && tp_less(&t, &e[j])) { e[j + 1] = e[j]; j--; }
        e[j + 1] = t;
    }
}
/* entries of a folder; -1 if the drive cannot be read */
static int tp_list(const struct tpath *p, struct dent *out, int max) {
    int n = 0;
    if (p->drive < 0) {
        for (int i = 0; i < FS_MAX && n < max; i++) if (fsdir[i].used) {
            kstrcpy(out[n].name, fsdir[i].name, sizeof out[n].name);
            out[n].size = fsdir[i].size; out[n].cluster = (u32)i; out[n].isdir = 0; n++;
        }
        return n;
    }
    if (p->drive >= N_DRIVES || !drv[p->drive].present || drv[p->drive].type != DT_FAT) return -1;
    struct fatvol v;
    if (!fat_mount(p->drive, &v)) return -1;
    return fat_readdir(&v, p->stack[p->depth], out, max);
}
/* 0 = folder (in *out), 1 = file (its folder in *out, entry in *fe),
 * -1 not found, -2 drive not usable, -3 not a folder, -4 path too deep */
static int tp_resolve(const char *arg, struct tpath *out, struct dent *fe) {
    struct tpath p = tcwd;
    const char *s = arg;
    if (s[0] && s[1] == ':') {
        char L = kupper(s[0]);
        if (L == 'R' && !fs_on_disk) tp_root(&p, -1);
        else if (L >= 'C' && L < 'C' + N_DRIVES && drv[L - 'C'].present) {
            int d = L - 'C';
            if (fs_on_disk && d == fs_drive) tp_root(&p, -1);
            else if (drv[d].type == DT_FAT) tp_root(&p, d);
            else return -2;
        } else return -2;
        s += 2;
    } else if (*s == '/' || *s == '\\') tp_root(&p, p.drive);
    int is_file = 0;
    while (*s) {
        while (*s == '/' || *s == '\\') s++;
        if (!*s) break;
        char comp[48]; int n = 0;
        while (*s && *s != '/' && *s != '\\') { if (n < 47) comp[n++] = *s; s++; }
        comp[n] = 0;
        if (is_file) return -3;
        if (!kstrcmp(comp, ".")) continue;
        if (!kstrcmp(comp, "..")) { tp_pop(&p); continue; }
        int cnt = tp_list(&p, tp_ents, MAX_DENT);
        if (cnt < 0) return -2;
        int k = 0;
        while (k < cnt && !tp_ieq(tp_ents[k].name, comp)) k++;
        if (k == cnt) return -1;
        if (tp_ents[k].isdir) { if (!tp_push(&p, tp_ents[k].name, tp_ents[k].cluster)) return -4; }
        else { *fe = tp_ents[k]; is_file = 1; }
    }
    *out = p;
    return is_file;
}
static void tp_err(int r, const char *arg) {
    if (r == -1) tprintf("Not found: %s\n", arg);
    else if (r == -2) tprintf("Cannot use that drive: %s\n(drives: C: to F:, FAT or LeactedFS; see disk)\n", arg);
    else if (r == -3) tprintf("Not a folder: %s\n", arg);
    else tprintf("Path too deep: %s\n", arg);
}
static void tp_fref(const struct tpath *dir, const struct dent *e, struct fref *f) {
    memset(f, 0, sizeof *f);
    if (dir->drive < 0) { f->kind = 1; f->drive = fs_on_disk ? fs_drive : -1; }
    else { f->kind = 2; f->drive = dir->drive; }
    f->cluster = e->cluster; f->size = e->size;
    kstrcpy(f->name, e->name, sizeof f->name);
}
/* find a file (also tries NAME.lapp when asked); prints the error and returns 0 if it fails */
static int tp_find_file(const char *arg, struct fref *f, int try_lapp) {
    struct tpath dir; struct dent e;
    int r = tp_resolve(arg, &dir, &e);
    if (r == -1 && try_lapp && slen(arg) < 40) {
        char nm[48]; ksnprintf(nm, sizeof nm, "%s.lapp", arg);
        r = tp_resolve(nm, &dir, &e);
    }
    if (r == 0) { tprintf("That is a folder: %s\n", arg); return 0; }
    if (r < 0) { tp_err(r, arg); return 0; }
    tp_fref(&dir, &e, f);
    return 1;
}
/* load a whole file into tbuf (cut at the LeactedFS slot size) */
static int tp_read(const char *arg, u32 *len, int *cut, struct fref *fo) {
    struct fref f;
    if (!tp_find_file(arg, &f, 0)) return 0;
    u32 want = f.size < FS_SLOT ? f.size : FS_SLOT;
    u32 n = want ? fref_load(&f, tbuf, FS_SLOT) : 0;
    if (n != want) { tputs("Disk read error.\n"); return 0; }
    tbuf[n] = 0;
    *len = n; *cut = f.size > FS_SLOT;
    if (fo) *fo = f;
    return 1;
}
static const char *prompt(void) {
    static char pb[48];
    if ((tcwd.drive < 0 && !tcwd.depth)) return is_root ? "leacted# " : "leacted> ";
    char sh[64]; tp_show(&tcwd, sh, sizeof sh);
    int l = slen(sh);
    if (l > 30) ksnprintf(pb, sizeof pb, "...%s%s ", sh + l - 27, is_root ? "#" : ">");
    else ksnprintf(pb, sizeof pb, "%s%s ", sh, is_root ? "#" : ">");
    return pb;
}

/* Notepad state is needed by the terminal (edit command) */
#define NOTE_MAX 16000
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

static void note_open_data(const char *name, const u8 *data, u32 len, int ro, int cut) {
    open_app(A_NOTE);
    kstrcpy(nname, name, FS_NAME);
    nlen = 0;
    for (u32 i = 0; i < len && nlen < NOTE_MAX; i++) {
        u8 c = data[i];
        if (c == '\r') continue;
        nbuf[nlen++] = (c == '\n') ? '\n' : (c == '\t') ? ' ' : (c >= 32 && c < 127) ? (char)c : '.';
    }
    nbuf[nlen] = 0; ndirty = 0;
    kstrcpy(nmsg, ro ? (cut ? "Read-only copy (cut to 4 KB)" : "Read-only copy; Save keeps a copy") : "", sizeof nmsg);
}
static int split_args(char *s, char **argv, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') s++;
        if (!*s) break;
        if (*s == '"') {                          /* "quoted text" is one argument */
            argv[n++] = ++s;
            while (*s && *s != '"') s++;
        } else {
            argv[n++] = s;
            while (*s && *s != ' ') s++;
        }
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
static void fmt_size(char *o, int max, u32 n) {
    if (n >= 1048576) ksnprintf(o, max, "%u MB", n / 1048576);
    else if (n >= 1024) ksnprintf(o, max, "%u KB", n / 1024);
    else ksnprintf(o, max, "%u B", n);
}
static void cmd_ls(const char *arg) {
    struct tpath dir; struct dent fe;
    int r = arg ? tp_resolve(arg, &dir, &fe) : (dir = tcwd, 0);
    if (r == 1) { char z[16]; fmt_size(z, sizeof z, fe.size); tprintf(" %s  %s\n", fe.name, z); return; }
    if (r < 0) { tp_err(r, arg); return; }
    int n = tp_list(&dir, tp_ents, MAX_DENT);
    if (n < 0) { tputs("Cannot read that drive (not FAT or LeactedFS).\n"); return; }
    tp_sort(tp_ents, n);
    int dirs = 0;
    for (int i = 0; i < n; i++) {
        char z[16];
        if (tp_ents[i].isdir) { dirs++; kstrcpy(z, "<DIR>", sizeof z); } else fmt_size(z, sizeof z, tp_ents[i].size);
        tprintf(" %-34s %s\n", tp_ents[i].name, z);
    }
    tprintf("%d file(s), %d folder(s)", n - dirs, dirs);
    if (dir.drive < 0) tprintf(", %s", fs_on_disk ? "stored on disk" : "RAM only");
    tputc('\n');
}
static void cmd_cd(const char *arg) {
    if (!arg) { tp_root(&tcwd, -1); return; }
    struct tpath dir; struct dent fe;
    int r = tp_resolve(arg, &dir, &fe);
    if (r == 1) { tprintf("Not a folder: %s\n", arg); return; }
    if (r < 0) { tp_err(r, arg); return; }
    tcwd = dir;
}
/* tree: every level has its own small buffer so the kernel stack stays small */
static int tree_lines, tree_files, tree_dirs;
static struct dent tree_ents[4][48];
static void tree_walk(const struct tpath *p, int level) {
    int n = tp_list(p, tree_ents[level], 48);
    if (n < 0) return;
    tp_sort(tree_ents[level], n);
    for (int i = 0; i < n; i++) {
        struct dent *e = &tree_ents[level][i];
        if (tree_lines >= 250) return;
        tree_lines++;
        if (e->isdir) tree_dirs++; else tree_files++;
        tprintf("%*s%s%s\n", level * 2 + 1, "", e->name, e->isdir ? "\\" : "");
        if (e->isdir && level < 3) { struct tpath q = *p; if (tp_push(&q, e->name, e->cluster)) tree_walk(&q, level + 1); }
    }
}
static void cmd_tree(const char *arg) {
    struct tpath dir; struct dent fe;
    int r = arg ? tp_resolve(arg, &dir, &fe) : (dir = tcwd, 0);
    if (r == 1) { tprintf("Not a folder: %s\n", arg); return; }
    if (r < 0) { tp_err(r, arg); return; }
    char sh[64]; tp_show(&dir, sh, sizeof sh); tprintf("%s\n", sh);
    tree_lines = tree_files = tree_dirs = 0;
    tree_walk(&dir, 0);
    tprintf("%d file(s), %d folder(s)%s\n", tree_files, tree_dirs, tree_lines >= 250 ? " (list cut)" : "");
}
static void cmd_cat(const char *arg) {
    u32 len; int cut;
    if (!tp_read(arg, &len, &cut, 0)) return;
    u32 show = len < 16384 ? len : 16384;
    for (u32 k = 0; k < show; k++) tputc((char)tbuf[k]);
    if (show && tbuf[show - 1] != '\n') tputc('\n');
    if (show < len || cut) tputs("(cut: use head, tail or hexdump for the rest)\n");
}
static int arg_count(char **av, int ac, int *n) {         /* parses -n N (returns args used) */
    *n = 10;
    if (ac >= 3 && !kstrcmp(av[1], "-n")) { i32 v; if (!parse_int(av[2], &v) || v < 1) v = 10; *n = v; return 2; }
    return 0;
}
static void cmd_head(char **av, int ac) {
    int n, used = arg_count(av, ac, &n);
    if (ac < 2 + used) { tputs("Usage: head [-n N] FILE\n"); return; }
    u32 len; int cut;
    if (!tp_read(av[1 + used], &len, &cut, 0)) return;
    u32 k = 0; int lines = 0;
    while (k < len && lines < n) { if (tbuf[k] == '\n') lines++; tputc((char)tbuf[k]); k++; }
    if (k && tbuf[k - 1] != '\n') tputc('\n');
}
static void cmd_tail(char **av, int ac) {
    int n, used = arg_count(av, ac, &n);
    if (ac < 2 + used) { tputs("Usage: tail [-n N] FILE\n"); return; }
    u32 len; int cut;
    if (!tp_read(av[1 + used], &len, &cut, 0)) return;
    u32 end = len; if (end && tbuf[end - 1] == '\n') end--;
    u32 k = end; int lines = 0;
    while (k > 0) { if (tbuf[k - 1] == '\n' && ++lines == n) break; k--; }
    for (; k < len; k++) tputc((char)tbuf[k]);
    if (len && tbuf[len - 1] != '\n') tputc('\n');
    if (cut) tputs("(file is longer than 128 KB: this is the end of the first 128 KB)\n");
}
static void cmd_wc(const char *arg) {
    u32 len; int cut;
    if (!tp_read(arg, &len, &cut, 0)) return;
    u32 lines = 0, words = 0; int in = 0;
    for (u32 k = 0; k < len; k++) {
        u8 c = tbuf[k];
        if (c == '\n') lines++;
        int sp = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!sp && !in) words++;
        in = !sp;
    }
    tprintf("%u lines  %u words  %u bytes%s\n", lines, words, len, cut ? "+ (first 128 KB)" : "");
}
static int has_sub(const u8 *line, u32 n, const char *pat, int icase) {
    int pl = slen(pat);
    for (u32 i = 0; i + pl <= n; i++) {
        int k = 0;
        while (k < pl && (icase ? kupper((char)line[i + k]) == kupper(pat[k]) : (char)line[i + k] == pat[k])) k++;
        if (k == pl) return 1;
    }
    return 0;
}
static int grep_buf(const char *pat, int icase, int num, int count_only, const char *label, u32 len) {
    u32 k = 0, ln = 0; int hits = 0;
    while (k < len) {
        u32 e = k; while (e < len && tbuf[e] != '\n') e++;
        ln++;
        u32 n = e - k; if (n && tbuf[e - 1] == '\r') n--;
        if (has_sub(tbuf + k, n, pat, icase)) {
            hits++;
            if (!count_only) {
                if (label) tprintf("%s:", label);
                if (num) tprintf("%u:", ln);
                for (u32 i = 0; i < n; i++) tputc((char)tbuf[k + i]);
                tputc('\n');
            }
        }
        k = e + 1;
    }
    return hits;
}
static void cmd_grep(char **av, int ac) {
    int icase = 0, num = 0, cnt = 0, a = 1;
    while (a < ac && av[a][0] == '-' && av[a][1]) {
        for (const char *f = av[a] + 1; *f; f++) { if (*f == 'i') icase = 1; else if (*f == 'n') num = 1; else if (*f == 'c') cnt = 1; }
        a++;
    }
    if (a >= ac) { tputs("Usage: grep [-i] [-n] [-c] TEXT [FILE]\n(without FILE: search every file in this folder)\n"); return; }
    const char *pat = av[a++];
    if (a < ac) {
        u32 len; int cut;
        if (!tp_read(av[a], &len, &cut, 0)) return;
        int h = grep_buf(pat, icase, num, cnt, 0, len);
        if (cnt) tprintf("%d\n", h); else if (!h) tputs("(no match)\n");
        return;
    }
    int n = tp_list(&tcwd, tp_ents, MAX_DENT), total = 0;
    if (n < 0) { tputs("Cannot read this drive.\n"); return; }
    static struct dent list[64]; if (n > 64) n = 64;
    memcpy(list, tp_ents, n * sizeof(struct dent));
    for (int i = 0; i < n; i++) if (!list[i].isdir) {
        struct fref f; tp_fref(&tcwd, &list[i], &f);
        u32 want = f.size < FS_SLOT ? f.size : FS_SLOT;
        u32 got = want ? fref_load(&f, tbuf, FS_SLOT) : 0;
        if (got != want) continue;
        tbuf[got] = 0;
        int h = grep_buf(pat, icase, num, cnt, list[i].name, got);
        if (cnt && h) tprintf("%s:%d\n", list[i].name, h);
        total += h;
    }
    if (!total) tputs("(no match)\n");
}
static void cmd_hexdump(const char *arg, const char *off) {
    u32 len; int cut; i32 start = 0;
    if (off && !parse_int(off, &start)) start = 0;
    if (start < 0) start = 0;
    if (!tp_read(arg, &len, &cut, 0)) return;
    start &= ~7;
    if ((u32)start >= len && len) { tputs("Offset is past the end.\n"); return; }
    u32 pos = (u32)start;
    for (int row = 0; row < 16 && pos < len; row++, pos += 8) {
        char line[64]; int p = ksnprintf(line, sizeof line, "%05x ", pos);
        for (int i = 0; i < 8; i++) p += pos + i < len ? ksnprintf(line + p, sizeof line - p, " %02x", tbuf[pos + i]) : ksnprintf(line + p, sizeof line - p, "   ");
        line[p++] = ' '; line[p++] = ' ';
        for (int i = 0; i < 8 && pos + i < len; i++) { u8 c = tbuf[pos + i]; line[p++] = (c >= 32 && c < 127) ? (char)c : '.'; }
        line[p] = 0;
        tprintf("%s\n", line);
    }
    if (pos < len) tprintf("(more: hexdump %s %u)\n", arg, pos);
}
/* where a copy/move target lands: always the root of LeactedFS. 0 = ok */
static const char *dest_name(const char *dst, const char *srcname, char *out) {
    const char *d = dst;
    if (d[0] && d[1] == ':') {
        char L = kupper(d[0]);
        int ok = fs_on_disk ? (L == 'C' + fs_drive) : L == 'R';
        if (!ok) return "Files can only be copied into LeactedFS (FAT drives are read-only).";
        d += 2;
    }
    while (*d == '/' || *d == '\\') d++;
    for (const char *q = d; *q; q++) if ((*q == '/' || *q == '\\') && q[1]) return "LeactedFS has no folders: give a plain file name.";
    char nm[FS_NAME]; int n = 0;
    const char *use = *d ? d : srcname;
    for (; use[n] && use[n] != '/' && use[n] != '\\' && n < FS_NAME - 1; n++) nm[n] = use[n];
    nm[n] = 0;
    if (!n) return "Missing file name.";
    fs_norm(out, nm);
    return 0;
}
static void cmd_cp(const char *src, const char *dst) {
    struct fref f;
    if (!tp_find_file(src, &f, 0)) return;
    char nm[FS_NAME]; const char *err = dest_name(dst, f.name, nm);
    if (err) { tprintf("%s\n", err); return; }
    int ex = fs_find(nm);
    if (f.kind == 1 && ex == (int)f.cluster) { tputs("Source and target are the same file.\n"); return; }
    if (ex >= 0 && !is_root) { tprintf("%s exists. Use 'super' to overwrite.\n", nm); return; }
    u32 want = f.size < fs_slot_limit ? f.size : fs_slot_limit;
    u32 n = want ? fref_load(&f, tbuf, fs_slot_limit) : 0;
    if (n != want) { tputs("Disk read error.\n"); return; }
    int i = fs_create(nm);
    if (i < 0) { tprintf("LeactedFS is full (%d files).\n", fs_max_files); return; }
    int r = fs_store(i, tbuf, n);
    if (f.size > fs_slot_limit) tprintf("Copied the first %u KB of %u bytes.\n", fs_slot_limit / 1024, f.size);
    print_store_result(r);
}
static void cmd_mv(const char *from, const char *to) {
    int i = fs_find(from[0] && from[1] == ':' ? from + 2 : from);
    if (i < 0) { tprintf("Not found in LeactedFS: %s\n", from); return; }
    char nm[FS_NAME]; const char *err = dest_name(to, fsdir[i].name, nm);
    if (err) { tprintf("%s\n", err); return; }
    int ex = fs_find(nm);
    if (ex == i) { tputs("Same name.\n"); return; }
    if (ex >= 0) { tprintf("%s already exists.\n", nm); return; }
    int r = fs_rename(i, nm);
    tputs(r < 0 ? "Disk write failed!\n" : "Renamed.\n");
}
static void cmd_touch(const char *name) {
    char nm[FS_NAME]; const char *err = dest_name(name, "", nm);
    if (err) { tprintf("%s\n", err); return; }
    if (fs_find(nm) >= 0) { tputs("Already exists.\n"); return; }
    int i = fs_create(nm);
    if (i < 0) { tprintf("LeactedFS is full (%d files).\n", fs_max_files); return; }
    print_store_result(fs_store(i, "", 0));
}
static void cmd_history(void) {
    for (int i = 0; i < nhist; i++) tprintf("%3d  %s\n", i + 1, hist[i]);
    if (!nhist) tputs("(empty)\n");
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
    if (i < 0) { tprintf("Disk full (%d files max).\n", fs_max_files); return; }
    static char buf[FS_SLOT + 2]; u32 len = 0;
    if (append) { len = fsdir[i].size; memcpy(buf, fsdata[i], len); }
    for (int k = 0; line[k] && len < fs_slot_limit - 1; k++) buf[len++] = line[k];
    buf[len++] = '\n';
    print_store_result(fs_store(i, buf, len));
}
static void run_cmd(char *line);
enum { TJ_NONE, TJ_PING, TJ_LOOKUP, TJ_WGET, TJ_DHCP, TJ_LGET };
static int tjob;
static int net_job_ok(void);
static void tjob_cancel(void);
static int net_command(const char *c, char **av, int ac);
static int ext_is(const char *name, const char *ext);
static void fm_refresh(void);
static void cmd_apps(void) {
    int n = 0;
    tputs("Apps on LeactedFS:\n");
    for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used && ext_is(fsdir[i].name, "lapp")) { tprintf(" %s  %u bytes\n", fsdir[i].name, fsdir[i].size); n++; }
    if (!n) tputs(" (none - copy .lapp files in with the File Manager)\n");
    n = 0;
    for (int k = 0; k < N_EXT; k++) if (xapp[k].used) { if (!n++) tputs("Running:\n"); tprintf(" %s\n", xapp[k].title); }
}
static void cmd_appinfo(const char *arg) {
    struct fref f;
    if (!tp_find_file(arg, &f, 1)) return;
    if (f.size > FS_SLOT) { tputs("Not a LeactedOS app (file too big).\n"); return; }
    u32 n = f.size ? fref_load(&f, tbuf, FS_SLOT) : 0;
    struct lapp_header h; const char *err = lapp_check(tbuf, n, &h);
    if (err) { tprintf("%s\n", err); return; }
    tprintf("%s\n window %dx%d, memory %u bytes\n needs API %u, %u relocations\n", h.name, h.win_w, h.win_h, h.image_size + h.bss_size, h.api_min, h.nreloc);
}
static void cmd_run(const char *arg) {
    struct fref f;
    if (!tp_find_file(arg, &f, 1)) return;
    const char *err = lapp_run_fref(&f);
    if (err) tprintf("Cannot run: %s\n", err);
}

/* ------------------------------------------------------------------ */
/* Leact-get: install apps from a repository on a drive                */
/*  A repository is a folder holding leact-repo.txt and the .lapp      */
/*  files it lists:                                                    */
/*      LEACT-REPO 1                                                   */
/*      name|path/to/file.lapp|version|one line description            */
/* ------------------------------------------------------------------ */
#define LG_MAX 48
struct lg_app { char name[24]; char file[64]; char ver[12]; char desc[64]; };
static struct lg_app lg_apps[LG_MAX];
static int lg_n;
static struct tpath lg_repo;                    /* chosen with: leact-get repo PATH */
static int lg_repo_set;
static const char LG_INDEX[] = "leact-repo.txt";

static int lg_has_index(const struct tpath *dir) {
    int n = tp_list(dir, tp_ents, MAX_DENT);
    for (int i = 0; i < n; i++) if (!tp_ents[i].isdir && tp_ieq(tp_ents[i].name, LG_INDEX)) return 1;
    return 0;
}
/* the folder to use: the chosen one, else the first FAT drive with an index, else LeactedFS */
static int lg_find_repo(struct tpath *out) {
    if (lg_repo_set && lg_has_index(&lg_repo)) { *out = lg_repo; return 1; }
    struct tpath p;
    for (int d = 0; d < N_DRIVES; d++) {
        if (!drv[d].present || drv[d].type != DT_FAT) continue;
        tp_root(&p, d);
        if (lg_has_index(&p)) { *out = p; return 1; }
    }
    tp_root(&p, -1);
    if (lg_has_index(&p)) { *out = p; return 1; }
    return 0;
}
/* look a path up relative to a folder (no drive prefix) */
static int lg_resolve(const struct tpath *base, const char *rel, struct fref *f) {
    struct tpath keep = tcwd, dir; struct dent e;
    tcwd = *base;
    int r = tp_resolve(rel, &dir, &e);
    tcwd = keep;
    if (r != 1) return 0;
    tp_fref(&dir, &e, f);
    return 1;
}
static void lg_field(char *dst, int max, const char *s, int n) {
    while (n > 0 && *s == ' ') { s++; n--; }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r')) n--;
    if (n > max - 1) n = max - 1;
    memcpy(dst, s, n); dst[n] = 0;
}
static int lg_name_ok(const char *n) {
    int l = slen(n);
    if (l < 1 || l > 17) return 0;
    for (int i = 0; i < l; i++) { char c = n[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0; }
    return 1;
}
/* parse index text into lg_apps; returns the number of apps or -1 (message printed) */
static int lg_parse(const u8 *buf, u32 len) {
    lg_n = 0; int seen_head = 0, bad = 0;
    for (u32 k = 0; k < len;) {
        u32 e = k; while (e < len && buf[e] != '\n') e++;
        const char *ln = (const char *)buf + k; int n = (int)(e - k);
        k = e + 1;
        while (n > 0 && (*ln == ' ' || *ln == '\t')) { ln++; n--; }
        if (n > 0 && ln[n - 1] == '\r') n--;
        if (!n || *ln == '#') continue;
        if (!seen_head) {
            if (n < 10 || memcmp(ln, "LEACT-REPO", 10)) { tputs("Not a Leact-get repository (missing LEACT-REPO line).\n"); return -1; }
            seen_head = 1; continue;
        }
        const char *fld[4]; int fl[4], nf = 0, st = 0;
        for (int i = 0; i <= n && nf < 4; i++) if (i == n || ln[i] == '|') { fld[nf] = ln + st; fl[nf] = i - st; nf++; st = i + 1; }
        if (nf < 2 || lg_n >= LG_MAX) { bad++; continue; }
        struct lg_app *a = &lg_apps[lg_n];
        memset(a, 0, sizeof *a);
        lg_field(a->name, sizeof a->name, fld[0], fl[0]);
        lg_field(a->file, sizeof a->file, fld[1], fl[1]);
        if (nf > 2) lg_field(a->ver, sizeof a->ver, fld[2], fl[2]);
        if (nf > 3) lg_field(a->desc, sizeof a->desc, fld[3], fl[3]);
        if (!lg_name_ok(a->name) || !a->file[0]) { bad++; continue; }
        lg_n++;
    }
    if (!seen_head) { tputs("Not a Leact-get repository (empty index).\n"); return -1; }
    if (bad) tprintf("(skipped %d bad line(s) in the index)\n", bad);
    return lg_n;
}
/* read the index of a repository on a drive */
static int lg_load(struct tpath *repo) {
    if (!lg_find_repo(repo)) {
        tputs("No repository found. Attach a disk that holds\nleact-repo.txt (see README), or: leact-get repo PATH\n");
        return -1;
    }
    struct fref f;
    if (!lg_resolve(repo, LG_INDEX, &f)) { tputs("Cannot read the repository index.\n"); return -1; }
    u32 want = f.size < FS_SLOT ? f.size : FS_SLOT;
    u32 len = want ? fref_load(&f, tbuf, FS_SLOT) : 0;
    if (len != want) { tputs("Cannot read the repository index.\n"); return -1; }
    return lg_parse(tbuf, len);
}
static int lg_find(const char *name) {
    for (int i = 0; i < lg_n; i++) if (tp_ieq(lg_apps[i].name, name)) return i;
    return -1;
}
static int lg_installed(const char *name) {
    char nm[FS_NAME]; ksnprintf(nm, sizeof nm, "%s.lapp", name);
    return fs_find(nm);
}
/* header checksum of the repository copy (reads only the first 64 bytes); 0 if unreadable */
static u32 lg_remote_crc(const struct tpath *repo, const struct lg_app *a) {
    struct fref f; u8 h[LAPP_HDR_SIZE];
    if (!lg_resolve(repo, a->file, &f) || f.size < LAPP_HDR_SIZE) return 0;
    if (fref_load(&f, h, LAPP_HDR_SIZE) < LAPP_HDR_SIZE) return 0;
    return lapp_rd32(h + 28);
}
/* check a package and save it as NAME.LAPP; NULL and *state: 0 new, 1 updated, 2 already current */
static const char *lg_store(const struct lg_app *a, const u8 *data, u32 n, int *state) {
    if (n > APP_FILE_MAX) return "Package is too big.";
    struct lapp_header h; const char *err = lapp_check(data, n, &h);
    if (err) return err;
    char nm[FS_NAME]; ksnprintf(nm, sizeof nm, "%s.lapp", a->name);
    int ex = fs_find(nm);
    if (ex >= 0 && fsdir[ex].size >= LAPP_HDR_SIZE && lapp_rd32(fsdata[ex] + 28) == h.crc32) { *state = 2; return 0; }
    int i = fs_create(nm);
    if (i < 0) return "LeactedFS is full.";
    *state = ex >= 0 ? 1 : 0;
    int r = fs_store(i, data, n);
    if (r < 0) return "Disk write failed!";
    return 0;
}
/* install or update from a repository on a drive */
static const char *lg_install(const struct tpath *repo, const struct lg_app *a, int *state) {
    struct fref f;
    if (!lg_resolve(repo, a->file, &f)) return "Package file not found in the repository.";
    if (f.size > APP_FILE_MAX) return "Package is too big.";
    u32 n = fref_load(&f, tbuf, FS_SLOT);
    if (n != f.size) return "Disk read error.";
    return lg_store(a, tbuf, n, state);
}

/* ---- online repositories: the index and packages come over http(s) as a background job ---- */
#define LG_DEFAULT_REPO "https://raw.githubusercontent.com/avarabb990/LeactedOS-Project/main/repo/"
enum { LGO_LIST, LGO_INFO, LGO_INSTALL, LGO_UPDATE };
static char lg_url[200] = LG_DEFAULT_REPO;
static int lg_url_set;                                     /* the user chose online (a URL, or "default") */
static int lg_op, lg_stage, lg_ui, lg_cur, lg_changed;
static char lg_arg[24];
static int lg_use_remote(void) {
    struct tpath r;
    if (lg_url_set) return 1;
    if (lg_repo_set) return 0;
    if (lg_find_repo(&r)) return 0;
    return net_have_nic;                                   /* nothing attached: go online */
}
static void lg_remote_start(int op, const char *arg) {
    if (!net_job_ok()) return;
    lg_op = op; lg_stage = 0; lg_changed = 0; lg_ui = 0; lg_cur = -1;
    kstrcpy(lg_arg, arg ? arg : "", sizeof lg_arg);
    char u[320]; url_join(lg_url, LG_INDEX, u, sizeof u);
    http_start(u);
    tjob = TJ_LGET;
    tputs("Contacting the repository...\n");
}
static int lg_next_update(void) {                          /* start downloading the next installed app; 0 when done */
    while (lg_ui < lg_n) {
        int i = lg_ui++;
        if (lg_arg[0] && !tp_ieq(lg_apps[i].name, lg_arg)) continue;
        if (lg_installed(lg_apps[i].name) < 0) continue;
        char u[320]; url_join(lg_url, lg_apps[i].file, u, sizeof u);
        lg_cur = i; http_start(u);
        return 1;
    }
    return 0;
}
static void lg_job_tick(void) {
    if (http_active()) return;
    if (lg_stage == 0) {                                   /* the index arrived (or failed) */
        if (http_st == H_ERR) { tprintf("Cannot reach the repository: %s\n", http_msg); tjob = TJ_NONE; return; }
        if (http_code != 200) { tprintf("The repository answered %d.\nCheck the address with: leact-get repo\n", http_code); tjob = TJ_NONE; return; }
        int n = lg_parse(http_body, http_len);
        if (n < 0) { tjob = TJ_NONE; return; }
        if (lg_op == LGO_LIST) {
            tprintf("Online repository: %d app(s)   * = installed\n", n);
            for (int i = 0; i < n; i++)
                tprintf("%s %-14s %-5s %s\n", lg_installed(lg_apps[i].name) >= 0 ? "*" : " ", lg_apps[i].name, lg_apps[i].ver, lg_apps[i].desc);
            tputs("Update installed apps with: leact-get update\n");
            tjob = TJ_NONE; return;
        }
        if (lg_op == LGO_INFO) {
            int i = lg_find(lg_arg);
            if (i < 0) tprintf("No app called %s in the repository.\n", lg_arg);
            else tprintf("%s  version %s\n%s\nfile: %s\nInstalled: %s\n", lg_apps[i].name, lg_apps[i].ver[0] ? lg_apps[i].ver : "?", lg_apps[i].desc, lg_apps[i].file, lg_installed(lg_apps[i].name) < 0 ? "no" : "yes");
            tjob = TJ_NONE; return;
        }
        if (lg_op == LGO_INSTALL) {
            int i = lg_find(lg_arg);
            if (i < 0) { tprintf("No app called %s in the repository. Try: leact-get list\n", lg_arg); tjob = TJ_NONE; return; }
            char u[320]; url_join(lg_url, lg_apps[i].file, u, sizeof u);
            lg_cur = i; http_start(u); lg_stage = 1;
            tprintf("Downloading %s...\n", lg_apps[i].name);
            return;
        }
        if (!lg_next_update()) { tputs(lg_arg[0] ? "That app is not installed.\n" : "No installed apps to update.\n"); tjob = TJ_NONE; return; }
        lg_stage = 1;
        return;
    }
    /* stage 1: a package arrived (or failed) */
    const struct lg_app *a = &lg_apps[lg_cur];
    int st = 0; const char *err = 0;
    if (http_st == H_ERR) err = http_msg;
    else if (http_code != 200) { static char m[40]; ksnprintf(m, sizeof m, "the server answered %d", http_code); err = m; }
    else if (http_trunc) err = "Package is too big.";
    else err = lg_store(a, http_body, http_len, &st);
    if (lg_op == LGO_INSTALL) {
        if (err) tprintf("Could not install %s: %s\n", a->name, err);
        else if (st == 2) tprintf("%s is already installed and up to date.\n", a->name);
        else tprintf("%s %s. Start it with: run %s\n", st ? "Updated" : "Installed", a->name, a->name);
        tjob = TJ_NONE; return;
    }
    if (err) tprintf("%s: %s\n", a->name, err);
    else if (st == 1) { tprintf("Updated %s\n", a->name); lg_changed++; }
    if (lg_next_update()) return;
    if (!lg_changed) tputs("Everything is up to date.\n");
    tjob = TJ_NONE;
}
static void cmd_leactget(char **av, int ac) {
    const char *sub = ac > 1 ? av[1] : "help";
    struct tpath repo;
    if (!kstrcmp(sub, "help") || !kstrcmp(sub, "?")) {
        tputs("leact-get list            apps in the repository\n");
        tputs("leact-get info NAME       details\n");
        tputs("leact-get install NAME    install (or: leact-get NAME)\n");
        tputs("leact-get update [NAME]   get newer versions\n");
        tputs("leact-get remove NAME     uninstall\n");
        tputs("leact-get repo [URL|PATH|default|auto]\n");
        tputs("                         show / choose the repository\n");
        tputs("Apps are saved in LeactedFS. Start one with: run NAME\n");
        return;
    }
    if (!kstrcmp(sub, "repo")) {
        if (ac > 2 && (n_starts(av[2], "http://") || n_starts(av[2], "https://"))) {
            kstrcpy(lg_url, av[2], sizeof lg_url);
            int l = slen(lg_url); if (l && l < (int)sizeof lg_url - 1 && lg_url[l - 1] != '/') { lg_url[l] = '/'; lg_url[l + 1] = 0; }
            lg_url_set = 1; lg_repo_set = 0;
        } else if (ac > 2 && !kstrcmp(av[2], "default")) { kstrcpy(lg_url, LG_DEFAULT_REPO, sizeof lg_url); lg_url_set = 1; lg_repo_set = 0; }
        else if (ac > 2 && !kstrcmp(av[2], "auto")) { lg_url_set = 0; lg_repo_set = 0; }
        else if (ac > 2) {
            struct tpath dir; struct dent e; int r = tp_resolve(av[2], &dir, &e);
            if (r != 0) { if (r == 1) tprintf("Not a folder: %s\n", av[2]); else tp_err(r, av[2]); return; }
            if (!lg_has_index(&dir)) { tprintf("No %s in that folder.\n", LG_INDEX); return; }
            lg_repo = dir; lg_repo_set = 1;
        }
        if (ac > 2 && lg_url_set == 0 && (n_starts(av[2], "http") || !kstrcmp(av[2], "default"))) lg_url_set = 1;
        if (lg_use_remote()) tprintf("Online repository:\n%s\n", lg_url);
        else if (lg_find_repo(&repo)) { char sh[64]; tp_show(&repo, sh, sizeof sh); tprintf("Repository: %s\n", sh); }
        else tputs("No repository found.\n");
        return;
    }
    if (!kstrcmp(sub, "list") || !kstrcmp(sub, "ls")) {
        if (lg_use_remote()) { lg_remote_start(LGO_LIST, 0); return; }
        int n = lg_load(&repo);
        if (n < 0) return;
        char sh[64]; tp_show(&repo, sh, sizeof sh);
        tprintf("Repository %s: %d app(s)   * = installed\n", sh, n);
        for (int i = 0; i < n; i++) {
            int ins = lg_installed(lg_apps[i].name);
            const char *mark = " ";
            if (ins >= 0) { u32 rc = lg_remote_crc(&repo, &lg_apps[i]); mark = (rc && rc != lapp_rd32(fsdata[ins] + 28)) ? "!" : "*"; }
            tprintf("%s %-14s %-5s %s\n", mark, lg_apps[i].name, lg_apps[i].ver, lg_apps[i].desc);
        }
        tputs("! = newer version available (leact-get update)\n");
        return;
    }
    if (!kstrcmp(sub, "info")) {
        if (ac < 3) { tputs("Usage: leact-get info NAME\n"); return; }
        if (lg_use_remote()) { lg_remote_start(LGO_INFO, av[2]); return; }
        if (lg_load(&repo) < 0) return;
        int i = lg_find(av[2]);
        if (i < 0) { tprintf("No app called %s in the repository.\n", av[2]); return; }
        tprintf("%s  version %s\n%s\nfile: %s\n", lg_apps[i].name, lg_apps[i].ver[0] ? lg_apps[i].ver : "?", lg_apps[i].desc, lg_apps[i].file);
        int ins = lg_installed(lg_apps[i].name);
        tprintf("Installed: %s\n", ins < 0 ? "no" : "yes");
        return;
    }
    if (!kstrcmp(sub, "remove") || !kstrcmp(sub, "uninstall")) {
        if (ac < 3) { tputs("Usage: leact-get remove NAME\n"); return; }
        int ins = lg_installed(av[2]);
        if (ins < 0) { tprintf("%s is not installed.\n", av[2]); return; }
        char nm[FS_NAME]; kstrcpy(nm, fsdir[ins].name, sizeof nm);
        int r = fs_delete(ins);
        if (r < 0) tputs("Disk write failed!\n"); else tprintf("Removed %s.\n", nm);
        return;
    }
    if (!kstrcmp(sub, "update") || !kstrcmp(sub, "upgrade")) {
        if (lg_use_remote()) { lg_remote_start(LGO_UPDATE, ac > 2 ? av[2] : 0); return; }
        if (lg_load(&repo) < 0) return;
        int done = 0, checked = 0;
        for (int i = 0; i < lg_n; i++) {
            if (ac > 2 && !tp_ieq(lg_apps[i].name, av[2])) continue;
            if (lg_installed(lg_apps[i].name) < 0) continue;
            checked++;
            int st = 0; const char *err = lg_install(&repo, &lg_apps[i], &st);
            if (err) tprintf("%s: %s\n", lg_apps[i].name, err);
            else if (st == 1) { tprintf("Updated %s\n", lg_apps[i].name); done++; }
        }
        if (!checked) tputs(ac > 2 ? "That app is not installed.\n" : "No installed apps to update.\n");
        else if (!done) tputs("Everything is up to date.\n");
        return;
    }
    /* install: "leact-get install NAME" or just "leact-get NAME" */
    const char *name = (!kstrcmp(sub, "install") || !kstrcmp(sub, "get")) ? (ac > 2 ? av[2] : 0) : sub;
    if (!name) { tputs("Usage: leact-get install NAME\n"); return; }
    if (lg_use_remote()) { lg_remote_start(LGO_INSTALL, name); return; }
    if (lg_load(&repo) < 0) return;
    int i = lg_find(name);
    if (i < 0) { tprintf("No app called %s in the repository. Try: leact-get list\n", name); return; }
    int st = 0; const char *err = lg_install(&repo, &lg_apps[i], &st);
    if (err) { tprintf("Could not install %s: %s\n", lg_apps[i].name, err); return; }
    if (st == 2) tprintf("%s is already installed and up to date.\n", lg_apps[i].name);
    else tprintf("%s %s. Start it with: run %s\n", st ? "Updated" : "Installed", lg_apps[i].name, lg_apps[i].name);
}
static void cmd_help(void) {
    tputs("Files (paths like D:\\docs\\a.txt, quotes for spaces)\n");
    tputs(" ls [P] cd [P] pwd tree [P]   look around\n");
    tputs(" cat head tail wc hexdump P   read a file\n");
    tputs(" grep [-i -n -c] TEXT [P]     search\n");
    tputs(" cp SRC NAME   copy into LeactedFS (from any drive)\n");
    tputs(" mv OLD NEW    rename     touch NAME   new file\n");
    tputs(" echo T > F    write (>> appends)  rm F (super)\n");
    tputs(" edit F        open in Notepad\n");
    tputs("Apps\n");
    tputs(" run P  apps  appinfo P  open APP  calc A op B\n");
    tputs(" leact-get list | install NAME | remove NAME\n");
    tputs("Network (QEMU: -nic user,model=rtl8139)\n");
    tputs(" ifconfig  dhcp  ping H  nslookup H\n");
    tputs(" wget URL [NAME]   browse [URL]\n");
    tputs(" proxy [on|off|HOST:PORT]  (https gateway)\n");
    tputs("System\n");
    tputs(" disk fsupgrade date uptime mem about history\n");
    tputs(" theme xp|unity|kde  beep [hz]  super  clear\n");
    tputs(" reboot  shutdown  exit\n");
    tputs("PgUp / PgDn scroll the output.\n");
}
static void run_cmd(char *line) {
    char work[ILMAX + 2]; kstrcpy(work, line, sizeof work);
    char copy[ILMAX + 2]; kstrcpy(copy, line, sizeof copy);
    char *av[8]; int ac = split_args(work, av, 8);
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
    else if (!kstrcmp(c, "ls") || !kstrcmp(c, "dir")) cmd_ls(ac > 1 ? av[1] : 0);
    else if (!kstrcmp(c, "cd")) cmd_cd(ac > 1 ? av[1] : 0);
    else if (!kstrcmp(c, "pwd")) { char sh[64]; tp_show(&tcwd, sh, sizeof sh); tprintf("%s\n", sh); }
    else if (!kstrcmp(c, "tree")) cmd_tree(ac > 1 ? av[1] : 0);
    else if (!kstrcmp(c, "cat") || !kstrcmp(c, "type")) {
        if (ac < 2) { tputs("Usage: cat FILE\n"); return; }
        cmd_cat(av[1]);
    }
    else if (!kstrcmp(c, "head")) cmd_head(av, ac);
    else if (!kstrcmp(c, "tail")) cmd_tail(av, ac);
    else if (!kstrcmp(c, "wc")) { if (ac < 2) tputs("Usage: wc FILE\n"); else cmd_wc(av[1]); }
    else if (!kstrcmp(c, "grep")) cmd_grep(av, ac);
    else if (!kstrcmp(c, "hexdump") || !kstrcmp(c, "hd")) {
        if (ac < 2) { tputs("Usage: hexdump FILE [OFFSET]\n"); return; }
        cmd_hexdump(av[1], ac > 2 ? av[2] : 0);
    }
    else if (!kstrcmp(c, "cp") || !kstrcmp(c, "copy")) {
        if (ac < 3) { tputs("Usage: cp SOURCE NAME   (copies into LeactedFS)\n"); return; }
        cmd_cp(av[1], av[2]);
    }
    else if (!kstrcmp(c, "mv") || !kstrcmp(c, "rename") || !kstrcmp(c, "move")) {
        if (ac < 3) { tputs("Usage: mv OLD NEW   (LeactedFS files)\n"); return; }
        cmd_mv(av[1], av[2]);
    }
    else if (!kstrcmp(c, "touch")) { if (ac < 2) tputs("Usage: touch NAME\n"); else cmd_touch(av[1]); }
    else if (!kstrcmp(c, "history")) cmd_history();
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
    else if (!kstrcmp(c, "disk") || !kstrcmp(c, "drives")) {
        if (!n_drives()) { tputs("No disk detected.\nStart QEMU with: -drive file=disk.img,format=raw,if=ide\n"); return; }
        for (int d = 0; d < N_DRIVES; d++) {
            if (!drv[d].present) continue;
            char t[16]; dtype_str(d, t, sizeof t);
            tprintf(" %c: %s  %u MB  %s\n", 'C' + d, drv[d].model, drv[d].sectors / 2048, t);
        }
        tprintf("LeactedFS: %s\n", fs_on_disk ? (fs_ver == 1 ? "mounted from a drive (old v1: 15 files of 4 KB, see fsupgrade)" : "mounted from a drive (v2: 64 files of 128 KB)") : "RAM only (use the Installer)");
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
    else if (!kstrcmp(c, "run") || !kstrcmp(c, "start")) {
        if (ac < 2) { tputs("Usage: run APP   (see: apps)\n"); return; }
        cmd_run(av[1]);
    }
    else if (!kstrcmp(c, "fsupgrade")) {
        int r = fs_upgrade();
        if (r < 0) tputs(fs_on_disk ? "Nothing to do: LeactedFS is already v2.\n" : "No LeactedFS drive is mounted.\n");
        else if (r == 0) tputs("Could not upgrade (disk needs about 9 MB). Nothing changed.\n");
        else { tputs("Upgraded to LeactedFS v2: 64 files of 128 KB.\n"); if (wins[A_FILES].open) fm_refresh(); }
    }
    else if (!kstrcmp(c, "leact-get") || !kstrcmp(c, "leactget")) cmd_leactget(av, ac);
    else if (net_command(c, av, ac)) { }
    else if (!kstrcmp(c, "apps")) cmd_apps();
    else if (!kstrcmp(c, "appinfo")) {
        if (ac < 2) { tputs("Usage: appinfo APP\n"); return; }
        cmd_appinfo(av[1]);
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
        if (nhist == NHIST) { memmove(hist[0], hist[1], (NHIST - 1) * (ILMAX + 1)); nhist--; }
        kstrcpy(hist[nhist++], iline, ILMAX + 1);
    }
    hpos = nhist; ilen = 0; iline[0] = 0;
    run_cmd(cmd);
}
static void term_key(int k) {
    if (k == K_PGUP) { int mx = trow >= TR ? trow - TR + 1 : 0; tscroll += TR - 3; if (tscroll > mx) tscroll = mx; return; }
    if (k == K_PGDN) { tscroll -= TR - 3; if (tscroll < 0) tscroll = 0; return; }
    if (tjob) { if (k == 3) tjob_cancel(); return; }
    tscroll = 0;
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
    int top = trow >= TR ? trow - TR + 1 : 0;
    top -= tscroll; if (top < 0) top = 0;
    for (int r = top; r < top + TR && r <= trow; r++) {
        int py = y + 6 + (r - top) * 10;
        if (r < trow || tcol) draw_str(x + 6, py, tg[r], tfg);
        if (r == trow && !tjob) {
            int px = x + 6 + tcol * 8;
            px = draw_str(px, py, prompt(), RGB(80, 255, 80));
            int avail = TC - tcol - slen(prompt()) - 1, il = slen(iline);
            px = draw_str(px, py, il > avail && avail > 0 ? iline + il - avail : iline, RGB(230, 230, 230));
            if ((ticks / 50) & 1) rect(px, py, 8, 8, RGB(230, 230, 230));
        }
    }
    if (tscroll) draw_str(x + w - 8 * 8 - 14, y + 6, "[scroll]", RGB(255, 200, 80));
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
/* Hex viewer                                                          */
/* ------------------------------------------------------------------ */
#define HEX_MAX 131072
static u8 hbuf[HEX_MAX];
static u32 hlen, htotal, hscroll;
static char hname[48];

static void hex_open(const struct fref *f) {
    open_app(A_HEX);
    kstrcpy(hname, f->name, sizeof hname);
    hlen = fref_load(f, hbuf, HEX_MAX);
    htotal = f->size ? f->size : hlen;
    hscroll = 0;
}
static int hex_rows(int h) { return (h - 66) / 10; }
static u32 hex_maxscroll(int rows) { u32 tr = (hlen + 7) / 8; return tr > (u32)rows ? tr - rows : 0; }
static void hex_draw(int x, int y, int w, int h) {
    int rows = hex_rows(h);
    char nm[42]; kstrcpy(nm, hname, sizeof nm);
    draw_str(x + 8, y + 6, nm, C_TEXT);
    drawf(x + 8, y + 18, C_DARK, "%u bytes%s", htotal, htotal > hlen ? " (first 128 KB shown)" : "");
    rect(x + 6, y + 32, w - 12, rows * 10 + 6, RGB(16, 18, 28));
    for (int r = 0; r < rows; r++) {
        u32 off = (hscroll + r) * 8;
        if (off >= hlen) break;
        char b[32], a[10]; int p = 0;
        for (int k = 0; k < 8; k++) {
            if (off + k < hlen) {
                u8 c = hbuf[off + k];
                p += ksnprintf(b + p, (int)sizeof b - p, "%02x ", c);
                a[k] = (c >= 32 && c < 127) ? (char)c : '.';
            } else { b[p++] = ' '; b[p++] = ' '; b[p++] = ' '; a[k] = ' '; }
        }
        b[p] = 0; a[8] = 0;
        int ry = y + 35 + r * 10;
        drawf(x + 12, ry, RGB(110, 190, 230), "%05x", off);
        draw_str(x + 68, ry, b, RGB(210, 210, 215));
        draw_str(x + 68 + 24 * 8 + 6, ry, a, RGB(120, 230, 120));
    }
    button(x + 6, y + h - 28, 46, 22, "Top", 0);
    button(x + 58, y + h - 28, 46, 22, "PgUp", 0);
    button(x + 110, y + h - 28, 46, 22, "PgDn", 0);
    button(x + 162, y + h - 28, 46, 22, "End", 0);
    drawf(x + 218, y + h - 20, C_DARK, "Row %u / %u", hscroll + 1, (hlen + 7) / 8);
}
static void hex_scroll(int d, int rows) {
    i32 v = (i32)hscroll + d; u32 mx_ = hex_maxscroll(rows);
    if (v < 0) v = 0;
    if ((u32)v > mx_) v = (i32)mx_;
    hscroll = (u32)v;
}
static void hex_key(int k) {
    int rows = hex_rows(CH(A_HEX));
    if (k == K_UP) hex_scroll(-1, rows); else if (k == K_DOWN) hex_scroll(1, rows);
    else if (k == K_PGUP) hex_scroll(-rows, rows); else if (k == K_PGDN) hex_scroll(rows, rows);
    else if (k == K_HOME) hscroll = 0; else if (k == K_END) hscroll = hex_maxscroll(rows);
}
static void hex_click(int rx, int ry) {
    int h = CH(A_HEX);
    if (ry < h - 28 || ry >= h - 6) return;
    if (inrect(rx, ry, 6, h - 28, 46, 22)) hex_key(K_HOME);
    else if (inrect(rx, ry, 58, h - 28, 46, 22)) hex_key(K_PGUP);
    else if (inrect(rx, ry, 110, h - 28, 46, 22)) hex_key(K_PGDN);
    else if (inrect(rx, ry, 162, h - 28, 46, 22)) hex_key(K_END);
}

/* ------------------------------------------------------------------ */
/* Image viewer (BMP: 1/4/8/24/32-bit, uncompressed)                   */
/* ------------------------------------------------------------------ */
#define IMG_MAX 524288
static u8 ibuf[IMG_MAX];
static u32 img_len, ioff, istride, ipal[256];
static char iname[48];
static const char *imsg;
static int iw, ih, ibpp, itop, iok;

static void img_parse(void) {
    iok = 0; imsg = NULL;
    if (img_len < 54 || ibuf[0] != 'B' || ibuf[1] != 'M') { imsg = "Not a BMP picture"; return; }
    u32 dib = rd32(ibuf + 14);
    if (dib < 40) { imsg = "Old BMP format not supported"; return; }
    i32 w = (i32)rd32(ibuf + 18), h = (i32)rd32(ibuf + 22);
    u32 bpp = rd16(ibuf + 28), comp = rd32(ibuf + 30);
    ioff = rd32(ibuf + 10);
    itop = h < 0; if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) { imsg = "Bad picture size"; return; }
    if (comp != 0 && !(comp == 3 && bpp == 32)) { imsg = "Compressed BMP not supported"; return; }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) { imsg = "Unsupported color depth"; return; }
    istride = (((u32)w * bpp + 31) / 32) * 4;
    if (ioff > img_len || istride * (u32)h > img_len - ioff) { imsg = "File is truncated"; return; }
    if (bpp <= 8) {
        u32 ncol = rd32(ibuf + 46), po = 14 + dib;
        if (!ncol || ncol > (1u << bpp)) ncol = 1u << bpp;
        memset(ipal, 0, sizeof ipal);
        for (u32 i = 0; i < ncol; i++)
            if (po + i * 4 + 3 < img_len) ipal[i] = (ibuf[po + i * 4 + 2] << 16) | (ibuf[po + i * 4 + 1] << 8) | ibuf[po + i * 4];
    }
    iw = w; ih = h; ibpp = (int)bpp; iok = 1;
}
static void img_open(const struct fref *f) {
    open_app(A_IMG);
    kstrcpy(iname, f->name, sizeof iname);
    img_len = 0; iok = 0;
    if (f->size > IMG_MAX) { imsg = "Picture is larger than 512 KB"; return; }
    img_len = fref_load(f, ibuf, IMG_MAX);
    img_parse();
}
static u32 img_px(int x, int y) {
    const u8 *row = ibuf + ioff + (u32)(itop ? y : ih - 1 - y) * istride;
    switch (ibpp) {
    case 24: { const u8 *p = row + x * 3; return (p[2] << 16) | (p[1] << 8) | p[0]; }
    case 32: { const u8 *p = row + x * 4; return (p[2] << 16) | (p[1] << 8) | p[0]; }
    case 8: return ipal[row[x]];
    case 4: { u8 b = row[x >> 1]; return ipal[(x & 1) ? (b & 15) : (b >> 4)]; }
    case 1: return ipal[(row[x >> 3] >> (7 - (x & 7))) & 1];
    }
    return 0;
}
static void img_draw(int x, int y, int w, int h) {
    int ax = x + 4, ay = y + 4, aw = w - 8, ah = h - 32;
    rect(ax, ay, aw, ah, RGB(70, 72, 80));
    if (!iok) { draw_str_c(ax + aw / 2, ay + ah / 2 - 4, imsg ? imsg : "No picture", RGB(240, 240, 240)); }
    else {
        int dw = iw, dh = ih;
        if (dw > aw || dh > ah) {
            if ((u32)iw * (u32)ah > (u32)ih * (u32)aw) { dw = aw; dh = ih * aw / iw; }
            else { dh = ah; dw = iw * ah / ih; }
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        static int xs[640];
        for (int i = 0; i < dw && i < 640; i++) xs[i] = i * iw / dw;
        int ox = ax + (aw - dw) / 2, oy = ay + (ah - dh) / 2;
        for (int j = 0; j < dh; j++) {
            int sy = j * ih / dh;
            for (int i = 0; i < dw && i < 640; i++) pset(ox + i, oy + j, img_px(xs[i], sy));
        }
    }
    char nm[32]; kstrcpy(nm, iname, sizeof nm);
    if (iok) drawf(x + 6, y + h - 20, C_TEXT, "%s  %dx%d, %d-bit", nm, iw, ih, ibpp);
    else drawf(x + 6, y + h - 20, C_TEXT, "%s", nm);
}

/* ------------------------------------------------------------------ */
/* "Open with" dialog                                                  */
/* ------------------------------------------------------------------ */
static struct fref ow_ref;
static int ow_sel, ow_rec;
#define OW_N 4
static char ow_msg[48];
static const char *ow_apps[OW_N] = { "Notepad", "Image Viewer", "Hex Viewer", "Run as app" };
static const char *ow_desc[OW_N] = { "View and edit text files", "View BMP pictures", "Show raw bytes of any file", "Start a LeactedOS app" };

static int ext_is(const char *name, const char *ext) {
    const char *dot = NULL;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return 0;
    dot++;
    while (*dot && *ext) { if (klower(*dot) != *ext) return 0; dot++; ext++; }
    return !*dot && !*ext;
}
static int ow_recommend(const char *name) {
    static const char *txt[] = { "txt", "cfg", "c", "h", "md", "log", "ini", "bat", "csv", "sh", "py", "json", "asm", "s", "xml", "html", "htm", "conf", "inf", NULL };
    for (int i = 0; txt[i]; i++) if (ext_is(name, txt[i])) return 0;
    if (ext_is(name, "bmp")) return 1;
    if (ext_is(name, "lapp")) return 3;
    return 2;
}
static void openwith_show(const struct fref *f) {
    ow_ref = *f; ow_msg[0] = 0;
    ow_rec = ow_recommend(f->name);
    ow_sel = ow_rec;
    open_app(A_OPENWITH);
    raise_app(A_OPENWITH);
}
static void ow_launch(void) {
    struct fref f = ow_ref;
    if (ow_sel == 3) {
        const char *err = lapp_run_fref(&f);
        if (err) { kstrcpy(ow_msg, err, sizeof ow_msg); return; }
        close_app(A_OPENWITH);
        return;
    }
    close_app(A_OPENWITH);
    if (ow_sel == 0) {
        if (f.kind == 1) note_open_file(f.name);
        else {
            static u8 tmp[NOTE_MAX + 1];
            u32 n = fref_load(&f, tmp, NOTE_MAX);
            note_open_data(f.name, tmp, n, 1, f.size > NOTE_MAX);
        }
    } else if (ow_sel == 1) img_open(&f);
    else hex_open(&f);
}
static void ow_draw(int x, int y, int w, int h) {
    char nm[34]; kstrcpy(nm, ow_ref.name, sizeof nm);
    draw_str(x + 10, y + 10, "Open file with:", C_TEXT);
    draw_str(x + 10, y + 24, nm, RGB(0, 70, 190));
    drawf(x + 10, y + 36, C_DARK, "%u bytes", ow_ref.size);
    for (int i = 0; i < OW_N; i++) {
        int ry = y + 54 + i * 40, sel = ow_sel == i;
        rect(x + 8, ry, w - 16, 36, sel ? RGB(255, 255, 255) : C_FACE);
        frame(x + 8, ry, w - 16, 36, sel ? RGB(49, 106, 197) : C_DARK);
        rect(x + 14, ry + 6, 22, 22, i == 0 ? RGB(60, 110, 220) : i == 1 ? RGB(60, 170, 70) : i == 2 ? RGB(90, 100, 120) : RGB(230, 140, 40));
        draw_str(x + 44, ry + 8, ow_apps[i], C_TEXT);
        draw_str(x + 44, ry + 21, ow_desc[i], C_DARK);
        if (i == ow_rec) draw_str(x + w - 108, ry + 8, "recommended", RGB(0, 110, 0));
        fill_circle(x + w - 24, ry + 26, 5, C_DARK); fill_circle(x + w - 24, ry + 26, 4, C_LIGHT);
        if (sel) fill_circle(x + w - 24, ry + 26, 2, RGB(49, 106, 197));
    }
    if (ow_msg[0]) draw_str(x + 6, y + h - 46, ow_msg, RGB(190, 20, 20));
    button(x + w - 146, y + h - 30, 62, 22, "Open", 0);
    button(x + w - 76, y + h - 30, 62, 22, "Cancel", 0);
}
static void ow_click(int rx, int ry) {
    int w = CW(A_OPENWITH), h = CH(A_OPENWITH);
    for (int i = 0; i < OW_N; i++) if (inrect(rx, ry, 8, 54 + i * 40, w - 16, 36)) { ow_sel = i; ow_msg[0] = 0; return; }
    if (inrect(rx, ry, w - 146, h - 30, 62, 22)) ow_launch();
    else if (inrect(rx, ry, w - 76, h - 30, 62, 22)) close_app(A_OPENWITH);
}
static void ow_key(int k) {
    if (k == K_UP && ow_sel > 0) ow_sel--;
    else if (k == K_DOWN && ow_sel < OW_N - 1) ow_sel++;
    else if (k == '\n') ow_launch();
    else if (k == K_ESC) close_app(A_OPENWITH);
}

/* ------------------------------------------------------------------ */
/* File manager: drives, folders, files                                */
/* ------------------------------------------------------------------ */
#define FM_ROWS 22
static int fm_drive = -1, fm_depth, fm_n, fm_sel = -1, fm_scroll;
static u32 fm_stack[16];
static char fm_path[100];
static struct dent fm_ents[MAX_DENT];
static struct fatvol fm_v;
static char fm_msg[64];
static u32 fm_ltick; static int fm_lrow = -1;

static int fm_less(const struct dent *a, const struct dent *b) {
    if (a->isdir != b->isdir) return a->isdir;
    for (int i = 0;; i++) {
        char x = klower(a->name[i]), y = klower(b->name[i]);
        if (x != y) return x < y;
        if (!x) return 0;
    }
}
static void fm_sort(void) {
    for (int i = 1; i < fm_n; i++) {
        struct dent t = fm_ents[i]; int j = i - 1;
        while (j >= 0 && fm_less(&t, &fm_ents[j])) { fm_ents[j + 1] = fm_ents[j]; j--; }
        fm_ents[j + 1] = t;
    }
}
static int fm_is_leacted(void) { return fm_drive == FM_RAM || (fm_drive >= 0 && drv[fm_drive].type == DT_LEACTED && fm_drive == fs_drive); }
static void fm_refresh(void) {
    fm_n = 0; fm_sel = -1; fm_scroll = 0;
    if (fm_drive < 0) return;
    if (fm_drive == FM_RAM || drv[fm_drive].type == DT_LEACTED) {
        if (fm_drive != FM_RAM && fm_drive != fs_drive) { kstrcpy(fm_msg, "Second LeactedFS drive is not mounted.", sizeof fm_msg); return; }
        for (int i = 0; i < FS_MAX; i++) if (fsdir[i].used) {
            struct dent *d = &fm_ents[fm_n++];
            kstrcpy(d->name, fsdir[i].name, sizeof d->name);
            d->size = fsdir[i].size; d->cluster = (u32)i; d->isdir = 0;
        }
    } else if (drv[fm_drive].type == DT_FAT) {
        if (!fat_mount(fm_drive, &fm_v)) { kstrcpy(fm_msg, "Cannot read the FAT volume.", sizeof fm_msg); return; }
        fm_n = fat_readdir(&fm_v, fm_stack[fm_depth], fm_ents, MAX_DENT);
        fm_sort();
        if (fm_n >= MAX_DENT) kstrcpy(fm_msg, "Only the first 200 items are shown.", sizeof fm_msg);
    } else kstrcpy(fm_msg, "Unformatted drive. Use the Installer to format it.", sizeof fm_msg);
}
static void fm_pick_drive(int d) {
    fm_drive = d; fm_depth = 0; fm_stack[0] = 0; fm_msg[0] = 0;
    if (d == FM_RAM) kstrcpy(fm_path, "R:\\", sizeof fm_path);
    else ksnprintf(fm_path, sizeof fm_path, "%c:\\", 'C' + d);
    fm_refresh();
}
static void fm_enter(const struct dent *e) {
    int pl = slen(fm_path), nl = slen(e->name);
    if (fm_depth >= 15 || pl + nl + 2 >= (int)sizeof fm_path) { kstrcpy(fm_msg, "Folder is nested too deeply.", sizeof fm_msg); return; }
    fm_stack[++fm_depth] = e->cluster;
    memcpy(fm_path + pl, e->name, nl); fm_path[pl + nl] = '\\'; fm_path[pl + nl + 1] = 0;
    fm_msg[0] = 0;
    fm_refresh();
}
static void fm_up(void) {
    if (fm_depth <= 0) return;
    fm_depth--;
    int i = slen(fm_path) - 2;
    while (i >= 0 && fm_path[i] != '\\') i--;
    fm_path[i + 1] = 0;
    fm_msg[0] = 0;
    fm_refresh();
}
static void fm_open_sel(void) {
    if (fm_sel < 0 || fm_sel >= fm_n) { kstrcpy(fm_msg, "Select a file first.", sizeof fm_msg); return; }
    struct dent *e = &fm_ents[fm_sel];
    if (e->isdir) { fm_enter(e); return; }
    struct fref f;
    f.kind = fm_is_leacted() ? 1 : 2; f.drive = fm_drive; f.cluster = e->cluster; f.size = e->size;
    kstrcpy(f.name, e->name, sizeof f.name);
    fm_msg[0] = 0;
    openwith_show(&f);
}
static void fm_import(void) {
    if (fm_sel < 0 || fm_sel >= fm_n || fm_ents[fm_sel].isdir) { kstrcpy(fm_msg, "Select a file to import.", sizeof fm_msg); return; }
    if (fm_is_leacted()) { kstrcpy(fm_msg, "That file is already in LeactedFS.", sizeof fm_msg); return; }
    struct dent *e = &fm_ents[fm_sel];
    static u8 tmp[FS_SLOT];
    struct fref f = { 2, fm_drive, e->cluster, e->size, "" };
    u32 n = fref_load(&f, tmp, fs_slot_limit);
    int i = fs_create(e->name);
    if (i < 0) { ksnprintf(fm_msg, sizeof fm_msg, "LeactedFS is full (%d files).", fs_max_files); return; }
    int r = fs_store(i, tmp, n);
    char b[64]; ksnprintf(b, sizeof b, "%s%s %s", r < 0 ? "Disk error: " : "Imported ", fsdir[i].name, e->size > fs_slot_limit ? (fs_slot_limit > 4096 ? "(cut to 128 KB)" : "(cut to 4 KB)") : "");
    kstrcpy(fm_msg, b, sizeof fm_msg);
}
static void fm_delete(void) {
    if (!fm_is_leacted()) { kstrcpy(fm_msg, "Only LeactedFS files can be deleted.", sizeof fm_msg); return; }
    if (fm_sel < 0 || fm_sel >= fm_n) { kstrcpy(fm_msg, "Select a file first.", sizeof fm_msg); return; }
    int r = fs_delete((int)fm_ents[fm_sel].cluster);
    kstrcpy(fm_msg, r < 0 ? "Disk error while deleting." : "Deleted.", sizeof fm_msg);
    fm_refresh();
}
static void fm_new(void) {
    if (!fm_is_leacted()) { kstrcpy(fm_msg, "New files go on LeactedFS (R: or its drive).", sizeof fm_msg); return; }
    char nm[FS_NAME];
    for (int n = 1; n < 100; n++) { ksnprintf(nm, sizeof nm, "NEW%d.TXT", n); if (fs_find(nm) < 0) break; }
    int i = fs_create(nm);
    if (i < 0) { kstrcpy(fm_msg, "LeactedFS is full (15 files).", sizeof fm_msg); return; }
    fs_store(i, "", 0);
    fm_refresh();
    note_open_file(nm);
}
static int fm_rows_list(int *out) {
    int n = 0;
    for (int d = 0; d < N_DRIVES; d++) if (drv[d].present) out[n++] = d;
    if (!fs_on_disk) out[n++] = FM_RAM;
    return n;
}
static void fm_show_sel(void) {
    if (fm_sel < fm_scroll) fm_scroll = fm_sel;
    if (fm_sel >= fm_scroll + FM_ROWS) fm_scroll = fm_sel - FM_ROWS + 1;
    if (fm_scroll < 0) fm_scroll = 0;
}
static void files_draw(int x, int y, int w, int h) {
    button(x + 6, y + 4, 44, 22, "Up", 0);
    rect(x + 56, y + 4, w - 62, 22, C_LIGHT); frame(x + 56, y + 4, w - 62, 22, C_DARK);
    draw_str(x + 62, y + 11, fm_path[0] ? fm_path : "No drive selected", C_TEXT);
    /* drives */
    rect(x + 6, y + 32, 92, h - 66, C_LIGHT); frame(x + 6, y + 32, 92, h - 66, C_DARK);
    int rows[5], nr = fm_rows_list(rows);
    for (int i = 0; i < nr; i++) {
        int ry = y + 34 + i * 36, d = rows[i], sel = d == fm_drive;
        if (sel) rect(x + 7, ry, 90, 34, RGB(49, 106, 197));
        u32 fg = sel ? C_LIGHT : C_TEXT;
        rect(x + 12, ry + 6, 20, 14, RGB(150, 150, 160)); rect(x + 14, ry + 16, 4, 2, RGB(60, 230, 60));
        char t[16], tn[16];
        if (d == FM_RAM) { kstrcpy(tn, "RAM files", sizeof tn); draw_str(x + 38, ry + 9, "R:", fg); }
        else {
            dtype_str(d, tn, sizeof tn);
            if (drv[d].type == DT_EMPTY) kstrcpy(tn, "Empty", sizeof tn);
            ksnprintf(t, sizeof t, "%c:", 'C' + d); draw_str(x + 38, ry + 9, t, fg);
        }
        draw_str(x + 12, ry + 23, tn, sel ? RGB(220, 230, 250) : C_DARK);
    }
    /* file list */
    int x0 = x + 104, w0 = w - 110;
    rect(x0, y + 32, w0, h - 66, C_LIGHT); frame(x0, y + 32, w0, h - 66, C_DARK);
    rect(x0 + 1, y + 33, w0 - 2, 12, C_DARK);
    draw_str(x0 + 6, y + 35, "Name", C_LIGHT); draw_str(x0 + w0 - 108, y + 35, "Size", C_LIGHT);
    for (int i = 0; i < FM_ROWS; i++) {
        int k = fm_scroll + i;
        if (k >= fm_n) break;
        const struct dent *e = &fm_ents[k];
        int ry = y + 47 + i * 11, sel = k == fm_sel;
        if (sel) rect(x0 + 1, ry - 1, w0 - 15, 11, RGB(49, 106, 197));
        u32 fg = sel ? C_LIGHT : C_TEXT;
        if (e->isdir) { rect(x0 + 4, ry + 1, 8, 6, RGB(240, 200, 70)); rect(x0 + 4, ry, 4, 2, RGB(240, 200, 70)); }
        else { rect(x0 + 5, ry, 6, 8, C_LIGHT); frame(x0 + 5, ry, 6, 8, sel ? C_LIGHT : C_DARK); }
        char nm[36]; kstrcpy(nm, e->name, sizeof nm);
        draw_str(x0 + 16, ry, nm, fg);
        char sz[16];
        if (e->isdir) kstrcpy(sz, "<DIR>", sizeof sz);
        else if (e->size >= 1048576) ksnprintf(sz, sizeof sz, "%u MB", e->size / 1048576);
        else if (e->size >= 1024) ksnprintf(sz, sizeof sz, "%u KB", e->size / 1024);
        else ksnprintf(sz, sizeof sz, "%u B", e->size);
        draw_str(x0 + w0 - 108, ry, sz, fg);
    }
    if (fm_n > FM_ROWS) {                                    /* scroll bar */
        int sx = x0 + w0 - 13, top = y + 46, bot = y + 47 + FM_ROWS * 11 - 12;
        button(sx, top, 12, 12, "^", 0); button(sx, bot, 12, 12, "v", 0);
        int tr0 = top + 13, tr1 = bot - 1, th_ = (tr1 - tr0) * FM_ROWS / fm_n; if (th_ < 8) th_ = 8;
        int pos = (tr1 - tr0 - th_) * fm_scroll / (fm_n - FM_ROWS);
        rect(sx, tr0, 12, tr1 - tr0, RGB(220, 218, 205)); bevel(sx, tr0 + pos, 12, th_, 0);
    }
    if (fm_msg[0]) draw_str(x + 8, y + h - 31, fm_msg, RGB(150, 40, 0));
    else drawf(x + 8, y + h - 31, C_DARK, "%d item(s)  -  %s", fm_n, fm_drive == FM_RAM ? "RAM (not saved)" : fm_drive >= 0 ? "double-click to open" : "pick a drive");
    button(x + 6, y + h - 22, 56, 20, "Open", 0);
    button(x + 68, y + h - 22, 60, 20, "Import", 0);
    button(x + 134, y + h - 22, 56, 20, "Delete", 0);
    button(x + 196, y + h - 22, 44, 20, "New", 0);
    button(x + 246, y + h - 22, 66, 20, "Refresh", 0);
}
static void files_click(int rx, int ry) {
    int w = CW(A_FILES), h = CH(A_FILES);
    if (inrect(rx, ry, 6, 4, 44, 22)) { fm_up(); return; }
    int rows[5], nr = fm_rows_list(rows);
    for (int i = 0; i < nr; i++) if (inrect(rx, ry, 7, 34 + i * 36, 90, 34)) { fm_pick_drive(rows[i]); return; }
    int x0 = 104, w0 = w - 110;
    if (fm_n > FM_ROWS) {
        int sx = x0 + w0 - 13, top = 46, bot = 47 + FM_ROWS * 11 - 12;
        if (inrect(rx, ry, sx, top, 12, 12)) { fm_scroll = fm_scroll > 3 ? fm_scroll - 3 : 0; return; }
        if (inrect(rx, ry, sx, bot, 12, 12)) { fm_scroll += 3; if (fm_scroll > fm_n - FM_ROWS) fm_scroll = fm_n - FM_ROWS; return; }
    }
    for (int i = 0; i < FM_ROWS; i++) {
        int k = fm_scroll + i;
        if (k >= fm_n) break;
        if (inrect(rx, ry, x0 + 1, 47 + i * 11 - 1, w0 - 15, 11)) {
            int dbl = fm_lrow == k && ticks - fm_ltick < 60;
            fm_sel = k; fm_lrow = k; fm_ltick = ticks; fm_msg[0] = 0;
            if (dbl) fm_open_sel();
            return;
        }
    }
    if (inrect(rx, ry, 6, h - 22, 56, 20)) fm_open_sel();
    else if (inrect(rx, ry, 68, h - 22, 60, 20)) fm_import();
    else if (inrect(rx, ry, 134, h - 22, 56, 20)) fm_delete();
    else if (inrect(rx, ry, 196, h - 22, 44, 20)) fm_new();
    else if (inrect(rx, ry, 246, h - 22, 66, 20)) { fm_msg[0] = 0; fm_refresh(); }
}
static void files_key(int k) {
    if (k == K_UP) { if (fm_sel > 0) fm_sel--; else if (fm_sel < 0 && fm_n) fm_sel = 0; fm_show_sel(); }
    else if (k == K_DOWN) { if (fm_sel < fm_n - 1) fm_sel++; fm_show_sel(); }
    else if (k == K_PGUP) { fm_sel = fm_sel > FM_ROWS ? fm_sel - FM_ROWS : (fm_n ? 0 : -1); fm_show_sel(); }
    else if (k == K_PGDN) { fm_sel += FM_ROWS; if (fm_sel >= fm_n) fm_sel = fm_n - 1; fm_show_sel(); }
    else if (k == K_HOME) { fm_sel = fm_n ? 0 : -1; fm_show_sel(); }
    else if (k == K_END) { fm_sel = fm_n - 1; fm_show_sel(); }
    else if (k == '\n') fm_open_sel();
    else if (k == '\b') fm_up();
    else if (k == K_DEL) fm_delete();
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
    if (n_drives()) drawf(x + 14, y + 170, C_TEXT, "Disk    : %d drive(s)", n_drives());
    else drawf(x + 14, y + 170, C_TEXT, "Disk    : none detected");
    drawf(x + 14, y + 184, C_TEXT, "FS      : %s", fs_on_disk ? (fs_ver == 1 ? "LeactedFS v1 (small)" : "LeactedFS v2") : "none");
    drawf(x + 14, y + 198, C_TEXT, "Uptime  : %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    drawf(x + 14, y + 212, C_TEXT, "Mode    : %s", is_root ? "super (root)" : "normal");
    button(x + w - 74, y + h - 30, 62, 22, "OK", 0);
}
static void about_click(int rx, int ry) {
    if (inrect(rx, ry, CW(A_ABOUT) - 74, CH(A_ABOUT) - 30, 62, 22)) close_app(A_ABOUT);
}
static int inst_sel = -1;
static void install_pick_default(void) {
    inst_sel = -1;
    for (int d = 0; d < N_DRIVES; d++) if (drv[d].present && drv[d].type == DT_EMPTY) { inst_sel = d; return; }
    for (int d = 0; d < N_DRIVES; d++) if (drv[d].present && drv[d].type == DT_LEACTED) { inst_sel = d; return; }
    for (int d = 0; d < N_DRIVES; d++) if (drv[d].present) { inst_sel = d; return; }
}
static void install_draw(int x, int y, int w, int h) {
    (void)h;
    draw_str(x + 12, y + 12, "Format a drive as LeactedFS", C_TEXT);
    rect(x + 10, y + 30, w - 20, 1, C_DARK);
    if (!n_drives()) {
        draw_str(x + 12, y + 44, "No disk detected!", RGB(180, 0, 0));
        draw_str(x + 12, y + 62, "Attach a drive when starting QEMU:", C_TEXT);
        draw_str(x + 12, y + 76, "-drive file=disk.img,format=raw,if=ide", C_DARK);
        draw_str(x + 12, y + 92, "Create one with:", C_TEXT);
        draw_str(x + 12, y + 106, "qemu-img create -f raw disk.img 64M", C_DARK);
        return;
    }
    if (inst_sel < 0 || !drv[inst_sel].present) install_pick_default();
    draw_str(x + 12, y + 38, "Select the target drive:", C_TEXT);
    for (int d = 0, row = 0; d < N_DRIVES; d++) {
        if (!drv[d].present) continue;
        int ry = y + 52 + row * 18; row++;
        int sel = d == inst_sel;
        if (sel) rect(x + 8, ry - 2, w - 16, 16, RGB(255, 255, 255));
        fill_circle(x + 18, ry + 5, 5, C_DARK); fill_circle(x + 18, ry + 5, 4, C_LIGHT);
        if (sel) fill_circle(x + 18, ry + 5, 2, RGB(49, 106, 197));
        char m[14], t[16]; kstrcpy(m, drv[d].model, sizeof m); dtype_str(d, t, sizeof t);
        drawf(x + 30, ry + 1, C_TEXT, "%c: %s", 'C' + d, m);
        drawf(x + 216, ry + 1, C_DARK, "%u MB", drv[d].sectors / 2048);
        draw_str(x + 276, ry + 1, t, drv[d].type == DT_FAT ? RGB(170, 0, 0) : C_DARK);
    }
    int ty = inst_sel >= 0 ? drv[inst_sel].type : DT_NONE;
    if (ty == DT_FAT) draw_str(x + 12, y + 130, "This drive has FAT files: they will be ERASED!", RGB(190, 0, 0));
    else if (ty == DT_LEACTED) draw_str(x + 12, y + 130, "Already LeactedFS (it will be reset)", RGB(160, 100, 0));
    else draw_str(x + 12, y + 130, "Drive is empty / unformatted", C_TEXT);
    if (inst_sel >= 0 && drv[inst_sel].sectors < FS2_MIN_SECTORS) draw_str(x + 12, y + 144, "Too small: needs about 9 MB or more.", RGB(190, 0, 0));
    else draw_str(x + 12, y + 144, "Files in RAM are copied to the drive.", C_TEXT);
    button(x + w / 2 - 74, y + 164, 148, 26, inst_state == 1 ? "Click again to erase" : "Install Now", 0);
    if (inst_state == 2) draw_str_c(x + w / 2, y + 202, "Install successful!", RGB(0, 130, 0));
    if (inst_state == 3) draw_str_c(x + w / 2, y + 202, "Install FAILED", RGB(200, 0, 0));
}
static void fm_pick_drive(int d);
static void install_click(int rx, int ry) {
    int w = CW(A_INSTALL);
    if (!n_drives()) return;
    for (int d = 0, row = 0; d < N_DRIVES; d++) {
        if (!drv[d].present) continue;
        if (inrect(rx, ry, 8, 50 + row * 18, w - 16, 16)) { inst_sel = d; inst_state = 0; return; }
        row++;
    }
    if (inrect(rx, ry, w / 2 - 74, 164, 148, 26)) {
        if (inst_sel < 0 || drv[inst_sel].sectors < FS2_MIN_SECTORS) { inst_state = 3; return; }
        if (inst_state != 1) { inst_state = 1; return; }
        inst_state = fs_install(inst_sel) ? 2 : 3;
        if (inst_state == 2 && wins[A_FILES].open) fm_pick_drive(fs_drive);
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
    if (i < 0) { kstrcpy(set_msg, "Not saved (disk full)", sizeof set_msg); return; }
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
    rect(x + 6, y + 20, w - 12, 140, C_LIGHT); frame(x + 6, y + 20, w - 12, 140, C_DARK);
    rect(x + 7, y + 21, w - 14, 12, C_DARK);
    draw_str(x + 10, y + 23, "Task", C_LIGHT); draw_str(x + 220, y + 23, "Status", C_LIGHT);
    int row = 0, n = 0;
    for (int a = 0; a < N_APPS; a++) {
        if (!wins[a].open) continue;
        n++;
        if (row >= 11) continue;
        int ry = y + 35 + row * 11;
        if (a == tsel) rect(x + 7, ry - 1, w - 14, 11, RGB(49, 106, 197));
        u32 fg = a == tsel ? C_LIGHT : C_TEXT;
        draw_str(x + 10, ry, wins[a].title, fg);
        draw_str(x + 220, ry, wins[a].min ? "Minimized" : top_app() == a ? "Active" : "Running", fg);
        row++;
    }
    button(x + 6, y + 166, 76, 22, "End Task", 0);
    button(x + 88, y + 166, 76, 22, "Switch To", 0);
    drawf(x + 176, y + 174, C_DARK, "%d task(s) running", n);
    rect(x + 6, y + 196, w - 12, 1, C_DARK);
    draw_str(x + 8, y + 202, "System", C_TEXT);
    u32 s = ticks / 100;
    drawf(x + 12, y + 216, C_TEXT, "Uptime  : %02u:%02u:%02u", (s / 3600) % 100, (s / 60) % 60, s % 60);
    u32 used_kb = (u32)((sizeof back + sizeof bgimg + sizeof fsdata + sizeof canvas + sizeof tg + sizeof app_mem + sizeof app_tmp + 65536) / 1024);
    drawf(x + 12, y + 230, C_TEXT, "Kernel  : %u KB of %u KB", used_kb, mem_kb ? mem_kb : used_kb);
    int bw = w - 24, fill = mem_kb ? (int)((u32)bw * used_kb / mem_kb) : bw;
    if (fill < 2) fill = 2;
    rect(x + 12, y + 242, bw, 8, C_LIGHT); frame(x + 12, y + 242, bw, 8, C_DARK);
    rect(x + 13, y + 243, fill > bw - 2 ? bw - 2 : fill, 6, RGB(60, 170, 70));
    drawf(x + 12, y + 256, C_TEXT, "Files   : %d / %d", fs_count(), fs_max_files);
    drawf(x + 12, y + 270, C_TEXT, "Disk    : %d drive(s), LeactedFS %s", n_drives(), fs_on_disk ? "on disk" : "in RAM");
    drawf(x + 12, y + 284, C_TEXT, "Display : 640x480  %u fps  [%s]", fps, themes[th_id].name);
}
static void task_click(int rx, int ry) {
    int w = CW(A_TASK);
    int row = 0;
    for (int a = 0; a < N_APPS && row < 11; a++) {
        if (!wins[a].open) continue;
        if (inrect(rx, ry, 7, 35 + row * 11 - 1, w - 14, 11)) { tsel = a; return; }
        row++;
    }
    if (inrect(rx, ry, 6, 166, 76, 22)) { if (tsel >= 0 && wins[tsel].open) { close_app(tsel); tsel = -1; } return; }
    if (inrect(rx, ry, 88, 166, 76, 22)) { if (tsel >= 0 && wins[tsel].open) raise_app(tsel); return; }
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
/* Network commands (run as background jobs) and the Browser           */
/* ------------------------------------------------------------------ */
static int tj_stage, tj_sent, tj_ok, tj_wait;
static u32 tj_t, tj_ip;
static char tj_host[100], tj_save[24];

static void net_hint(void) { tputs("No network card found.\nStart QEMU with: -nic user,model=rtl8139\n"); }
static int net_job_ok(void) {
    if (!net_have_nic) { net_hint(); return 0; }
    if (tjob || http_active()) { tputs("The network is busy. Wait, or press Ctrl+C.\n"); return 0; }
    return 1;
}
/* 1 = we have an address, 0 = still waiting, -1 = DHCP failed */
static int tj_wait_net(void) {
    if (net_ip) return 1;
    if (dhcp_state == 0) dhcp_start();
    return dhcp_state == 4 ? -1 : 0;
}
static void tjob_cancel(void) {
    if (tjob == TJ_WGET || tjob == TJ_LGET) http_cancel();
    tjob = TJ_NONE;
    tputs("^C\n");
}
static void dl_name(char *out, int max, const char *url, const char *given) {
    if (given && given[0]) { kstrcpy(out, given, max); return; }
    const char *p = url; int slash = 0;
    for (const char *c = url; *c && *c != '?' && *c != '#'; c++) {
        if (c[0] == ':' && c[1] == '/' && c[2] == '/') { p = c + 3; slash = 0; continue; }
        if (*c == '/') { if (!slash && p != url && c >= p) { slash = 1; } p = c + 1; }
    }
    char seg[32]; int l = 0;
    for (const char *c = p; *c && *c != '?' && *c != '#' && l < 23; c++) seg[l++] = *c;
    seg[l] = 0;
    for (int i = 0; i < l; i++) { char ch = seg[i]; if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_')) seg[i] = '_'; }
    /* a bare host name ("http://example.com") has no file name */
    int has_path = 0; for (const char *c = url; *c && *c != '?' && *c != '#'; c++) if (*c == '/' && c[1] != '/' && c > url && c[-1] != '/') has_path = 1;
    if (!l || !has_path) kstrcpy(seg, "index.html", sizeof seg);
    kstrcpy(out, seg, max);
}
static int dl_save(const char *name, char *msg, int mmax) {
    int i = fs_create(name);
    if (i < 0) { ksnprintf(msg, mmax, "LeactedFS is full - could not save %s", name); return -1; }
    u32 n = http_len > FS_SLOT ? FS_SLOT : http_len;
    int r = fs_store(i, http_body, n);
    if (r < 0) { ksnprintf(msg, mmax, "Disk write failed saving %s", name); return -1; }
    ksnprintf(msg, mmax, "Saved %s (%u bytes)%s", fsdir[i].name, n, http_len > n ? ", cut at 128 KB" : "");
    return 0;
}
static void cmd_ifconfig(void) {
    if (!net_have_nic) { net_hint(); return; }
    char b[20];
    tprintf("MAC      %02x:%02x:%02x:%02x:%02x:%02x\n", net_mac[0], net_mac[1], net_mac[2], net_mac[3], net_mac[4], net_mac[5]);
    net_fmt_ip(b, net_ip); tprintf("Address  %s\n", net_ip ? b : "(none)");
    net_fmt_ip(b, net_mask); tprintf("Mask     %s\n", net_ip ? b : "(none)");
    net_fmt_ip(b, net_gw); tprintf("Gateway  %s\n", net_gw ? b : "(none)");
    net_fmt_ip(b, net_dns); tprintf("DNS      %s\n", net_dns ? b : "(none)");
    tprintf("DHCP     %s\n", dhcp_state == 3 ? "bound" : dhcp_state == 4 ? "failed" : dhcp_state == 0 ? "idle" : "working...");
    tprintf("Packets  in %u  out %u  dropped %u\n", net_stat_rx, net_stat_tx, net_stat_drop);
}
static void cmd_dhcp(void) {
    if (!net_job_ok()) return;
    dhcp_start(); tjob = TJ_DHCP; tj_t = net_ms();
    tputs("Asking for an address...\n");
}
static void cmd_ping(char **av, int ac) {
    if (ac < 2) { tputs("Usage: ping HOST\n"); return; }
    if (!net_job_ok()) return;
    kstrcpy(tj_host, av[1], sizeof tj_host);
    tjob = TJ_PING; tj_stage = 0; tj_sent = 0; tj_ok = 0; tj_wait = 0; tj_t = net_ms();
}
static void cmd_nslookup(char **av, int ac) {
    if (ac < 2) { tputs("Usage: nslookup HOST\n"); return; }
    if (!net_job_ok()) return;
    kstrcpy(tj_host, av[1], sizeof tj_host);
    tjob = TJ_LOOKUP; tj_stage = 0; tj_t = net_ms();
}
static void cmd_wget(char **av, int ac) {
    if (ac < 2) { tputs("Usage: wget URL [NAME]\n"); return; }
    if (!net_job_ok()) return;
    tj_save[0] = 0; if (ac > 2) kstrcpy(tj_save, av[2], sizeof tj_save);
    http_start(av[1]); tjob = TJ_WGET; tj_t = net_ms();
    tputs("Downloading... (Ctrl+C stops)\n");
}
static void tjob_tick(void) {
    if (!tjob) return;
    u32 now = net_ms(); char b[20];
    switch (tjob) {
    case TJ_DHCP:
        if (dhcp_state == 3) { net_fmt_ip(b, net_ip); tprintf("Address %s\n", b); tjob = TJ_NONE; }
        else if (dhcp_state == 4) { tputs("No DHCP answer.\n"); tjob = TJ_NONE; }
        break;
    case TJ_LOOKUP:
        if (tj_stage == 0) {
            int r = tj_wait_net();
            if (r < 0) { tputs("No network address (DHCP failed).\n"); tjob = TJ_NONE; }
            else if (r > 0) { dns_start(tj_host); tj_stage = 1; }
        } else if (dns_state == 2) { net_fmt_ip(b, dns_ip); tprintf("%s has address %s\n", tj_host, b); tjob = TJ_NONE; }
        else if (dns_state == 3) { tprintf(dns_nx ? "Host not found: %s\n" : "Lookup failed: %s\n", tj_host); tjob = TJ_NONE; }
        break;
    case TJ_PING:
        if (tj_stage == 0) {
            int r = tj_wait_net();
            if (r < 0) { tputs("No network address (DHCP failed).\n"); tjob = TJ_NONE; }
            else if (r > 0) { dns_start(tj_host); tj_stage = 1; }
        } else if (tj_stage == 1) {
            if (dns_state == 2) { tj_ip = dns_ip; net_fmt_ip(b, tj_ip); tprintf("Pinging %s [%s]\n", tj_host, b); tj_stage = 2; tj_t = now - 1000; }
            else if (dns_state == 3) { tprintf("Cannot find %s\n", tj_host); tjob = TJ_NONE; }
        } else {
            if (!tj_wait && tj_sent < 4 && now - tj_t >= 1000) { ping_send(tj_ip, (u16)(++tj_sent)); tj_wait = 1; tj_t = now; }
            if (tj_wait) {
                if (ping_got == tj_sent) {
                    net_fmt_ip(b, ping_from);
                    if (ping_rtt < 10) tprintf("Reply from %s: time<10ms\n", b); else tprintf("Reply from %s: time=%ums\n", b, ping_rtt);
                    tj_ok++; tj_wait = 0;
                } else if (now - tj_t >= 2000) { tputs("Request timed out.\n"); tj_wait = 0; }
            }
            if (!tj_wait && tj_sent >= 4) { tprintf("%d sent, %d received\n", tj_sent, tj_ok); tjob = TJ_NONE; }
        }
        break;
    case TJ_LGET: lg_job_tick(); break;
    case TJ_WGET:
        if (http_active()) break;
        if (http_st == H_ERR) tprintf("Failed: %s\n", http_msg);
        else {
            char nm[24], msg[80];
            dl_name(nm, sizeof nm, http_url, tj_save);
            if (http_code >= 400) tprintf("The server answered %d - saving anyway.\n", http_code);
            dl_save(nm, msg, sizeof msg); tputs(msg); tputc('\n');
            int l = slen(nm);
            if (l > 5 && !kstrcmp(nm + l - 5, ".lapp")) tputs("Start it with: run NAME\n");
        }
        tjob = TJ_NONE;
        break;
    }
}

/* ---------------------------- Browser ------------------------------ */
#define BR_TXT 98304
#define BR_ROWS 4000
#define BR_LINKS 200
#define BR_TB 30                    /* toolbar height */
#define BR_SB 18                    /* status bar height */
enum { BK_NORMAL, BK_H1, BK_H2, BK_H3, BK_PRE, BK_HR };
struct brow { u32 off; u16 len; u8 kind, indent; };
static char br_t[BR_TXT]; static u8 br_lk[BR_TXT], br_bd[BR_TXT];
static struct brow br_rows[BR_ROWS];
static int br_nrows, br_col, br_lastsp, br_cols = 70, br_full, br_txt_lim = BR_TXT - 64, br_row_lim = BR_ROWS - 4;
static u32 br_n, br_rs;
static int br_bk, br_ind, br_pre, br_bold, br_cur_link, br_intitle, br_depth, br_ol[4], br_cnt[4];
static char br_links[BR_LINKS][160]; static int br_nlinks;
static char br_title[64], br_base[320], br_url[320], br_addr[320], br_status[80], br_wtitle[96];
static char br_hist[12][320];
static int br_nh, br_alen, br_focus = 1, br_scroll, br_sel = 1;     /* br_sel: the address bar text is selected */

static void br_push(u32 off, int len, int kind, int ind) {
    if (br_nrows >= br_row_lim) { br_full = 1; return; }
    br_rows[br_nrows].off = off; br_rows[br_nrows].len = (u16)len; br_rows[br_nrows].kind = (u8)kind; br_rows[br_nrows].indent = (u8)ind;
    br_nrows++;
}
static void br_endrow(void) {
    int len = (int)(br_n - br_rs);
    if (!br_pre) while (len > 0 && br_t[br_rs + (u32)len - 1] == ' ') len--;
    if (len > 0 || (br_pre && br_n > br_rs)) br_push(br_rs, len, br_bk, br_ind);
    br_rs = br_n; br_col = 0; br_lastsp = -1;
}
static void br_blank(void) {
    br_endrow();
    if (br_nrows && br_rows[br_nrows - 1].len) br_push(br_n, 0, 0, 0);
}
static void br_put(unsigned char c) {
    if (br_full || br_n >= (u32)br_txt_lim) { br_full = 1; return; }
    if (br_intitle) {
        int l = slen(br_title);
        if (c < 32) c = ' ';
        if (!(c == ' ' && (l == 0 || br_title[l - 1] == ' ')) && l < 62) { br_title[l] = (char)c; br_title[l + 1] = 0; }
        return;
    }
    if (br_pre) {
        if (c == '\r') return;
        if (c == '\n') { if (br_n == br_rs) br_push(br_n, 0, br_bk, br_ind); else br_endrow(); return; }
        if (c == '\t') { for (int k = 0; k < 4; k++) br_put(' '); return; }
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        if (br_n == br_rs || br_t[br_n - 1] == ' ') return;
        c = ' ';
    }
    if (c < 32 || c > 126) c = '?';
    br_t[br_n] = (char)c; br_lk[br_n] = (u8)br_cur_link; br_bd[br_n] = (u8)(br_bold > 0);
    br_n++; br_col++;
    if (c == ' ' && !br_pre) br_lastsp = (int)br_n - 1;
    int limit = (br_cols - br_ind) / (br_bk == BK_H1 ? 2 : 1);
    if (limit < 8) limit = 8;
    if (br_col > limit) {
        if (!br_pre && br_lastsp >= (int)br_rs) {
            br_push(br_rs, br_lastsp - (int)br_rs, br_bk, br_ind);
            br_rs = (u32)br_lastsp + 1; br_col = (int)(br_n - br_rs); br_lastsp = -1;
        } else {
            br_push(br_rs, (int)(br_n - 1 - br_rs), br_bk, br_ind);
            br_rs = br_n - 1; br_col = 1; br_lastsp = -1;
        }
    }
}
static void br_text(const char *s) { while (*s) br_put((unsigned char)*s++); }
static void br_cp(u32 cp) {
    if (cp < 128) { br_put((unsigned char)cp); return; }
    switch (cp) {
    case 0xA0: br_put(' '); break;
    case 0x2018: case 0x2019: case 0x2032: br_put('\''); break;
    case 0x201C: case 0x201D: br_put('"'); break;
    case 0x2013: case 0x2014: br_put('-'); break;
    case 0x2026: br_text("..."); break;
    case 0x2022: case 0xB7: br_put('*'); break;
    case 0xA9: br_text("(c)"); break;
    case 0xAB: br_text("<<"); break;
    case 0xDF: br_text("ss"); break;
    case 0xBB: br_text(">>"); break;
    default:
        if (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7) {      /* accented Latin letters -> plain letters */
            static const char base[] = "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
            br_put((unsigned char)base[cp - 0xC0]);
        } else br_put('?');
    }
}
static u32 br_entity(const u8 *d, u32 n, u32 i, u32 *adv) {       /* d[i] == '&'; 0 if it is not an entity */
    u32 j = i + 1; char nm[10]; int l = 0;
    while (j < n && d[j] != ';' && l < 9 && d[j] > ' ') nm[l++] = (char)d[j++];
    if (j >= n || d[j] != ';' || !l) return 0;
    nm[l] = 0; *adv = j - i + 1;
    if (nm[0] == '#') {
        u32 v = 0;
        if (nm[1] == 'x' || nm[1] == 'X') { for (int k = 2; k < l; k++) { char c = nm[k]; v = v * 16 + (u32)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); } }
        else for (int k = 1; k < l; k++) v = v * 10 + (u32)(nm[k] - '0');
        return v ? v : '?';
    }
    static const struct { const char *n; u32 cp; } tab[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 }, { "copy", 0xA9 },
        { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "laquo", 0xAB }, { "raquo", 0xBB },
        { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 }, { "middot", 0xB7 },
    };
    for (unsigned k = 0; k < sizeof tab / sizeof tab[0]; k++) if (!kstrcmp(nm, tab[k].n)) return tab[k].cp;
    return '?';
}
static u32 br_utf8(const u8 *d, u32 n, u32 i, u32 *adv) {
    u8 c = d[i]; int l = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    u32 cp = l == 2 ? (u32)(c & 0x1F) : l == 3 ? (u32)(c & 0x0F) : l == 4 ? (u32)(c & 7) : c;
    if (l > 1 && i + (u32)l > n) l = 1;
    for (int k = 1; k < l; k++) { if ((d[i + (u32)k] & 0xC0) != 0x80) { l = 1; break; } cp = cp << 6 | (d[i + (u32)k] & 0x3F); }
    *adv = (u32)l;
    return l == 1 ? '?' : cp;
}
static int br_attr(const u8 *t, u32 len, const char *key, char *out, int max) {
#define WS(ch) ((ch) == ' ' || (ch) == '\t' || (ch) == '\n' || (ch) == '\r')
    u32 i = 0;
    while (i < len && !WS(t[i]) && t[i] != '/') i++;                       /* tag name */
    while (i < len) {
        while (i < len && (WS(t[i]) || t[i] == '/')) i++;
        u32 ks = i; while (i < len && t[i] != '=' && !WS(t[i]) && t[i] != '/') i++;
        u32 ke = i;
        while (i < len && WS(t[i])) i++;
        u32 vs = i, ve = i; int has = 0;
        if (i < len && t[i] == '=') {
            i++; while (i < len && WS(t[i])) i++;
            if (i < len && (t[i] == '"' || t[i] == '\'')) { u8 q = t[i++]; vs = i; while (i < len && t[i] != q) i++; ve = i; if (i < len) i++; }
            else { vs = i; while (i < len && !WS(t[i])) i++; ve = i; }
            has = 1;
        }
        int kl = slen(key);
        if (has && (int)(ke - ks) == kl) {
            int eq = 1; for (int k = 0; k < kl; k++) if (klower((char)t[ks + (u32)k]) != key[k]) eq = 0;
            if (eq) { int l = 0; for (u32 k = vs; k < ve && l < max - 1; k++) out[l++] = (char)t[k]; out[l] = 0; return 1; }
        }
    }
    return 0;
#undef WS
}
static void br_tag(const u8 *t, u32 len) {
    int closing = 0; u32 i = 0;
    if (len && t[0] == '/') { closing = 1; i = 1; }
    char nm[12]; int nl = 0;
    while (i < len && nl < 11 && ((t[i] >= 'a' && t[i] <= 'z') || (t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= '0' && t[i] <= '9'))) nm[nl++] = klower((char)t[i++]);
    nm[nl] = 0;
    if (!nl) return;
#define IS(x) (!kstrcmp(nm, x))
    if (IS("br")) { if (br_n == br_rs) br_push(br_n, 0, br_bk, br_ind); else br_endrow(); }
    else if (nl == 2 && nm[0] == 'h' && nm[1] >= '1' && nm[1] <= '6') {
        br_blank(); br_bk = closing ? BK_NORMAL : nm[1] == '1' ? BK_H1 : nm[1] <= '3' ? BK_H2 : BK_H3; br_bold = closing ? 0 : 1;
    }
    else if (IS("ul") || IS("ol")) {
        if (!closing) { if (br_depth < 4) { br_ol[br_depth] = IS("ol"); br_cnt[br_depth] = 0; br_depth++; br_ind += 2; } br_endrow(); }
        else { br_endrow(); if (br_depth > 0) { br_depth--; br_ind -= 2; } if (br_depth == 0) br_blank(); }
    }
    else if (IS("li")) {
        if (!closing) {
            br_endrow();
            char b[12]; int d = br_depth > 0 ? br_depth - 1 : 0;
            if (br_depth && br_ol[d]) ksnprintf(b, sizeof b, "%d. ", ++br_cnt[d]); else kstrcpy(b, "* ", sizeof b);
            br_text(b);
        }
    }
    else if (IS("blockquote")) { br_blank(); br_ind += closing ? -4 : 4; if (br_ind < 0) br_ind = 0; if (br_ind > 24) br_ind = 24; }
    else if (IS("p") || IS("dl") || IS("form") || IS("center") || IS("table")) br_blank();
    else if (IS("div") || IS("section") || IS("article") || IS("header") || IS("footer") || IS("nav") || IS("main") || IS("aside") ||
             IS("address") || IS("figure") || IS("figcaption") || IS("details") || IS("summary") || IS("dt") || IS("dd")) br_endrow();
    else if (IS("tr")) br_endrow();
    else if (IS("td") || IS("th")) { if (!closing) { if (br_n > br_rs) br_text(" | "); if (IS("th")) br_bold = 1; } else if (IS("th")) br_bold = 0; }
    else if (IS("pre")) { br_blank(); br_pre = !closing; br_bk = closing ? BK_NORMAL : BK_PRE; }
    else if (IS("hr")) { br_blank(); br_push(br_n, 0, BK_HR, 0); }
    else if (IS("b") || IS("strong")) { br_bold += closing ? -1 : 1; if (br_bold < 0) br_bold = 0; }
    else if (IS("title")) br_intitle = !closing;
    else if (IS("a")) {
        if (closing) br_cur_link = 0;
        else {
            char h[200];
            br_cur_link = 0;
            if (br_attr(t, len, "href", h, sizeof h) && h[0] && h[0] != '#' && !n_starts(h, "javascript:") && !n_starts(h, "mailto:") && br_nlinks < BR_LINKS - 1) {
                url_join(br_base, h, br_links[br_nlinks + 1], sizeof br_links[0]);
                br_cur_link = ++br_nlinks;
            }
        }
    }
    else if (IS("img")) {
        char alt[60];
        if (!br_attr(t, len, "alt", alt, sizeof alt) || !alt[0]) kstrcpy(alt, "image", sizeof alt);
        br_put('['); br_text(alt); br_put(']');
    }
#undef IS
}
static void br_html(const u8 *d, u32 n) {
    u32 i = 0;
    while (i < n && !br_full) {
        u8 c = d[i];
        if (c == '<' && i + 1 < n && (d[i + 1] == '!' || d[i + 1] == '/' || d[i + 1] == '?' || ((d[i + 1] | 32) >= 'a' && (d[i + 1] | 32) <= 'z'))) {
            if (d[i + 1] == '!' && i + 3 < n && d[i + 2] == '-' && d[i + 3] == '-') {          /* comment */
                i += 4; while (i + 2 < n && !(d[i] == '-' && d[i + 1] == '-' && d[i + 2] == '>')) i++;
                i += 3; continue;
            }
            u32 j = i + 1; u8 q = 0;
            while (j < n) { if (q) { if (d[j] == q) q = 0; } else if (d[j] == '"' || d[j] == '\'') q = d[j]; else if (d[j] == '>') break; j++; }
            if (d[i + 1] != '!' && d[i + 1] != '?') {
                br_tag(d + i + 1, j > i + 1 ? j - i - 1 : 0);
                int sc = n_starts((const char *)d + i + 1, "script"), st = n_starts((const char *)d + i + 1, "style");
                if (sc || st) {                                                              /* skip the code */
                    const char *end = sc ? "</script" : "</style";
                    u32 k = j + 1;
                    while (k < n && !(d[k] == '<' && n_starts((const char *)d + k, end))) k++;
                    while (k < n && d[k] != '>') k++;
                    i = k + 1; continue;
                }
            }
            i = j + 1; continue;
        }
        if (c == '&') { u32 adv = 0, cp = br_entity(d, n, i, &adv); if (cp) { br_cp(cp); i += adv; continue; } }
        if (c >= 0x80) { u32 adv = 1, cp = br_utf8(d, n, i, &adv); br_cp(cp); i += adv; continue; }
        br_put(c); i++;
    }
}
static void br_reset_page(void) {
    br_n = br_rs = 0; br_nrows = 0; br_col = 0; br_lastsp = -1; br_full = 0; br_bk = BK_NORMAL; br_ind = 0; br_pre = 0; br_bold = 0;
    br_cur_link = 0; br_intitle = 0; br_depth = 0; br_nlinks = 0; br_title[0] = 0; br_scroll = 0;
    br_txt_lim = BR_TXT - 64; br_row_lim = BR_ROWS - 4;
}
static void br_notice(const char *msg) {          /* a line after the page, even when the page filled the buffers */
    br_endrow(); br_txt_lim = BR_TXT - 2; br_row_lim = BR_ROWS; br_full = 0;
    br_blank(); br_text(msg); br_endrow();
}
static void br_set_title(void) {
    if (br_title[0]) ksnprintf(br_wtitle, sizeof br_wtitle, "%s - Browser", br_title); else kstrcpy(br_wtitle, "Browser", sizeof br_wtitle);
    wins[A_BROWSER].title = br_wtitle;
}
static void br_layout(const u8 *d, u32 n, int html, const char *base) {
    br_reset_page();
    br_cols = (CW(A_BROWSER) - 16 - 8) / 8;
    kstrcpy(br_base, base, sizeof br_base);
    if (html) br_html(d, n);
    else { br_pre = 1; br_bk = BK_PRE; for (u32 i = 0; i < n && !br_full; i++) br_put(d[i]); }
    br_endrow(); br_pre = 0; br_bk = BK_NORMAL;
    if (!br_nrows) { br_push(br_n, 0, 0, 0); }
    br_set_title();
}
static void br_show_html(const char *html, const char *url) {
    br_layout((const u8 *)html, (u32)slen(html), 1, url);
    kstrcpy(br_url, url, sizeof br_url); kstrcpy(br_addr, url, sizeof br_addr); br_alen = slen(br_addr);
}
static void br_show_home(void) {
    static const char home[] =
        "<html><title>LeactedOS Browser</title><body><h1>LeactedOS Browser</h1>"
        "<p>Type an address above and press Enter. Words without a dot are searched for.</p>"
        "<p><b>Plain http:// sites only.</b> HTTPS needs a TLS stack, which LeactedOS does not have yet, "
        "so many modern sites will not open.</p><hr><h2>Sites that may work</h2><ul>"
        "<li><a href=\"http://example.com/\">example.com</a></li>"
        "<li><a href=\"http://neverssl.com/\">neverssl.com</a> - a site that never uses https</li>"
        "<li><a href=\"http://info.cern.ch/\">info.cern.ch</a> - the first website</li>"
        "<li><a href=\"http://frogfind.com/\">frogfind.com</a> - a search engine for simple browsers</li></ul>"
        "<h2>Keys</h2><pre>Up/Down/PgUp/PgDn  scroll\nBackspace          back\nEnter              go (in the address bar)\nCtrl+C / Esc       stop loading</pre>"
        "</body></html>";
    br_show_html(home, "about:home");
    kstrcpy(br_status, "Welcome", sizeof br_status);
}
static void br_show_message(const char *head, const char *msg, const char *url) {
    char tmp[600];
    ksnprintf(tmp, sizeof tmp, "<html><title>%s</title><body><h2>%s</h2><p>%s</p><p>%s</p></body></html>", head, head, msg, url);
    br_show_html(tmp, "about:error");
    kstrcpy(br_url, url, sizeof br_url); kstrcpy(br_addr, url, sizeof br_addr); br_alen = slen(br_addr);
}
static int br_is_text_type(const char *ct) {
    return !ct[0] || n_starts(ct, "text/") || n_starts(ct, "application/xhtml") || n_starts(ct, "application/json") ||
           n_starts(ct, "application/xml") || n_starts(ct, "application/javascript");
}
static void br_show_http(void) {
    char url[320]; kstrcpy(url, http_url, sizeof url);
    http_body[http_len] = 0;
    if (!br_is_text_type(http_ctype)) {
        char nm[24], msg[100], page[400];
        dl_name(nm, sizeof nm, url, 0);
        dl_save(nm, msg, sizeof msg);
        ksnprintf(page, sizeof page, "<h2>Download</h2><p>This is not a web page (%s).</p><p>%s</p><p>Open it from the File Manager.</p>", http_ctype, msg);
        br_show_html(page, "about:download");
        kstrcpy(br_url, url, sizeof br_url); kstrcpy(br_addr, url, sizeof br_addr); br_alen = slen(br_addr);
        kstrcpy(br_status, msg, sizeof br_status);
        return;
    }
    int html = n_starts(http_ctype, "text/html") || n_starts(http_ctype, "application/xhtml");
    if (!html && !n_starts(http_ctype, "text/plain") && http_ctype[0] == 0) {
        for (u32 i = 0; i < http_len && i < 512; i++) { if (http_body[i] == '<') { html = 1; break; } if (http_body[i] > ' ') break; }
    }
    br_layout(http_body, http_len, html, url);
    if (http_trunc || br_full) br_notice("[Page cut off - too big]");
    kstrcpy(br_url, url, sizeof br_url); kstrcpy(br_addr, url, sizeof br_addr); br_alen = slen(br_addr);
    if (http_code >= 400) ksnprintf(br_status, sizeof br_status, "Server says %d  (%u bytes)", http_code, http_len);
    else ksnprintf(br_status, sizeof br_status, "Done  (%u bytes)", http_len);
}
static void br_search_url(char *out, int max, const char *q) {
    int o = 0; const char *pre = "http://frogfind.com/?q=";
    while (*pre && o < max - 1) out[o++] = *pre++;
    for (; *q && o < max - 4; q++) {
        char c = *q;
        if (c == ' ') out[o++] = '+';
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out[o++] = c;
        else { out[o++] = '%'; out[o++] = "0123456789ABCDEF"[(u8)c >> 4]; out[o++] = "0123456789ABCDEF"[(u8)c & 15]; }
    }
    out[o] = 0;
}
static void br_go(const char *in, int push) {
    char url[330];
    while (*in == ' ') in++;
    if (!*in) return;
    if (!kstrcmp(in, "about:home")) { br_show_home(); br_focus = 1; br_sel = 1; return; }
    if (tjob || http_active()) { kstrcpy(br_status, "The network is busy - wait or press Esc", sizeof br_status); return; }
    if (!net_have_nic) { br_show_message("No network", "No network card was found. Start QEMU with:  -nic user,model=rtl8139", in); br_focus = 1; return; }
    int scheme = 0, dot = 0, sp = 0;
    for (const char *c = in; *c; c++) { if (c[0] == ':' && c[1] == '/' && c[2] == '/') scheme = 1; if (*c == '.' || *c == ':') dot = 1; if (*c == ' ') sp = 1; }
    if (scheme) kstrcpy(url, in, sizeof url);
    else if (sp || !dot) br_search_url(url, sizeof url, in);
    else { kstrcpy(url, "http://", sizeof url); kstrcpy(url + 7, in, (int)sizeof url - 7); }
    if (push && br_url[0] && br_nh < 12) kstrcpy(br_hist[br_nh++], br_url, sizeof br_hist[0]);
    else if (push && br_url[0]) { memmove(br_hist[0], br_hist[1], 11 * sizeof br_hist[0]); kstrcpy(br_hist[11], br_url, sizeof br_hist[0]); }
    kstrcpy(br_addr, url, sizeof br_addr); br_alen = slen(br_addr);
    http_start(url); br_wait = 1; br_focus = 0; br_sel = 0;
    kstrcpy(br_status, "Starting...", sizeof br_status);
}
static void br_back(void) {
    if (!br_nh) return;
    char u[320]; kstrcpy(u, br_hist[--br_nh], sizeof u);
    br_go(u, 0);
}
static void br_stop(void) {
    if (br_wait) { http_cancel(); br_wait = 0; kstrcpy(br_status, "Stopped", sizeof br_status); }
}
static void br_check(void) {                    /* called every frame */
    if (!br_wait || http_active()) return;
    br_wait = 0;
    if (http_st == H_DONE) br_show_http();
    else {
        char u[320]; kstrcpy(u, http_url, sizeof u);
        if (!kstrcmp(http_msg, "Stopped")) { kstrcpy(br_status, "Stopped", sizeof br_status); return; }
        br_show_message("Cannot open this page", http_msg, u);
        kstrcpy(br_status, http_msg, sizeof br_status);
    }
}
static void br_open(void) {
    br_nh = 0; br_wait = 0; br_url[0] = 0; br_focus = 1; br_sel = 1;
    br_show_home();
}
static int br_rowh(int r) { return br_rows[r].kind == BK_H1 ? 20 : 10; }
static int br_viewh(void) { return CH(A_BROWSER) - BR_TB - BR_SB - 2; }
static int br_maxscroll(void) {
    int vh = br_viewh(), s = br_nrows, used = 0;
    while (s > 0 && used + br_rowh(s - 1) <= vh) { used += br_rowh(s - 1); s--; }
    return s;
}
static void br_scrollby(int d) {
    br_scroll += d;
    int mx = br_maxscroll();
    if (br_scroll > mx) br_scroll = mx;
    if (br_scroll < 0) br_scroll = 0;
}
static void br_draw(int x, int y, int w, int h) {
    /* toolbar */
    button(x + 4, y + 4, 26, 22, "<", 0);
    int fx = x + 36, fw = w - 36 - 62;
    rect(fx, y + 4, fw, 22, C_LIGHT); frame(fx - 1, y + 3, fw + 2, 24, C_DARK);
    int maxc = (fw - 8) / 8;
    const char *shown = br_addr;
    if (br_alen > maxc) shown = br_addr + br_alen - maxc;
    int ex;
    if (br_focus && br_sel && br_alen) { rect(fx + 2, y + 7, br_alen * 8 + 4 > fw - 4 ? fw - 4 : br_alen * 8 + 4, 14, RGB(49, 106, 197)); ex = draw_str(fx + 4, y + 11, shown, C_LIGHT); }
    else { ex = draw_str(fx + 4, y + 11, shown, C_BLACK); if (br_focus && ((ticks / 50) & 1)) rect(ex, y + 9, 2, 12, C_BLACK); }
    button(x + w - 56, y + 4, 52, 22, br_wait ? "Stop" : "Go", 0);
    /* page */
    int vy = y + BR_TB, vh = br_viewh() + 2, vw = w - 14;
    rect(x, vy, vw, vh, C_LIGHT);
    int py = vy + 3;
    for (int r = br_scroll; r < br_nrows && py + br_rowh(r) <= vy + vh + 1; r++) {
        const struct brow *b = &br_rows[r];
        int sc = b->kind == BK_H1 ? 2 : 1, px = x + 4 + b->indent * 8;
        if (b->kind == BK_HR) { rect(x + 8, py + 4, vw - 16, 1, C_DARK); py += 10; continue; }
        if (b->kind == BK_PRE) rect(x + 2, py - 1, vw - 4, 10, RGB(240, 240, 236));
        for (int i = 0; i < b->len; i++) {
            u32 o = b->off + (u32)i; int cx = px + i * 8 * sc;
            if (cx > x + vw) break;
            u32 col = br_lk[o] ? RGB(0, 0, 220) : b->kind == BK_H1 ? RGB(0, 40, 130) : b->kind <= BK_H3 && b->kind != BK_NORMAL ? RGB(20, 50, 120) : b->kind == BK_PRE ? RGB(60, 60, 60) : C_BLACK;
            draw_char_s(cx, py, br_t[o], col, sc);
            if (br_bd[o] && sc == 1) draw_char_s(cx + 1, py, br_t[o], col, 1);
            if (br_lk[o]) rect(cx, py + 9 * sc - (sc > 1 ? 2 : 0), 8 * sc, 1, col);
        }
        py += br_rowh(r);
    }
    /* scrollbar */
    int sx = x + vw;
    rect(sx, vy, 14, vh, RGB(220, 220, 214)); frame(sx, vy, 14, vh, C_DARK);
    int mx = br_maxscroll();
    if (br_nrows > 0) {
        int th = mx > 0 ? vh * (vh / 10) / (br_nrows > 1 ? br_nrows : 1) : vh - 4;
        if (th < 14) th = 14;
        if (th > vh - 4) th = vh - 4;
        int ty = vy + 2 + (mx > 0 ? (vh - 4 - th) * br_scroll / mx : 0);
        bevel(sx + 1, ty, 12, th, 0);
    }
    /* status */
    int sy = y + h - BR_SB;
    rect(x, sy, w, BR_SB, C_FACE); rect(x, sy, w, 1, C_DARK);
    char st[100];
    if (br_wait) ksnprintf(st, sizeof st, "%s  %u KB", http_msg, http_len / 1024); else kstrcpy(st, br_status, sizeof st);
    draw_str(x + 6, sy + 5, st, C_BLACK);
    if (tjob) draw_str(x + w - 140, sy + 5, "(terminal busy)", RGB(160, 60, 0));
}
static void br_key(int k) {
    if (k == K_ESC || k == 3) {
        if (br_wait) { br_stop(); return; }
        if (br_focus) { br_focus = 0; return; }
    }
    if (br_focus) {
        if (k >= 32 && k < 127) { if (br_sel) { br_alen = 0; br_addr[0] = 0; br_sel = 0; } if (br_alen < 300) { br_addr[br_alen++] = (char)k; br_addr[br_alen] = 0; } }
        else if (k == '\b') { if (br_sel) { br_alen = 0; br_addr[0] = 0; br_sel = 0; } else if (br_alen) br_addr[--br_alen] = 0; }
        else if (k == '\n') { char u[320]; kstrcpy(u, br_addr, sizeof u); br_go(u, 1); }
        else if (k == K_PGUP) br_scrollby(-(br_viewh() / 10 - 1));
        else if (k == K_PGDN) br_scrollby(br_viewh() / 10 - 1);
        else if (k == K_UP) br_scrollby(-1);
        else if (k == K_DOWN) br_scrollby(1);
        return;
    }
    int page = br_viewh() / 10 - 1;
    if (k == K_UP) br_scrollby(-1);
    else if (k == K_DOWN) br_scrollby(1);
    else if (k == K_PGUP) br_scrollby(-page);
    else if (k == K_PGDN || k == ' ') br_scrollby(page);
    else if (k == K_HOME) br_scrollby(-100000);
    else if (k == K_END) br_scrollby(100000);
    else if (k == '\b') br_back();
    else if (k == 12 || k == '/' || k == '\n') { br_focus = 1; br_sel = 1; }
}
static void br_click(int rx, int ry) {
    int w = CW(A_BROWSER);
    if (ry < BR_TB) {
        if (inrect(rx, ry, 4, 4, 26, 22)) br_back();
        else if (inrect(rx, ry, w - 56, 4, 52, 22)) { if (br_wait) br_stop(); else { char u[320]; kstrcpy(u, br_addr, sizeof u); br_go(u, 1); } }
        else if (inrect(rx, ry, 36, 4, w - 36 - 62, 22)) { if (!br_focus) br_sel = 1; br_focus = 1; }
        return;
    }
    br_focus = 0;
    int vh = br_viewh() + 2, vw = w - 14;
    if (rx >= vw) {                                              /* scrollbar: click above/below the thumb */
        if (ry < BR_TB + vh / 2) br_scrollby(-(br_viewh() / 10 - 1)); else br_scrollby(br_viewh() / 10 - 1);
        return;
    }
    int y = BR_TB + 3;
    for (int r = br_scroll; r < br_nrows && y < BR_TB + vh; r++) {
        int rh = br_rowh(r);
        if (ry >= y && ry < y + rh) {
            const struct brow *b = &br_rows[r];
            int sc = b->kind == BK_H1 ? 2 : 1, col = (rx - 4 - b->indent * 8) / (8 * sc);
            if (rx >= 4 + b->indent * 8 && col >= 0 && col < b->len) {
                int id = br_lk[b->off + (u32)col];
                if (id) { char u[160]; kstrcpy(u, br_links[id], sizeof u); br_go(u, 1); }
            }
            return;
        }
        y += rh;
    }
}
static void cmd_proxy(char **av, int ac) {
    if (ac > 1) {
        if (!kstrcmp(av[1], "off")) http_proxy_on = 0;
        else if (!kstrcmp(av[1], "on")) http_proxy_on = 1;
        else {
            char *c = av[1]; int l = 0; while (c[l] && c[l] != ':') l++;
            if (l < 1 || l > 22) { tputs("Usage: proxy [on|off|HOST[:PORT]]\n"); return; }
            u32 port = 8080;
            if (c[l] == ':') { port = 0; for (const char *d = c + l + 1; *d >= '0' && *d <= '9'; d++) port = port * 10 + (u32)(*d - '0'); if (!port || port > 65535) { tputs("Bad port.\n"); return; } }
            memcpy(http_proxy_host, c, (size_t)l); http_proxy_host[l] = 0; http_proxy_port = (u16)port; http_proxy_on = 1;
        }
    }
    tprintf("HTTPS gateway: %s  %s:%u\n", http_proxy_on ? "on" : "off", http_proxy_host, (u32)http_proxy_port);
    if (http_proxy_on) tputs("https:// goes through it. Run on your PC:\n  python3 tools/leact-gateway.py\n");
    else tputs("https:// pages cannot be opened while it is off.\n");
}
static int net_command(const char *c, char **av, int ac) {
    if (!kstrcmp(c, "proxy") || !kstrcmp(c, "gateway")) { cmd_proxy(av, ac); return 1; }
    if (!kstrcmp(c, "ifconfig") || !kstrcmp(c, "ipconfig") || !kstrcmp(c, "net")) cmd_ifconfig();
    else if (!kstrcmp(c, "dhcp")) cmd_dhcp();
    else if (!kstrcmp(c, "ping")) cmd_ping(av, ac);
    else if (!kstrcmp(c, "nslookup") || !kstrcmp(c, "dig")) cmd_nslookup(av, ac);
    else if (!kstrcmp(c, "wget") || !kstrcmp(c, "download")) cmd_wget(av, ac);
    else if (!kstrcmp(c, "browse") || !kstrcmp(c, "browser") || !kstrcmp(c, "web")) {
        open_app(A_BROWSER);
        if (ac > 1) br_go(av[1], 1);
    }
    else return 0;
    return 1;
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
    }
    else if (a == A_SETTINGS) set_msg[0] = 0;
    else if (a == A_TASK) tsel = -1;
    else if (a == A_CALC) calc_reset();
    else if (a == A_BROWSER) br_open();
    else if (a == A_FILES) { if (fm_drive < 0 || (fm_drive != FM_RAM && !drv[fm_drive].present)) fm_pick_drive(fs_on_disk ? fs_drive : FM_RAM); else fm_refresh(); }
    else if (a == A_INSTALL) { inst_state = 0; install_pick_default(); }
    else if (a >= A_EXT0) { if (xcall(a, EV_INIT, 0, 0, 0, 0)) xapp[a - A_EXT0].want_close = 1; }
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
    case A_BROWSER: br_draw(x, y, w, h); break;
    case A_OPENWITH: ow_draw(x, y, w, h); break;
    case A_HEX: hex_draw(x, y, w, h); break;
    case A_IMG: img_draw(x, y, w, h); break;
    default:
        if (a >= A_EXT0) { xdraw = 1; xox = x; xoy = y; xcall(a, EV_DRAW, 0, 0, 0, 0); xdraw = 0; }
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
    case A_BROWSER: br_click(rx, ry); break;
    case A_OPENWITH: ow_click(rx, ry); break;
    case A_HEX: hex_click(rx, ry); break;
    default: if (a >= A_EXT0) xcall(a, EV_CLICK, rx, ry, 0, 1);
    }
}
static void app_key(int a, int k) {
    if (a == A_TERM) term_key(k);
    else if (a == A_NOTE) note_key(k);
    else if (a == A_CALC) calc_key(k);
    else if (a == A_BROWSER) br_key(k);
    else if (a == A_FILES) files_key(k);
    else if (a == A_OPENWITH) ow_key(k);
    else if (a == A_HEX) hex_key(k);
    else if (a >= A_EXT0) xcall(a, EV_KEY, 0, 0, k, 0);
}

/* ------------------------------------------------------------------ */
/* Desktop, themes, panels, start menu                                 */
/* ------------------------------------------------------------------ */
#define UBAR_H 24            /* Unity top panel */
#define LAUNCH_W 52          /* Unity left launcher */
#define LAUNCH_SLOT 36
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

/* Start an app from a complete .lapp file in memory. Returns NULL or an error message. */
static const char *lapp_start(const u8 *file, u32 len, const char *fname, lapp_entry_fn native) {
    struct lapp_header h; const char *err = lapp_check(file, len, &h);
    if (err) return err;
    int s = 0; while (s < N_EXT && xapp[s].used) s++;
    if (s == N_EXT) return "Too many apps running (4 at most)";
    err = lapp_load(file, &h, app_mem[s], (u32)(uintptr_t)app_mem[s]);
    if (err) return err;
    struct xapp *p = &xapp[s];
    memset(p, 0, sizeof *p);
    p->used = 1; p->mem = app_mem[s]; p->entry = h.entry_off; p->native = native;
    kstrcpy(p->title, h.name[0] ? h.name : fname, sizeof p->title);
    int a = A_EXT0 + s; struct win *w = &wins[a];
    w->title = p->title; w->w = h.win_w + 6; w->h = h.win_h + 6 + TITLE_H;
    w->x = area_x0 + 30 + s * 26; w->y = area_y0 + 16 + s * 26;
    clamp_win(w);
    open_app(a);
    return NULL;
}
static const char *lapp_run_fref(const struct fref *f) {
    if (f->size > APP_FILE_MAX) return "App file is too big";
    u32 n = fref_load(f, app_tmp, APP_FILE_MAX);
    if (n != f->size) return "Could not read the app file";
    return lapp_start(app_tmp, n, f->name, 0);
}

static int start_open;
#define SM_W 200
#define SM_ITEM 24
#define SM_HEAD 30
#define SM_N (N_PIN + 2)
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

static const char *icon_label[12] = { "Terminal", "Notepad", "Paint", "Files", "Clock", "About", "Install", "Settings", "Tasks", "Calc", "Browser", "Reboot" };
#define N_ICONS 12
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
    case 10:  /* browser: globe */
        fill_circle(x + 16, y + 16, 15, RGB(40, 110, 220)); fill_circle(x + 11, y + 12, 5, RGB(70, 170, 80)); fill_circle(x + 21, y + 21, 6, RGB(70, 170, 80));
        fill_circle(x + 22, y + 9, 3, RGB(70, 170, 80)); rect(x + 3, y + 15, 26, 1, RGB(200, 225, 255)); rect(x + 15, y + 2, 1, 28, RGB(200, 225, 255)); break;
    case 11: fill_circle(x + 16, y + 16, 15, RGB(210, 60, 30)); fill_circle(x + 16, y + 16, 8, RGB(255, 255, 255)); fill_circle(x + 16, y + 16, 6, RGB(210, 60, 30)); rect(x + 14, y + 3, 4, 14, C_LIGHT); break;
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
    if (a >= N_PIN && a < A_EXT0) {      /* helper windows cannot be minimised */
        u32 c1 = focus ? th->ta1 : th->ti1, c2 = focus ? th->ta2 : th->ti2;
        min_rect(a, &bx, &by, &bw, &bh);
        for (int j = 0; j < bh; j++) rect(bx, by + j, bw, 1, mix(c1, c2, by + j - (w->y + 3), TITLE_H - 1));
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
    for (int i = 0; i < N_PIN; i++) {
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
    static const u32 bc[N_PIN] = { RGB(60, 60, 70), RGB(60, 110, 220), RGB(220, 60, 50), RGB(240, 200, 60), RGB(120, 120, 130),
                                    RGB(40, 100, 220), RGB(80, 160, 80), RGB(140, 146, 160), RGB(60, 170, 70), RGB(90, 100, 120), RGB(40, 110, 220) };
    for (int i = 0; i < SM_N; i++) {
        int iy = sm_y + SM_HEAD + i * SM_ITEM;
        const char *t = i < N_PIN ? app_names[i] : i == N_PIN ? "Restart" : "Shut Down";
        if (i == N_PIN) rect(sm_x + 8, iy - 1, SM_W - 16, 1, th->menu_edge);
        int hot = inrect(mx, my, sm_x, iy, SM_W, SM_ITEM);
        if (hot) rect(sm_x + 2, iy, SM_W - 4, SM_ITEM, th->menu_hl);
        if (i < N_PIN) { rect(sm_x + 10, iy + 7, 10, 10, bc[i]); frame(sm_x + 10, iy + 7, 10, 10, C_DARK); }
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
    if (i < N_PIN) open_app(i);
    else if (i == N_PIN) reboot_now();
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
            if (i >= 0 && i < N_PIN) toggle_app(i);
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
        if (inrect(x, y, bx, by, bw, bh)) { if (a < N_PIN || a >= A_EXT0) w->min = 1; return; }
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
            if (dbl) { if (i < N_PIN) open_app(i); else reboot_now(); }
            return;
        }
    }
}
static void on_release(void) {
    if (press_app >= A_EXT0 && wins[press_app].open) xcall(press_app, EV_RELEASE, mx - CX(press_app), my - CY(press_app), 0, 0);
    drag_app = -1; press_app = -1; plastx = -1;
}
static void on_move_held(int x, int y) {
    if (drag_app >= 0) {
        struct win *w = &wins[drag_app];
        w->x = x - drag_dx; w->y = y - drag_dy;
        clamp_win(w);
    } else if (press_app == A_PAINT && wins[A_PAINT].open) {
        paint_drag(x - CX(A_PAINT), y - CY(A_PAINT));
    } else if (press_app >= A_EXT0 && wins[press_app].open) {
        xcall(press_app, EV_MOVE, x - CX(press_app), y - CY(press_app), 0, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Frame / main                                                        */
/* ------------------------------------------------------------------ */
static void os_draw(void) {
    frame_count++;
    if (ticks - fps_t >= 100) { fps = (frame_count - fps_f) * 100 / (ticks - fps_t); fps_f = frame_count; fps_t = ticks; }
    clip_reset();
    xapp_tick();
    draw_desktop();
    for (int i = 0; i < nz; i++) if (!wins[zo[i]].min) draw_window(zo[i]);
    draw_bar();
    if (start_open) draw_startmenu();
    draw_cursor();
    xapp_reap();
}

static int prev_btn;
static void os_events(void) {
    xapp_reap();
    input_poll();
    net_poll(); tjob_tick(); br_check();
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
    ata_identify_all();
    for (int d = 0; d < N_DRIVES; d++) drive_probe(d);
    splash(55, "Disk");
    fs_check();
    fs_load();
    settings_load();
    net_init();
    splash(80, "Files and settings");
    theme_apply(th_id);
    splash(100, "Desktop");
    sleep_ticks(70);
    open_app(A_TERM);
    if (fs_on_disk && fs_ver == 1) tputs("Your LeactedFS disk is the old small version.\nType fsupgrade for 64 files of 128 KB.\n");
}

void kernel_main(u32 magic, const u32 *mbi) {
    os_init(magic, mbi);
    u32 next = ticks;
    for (;;) {
        os_events();
        os_draw();
        flip();
        next += 3;                                   /* ~33 fps */
        while ((i32)(ticks - next) < 0) { wait_tick(); input_poll(); net_poll(); }
        if ((i32)(ticks - next) > 30) next = ticks;  /* fell behind: resync */
    }
}
