/*
 * run32.c - runs real, compiled .lapp files as 32-bit code.
 *
 * Freestanding 32-bit Linux program (no libc).  It uses the same loader core
 * the kernel uses (sdk/lapp_load.h), relocates each app into two different
 * memory areas, implements the kernel API on a small software canvas and
 * drives the app with events.  Exit code 0 = every check passed.
 *
 *   run32 app.lapp [out.ppm]
 */
#include "../sdk/lapp_load.h"
#include "../font.h"

/* ---- raw Linux syscalls (int 0x80) ---- */
static int sc(int n, int a, int b, int c) {
    int r; __asm__ volatile("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b), "d"(c) : "memory"); return r;
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc(4, 2, (int)s, slen(s)); }
static void outn(int v) { char b[16]; int k = 15; b[k] = 0; unsigned u = v < 0 ? -v : v; do { b[--k] = '0' + u % 10; u /= 10; } while (u); if (v < 0) b[--k] = '-'; out(b + k); }
static int fails;
#define CHECK(c) do { if (!(c)) { out("  FAIL: " #c " (line "); outn(__LINE__); out(")\n"); fails++; } } while (0)

/* ---- software canvas the app draws into ---- */
#define CW 640
#define CH 480
static uint32_t cv[CW * CH];
static int ox, oy, ow, oh;          /* window contents rectangle on the canvas */
static int drawing;
static char tlog[4096]; static int tlen;       /* every string drawn with text() */
static char klog[512]; static int klen;        /* log() output */
static int nbeeps, last_hz, closed;
static uint32_t now = 1000, rnd_seq[32], rnd_i, rnd_state = 1;
static char fname[8][24]; static uint8_t fdata[8][4096]; static uint32_t fsize[8]; static int fused[8];

static void put(int x, int y, uint32_t c) {
    x += ox; y += oy;
    if (x < ox || y < oy || x >= ox + ow || y >= oy + oh) return;
    cv[y * CW + x] = c;
}
static void a_rect(int x, int y, int w, int h, uint32_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, c); }
static void a_frame(int x, int y, int w, int h, uint32_t c) { a_rect(x, y, w, 1, c); a_rect(x, y + h - 1, w, 1, c); a_rect(x, y, 1, h, c); a_rect(x + w - 1, y, 1, h, c); }
static void a_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { put(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
static void a_circle(int cx, int cy, int r, uint32_t c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r) put(cx + x, cy + y, c); }
static void a_pixel(int x, int y, uint32_t c) { put(x, y, c); }
static int a_text_scaled(int x, int y, const char *s, uint32_t c, int sc_) {
    if (drawing) { for (int i = 0; s[i] && tlen < (int)sizeof tlog - 2; i++) tlog[tlen++] = s[i]; tlog[tlen++] = '|'; tlog[tlen] = 0; }
    for (; *s; s++, x += 8 * sc_) {
        int ch = (unsigned char)*s; if (ch < 32 || ch > 127) ch = '?';
        for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b++) if (font8[ch - 32][r] & (0x80 >> b)) a_rect(x + b * sc_, y + r * sc_, sc_, sc_, c);
    }
    return x;
}
static int a_text(int x, int y, const char *s, uint32_t c) { return a_text_scaled(x, y, s, c, 1); }
static void a_button(int x, int y, int w, int h, const char *l, int p) {
    (void)p; a_rect(x, y, w, h, 0xD4D0C8); a_frame(x, y, w, h, 0x404040); a_text(x + (w - slen(l) * 8) / 2, y + (h - 8) / 2, l, 0);
}
static void a_blit(int x, int y, int w, int h, const uint32_t *s) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, s[j * w + i] & 0xFFFFFF); }
static uint32_t a_ticks(void) { return now; }
static uint32_t a_rand(void) { if (rnd_i < 32 && rnd_seq[rnd_i] != 0xFFFFFFFFu) return rnd_seq[rnd_i++]; rnd_state = rnd_state * 1103515245u + 12345u; return (rnd_state >> 8) ^ (now >> 3); }
static void a_beep(uint32_t hz, uint32_t ms) { (void)ms; nbeeps++; last_hz = hz; }
static void a_set_title(const char *s) { (void)s; }
static void a_close(void) { closed = 1; }
static void a_log(const char *s) { for (int i = 0; s[i] && klen < (int)sizeof klog - 1; i++) klog[klen++] = s[i]; klog[klen] = 0; }
static int nameeq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int ffind(const char *n) { for (int i = 0; i < 8; i++) if (fused[i] && nameeq(fname[i], n)) return i; return -1; }
static int a_file_read(const char *n, void *b, uint32_t max) { int i = ffind(n); if (i < 0) return -1; uint32_t k = fsize[i] < max ? fsize[i] : max; for (uint32_t j = 0; j < k; j++) ((uint8_t *)b)[j] = fdata[i][j]; return k; }
static int a_file_write(const char *n, const void *b, uint32_t len) {
    int i = ffind(n); if (i < 0) for (i = 0; i < 8 && fused[i]; i++) ;
    if (i == 8 || len > 4096) return -1;
    fused[i] = 1; for (int k = 0; k < 24; k++) { fname[i][k] = n[k]; if (!n[k]) break; }
    for (uint32_t j = 0; j < len; j++) fdata[i][j] = ((const uint8_t *)b)[j];
    fsize[i] = len;
    return 0;
}
static int a_file_delete(const char *n) { int i = ffind(n); if (i < 0) return -1; fused[i] = 0; return 0; }
static int a_file_name(int idx, char *o, uint32_t max) { int k = 0; for (int i = 0; i < 8; i++) if (fused[i]) { if (k++ == idx) { for (uint32_t j = 0; j < max; j++) { o[j] = fname[i][j]; if (!o[j]) break; } return fsize[i]; } } return -1; }

static struct leact_api api = {
    sizeof(struct leact_api), LAPP_API_VERSION,
    a_rect, a_frame, a_line, a_circle, a_pixel, a_text, a_text_scaled, a_button, a_blit,
    a_ticks, a_rand, a_beep, a_set_title, a_close, a_log,
    a_file_read, a_file_write, a_file_delete, a_file_name,
    0xD4D0C8, 0x000000, 0xFFFFFF, 0x716F64,
};

/* ---- two RWX arenas: the app must behave the same at both addresses ---- */
static uint8_t arena[2][LAPP_MAX_MEM] __attribute__((aligned(16)));
static uint8_t filebuf[LAPP_HDR_SIZE + LAPP_MAX_MEM + LAPP_MAX_RELOC * 4];

typedef int (*entry_fn)(const struct leact_api *, const struct leact_event *);
static entry_fn entry; static struct lapp_header hdr;
static int send(int type, int x, int y, int key) {
    struct leact_event ev = { type, x, y, key, type == EV_CLICK || type == EV_MOVE, ow, oh };
    drawing = type == EV_DRAW;
    int r = entry(&api, &ev);
    drawing = 0;
    return r;
}
static void redraw(void) { tlen = 0; tlog[0] = 0; send(EV_DRAW, 0, 0, 0); }
static int has(const char *needle) {
    for (int i = 0; tlog[i]; i++) { int k = 0; while (needle[k] && tlog[i + k] == needle[k]) k++; if (!needle[k]) return 1; }
    return 0;
}
static uint32_t hash_canvas(void) { uint32_t h = 2166136261u; for (int j = 0; j < oh; j++) for (int i = 0; i < ow; i++) h = (h ^ cv[(oy + j) * CW + ox + i]) * 16777619u; return h; }
static void reset_state(void) {
    for (int i = 0; i < CW * CH; i++) cv[i] = 0x123456;
    tlen = 0; klen = 0; klog[0] = 0; nbeeps = 0; closed = 0; now = 1000; rnd_i = 0; rnd_state = 1;
    for (int i = 0; i < 32; i++) rnd_seq[i] = 0xFFFFFFFFu;
    for (int i = 0; i < 8; i++) fused[i] = 0;
}
static uint32_t at(int x, int y) { return cv[(oy + y) * CW + ox + x]; }

/* ---- per-app scenarios ---- */
static uint32_t scenario_hello(void) {
    send(EV_INIT, 0, 0, 0);
    CHECK(klen > 0);                                   /* log() from INIT */
    redraw();
    CHECK(has("Hello, world!") && has("Clicks: 0   Last key: 0") && has("Running for 10 seconds"));
    now += 2500; redraw(); CHECK(has("Running for 35 seconds"));
    send(EV_KEY, 0, 0, 'x'); redraw(); CHECK(has("Last key: 120"));
    send(EV_CLICK, 20, oh - 30, 0); redraw();                     /* Beep button */
    CHECK(nbeeps == 1 && has("Beep #1") && has("Clicks: 1"));
    send(EV_CLICK, 100, oh - 30, 0); redraw();                    /* Files button, no files yet */
    CHECK(has("LeactedFS has 0 file(s)"));
    CHECK(at(ow - 40, 40) == 0xFFD228);                           /* smiley face colour */
    send(EV_CLICK, 200, oh - 30, 0);                              /* Close button */
    CHECK(closed);
    send(EV_CLOSE, 0, 0, 0);
    return hash_canvas();
}
static uint32_t scenario_snake(void) {
    rnd_seq[0] = 10; rnd_seq[1] = 7;                              /* food at 10,7: two steps ahead of the head */
    send(EV_INIT, 0, 0, 0);
    redraw(); CHECK(has("Score 0   Best 0"));
    for (int i = 0; i < 9 * 2; i++) send(EV_TICK, 0, 0, 0);       /* head 8 -> 10: eats */
    redraw(); CHECK(has("Score 1   Best 0"));
    CHECK(nbeeps >= 1 && last_hz == 880);
    send(EV_KEY, 0, 0, LK_DOWN);                                  /* turn */
    for (int i = 0; i < 9; i++) send(EV_TICK, 0, 0, 0);
    send(EV_KEY, 0, 0, LK_UP);                                    /* reversing is ignored */
    send(EV_KEY, 0, 0, 'a');                                      /* so is 'a' while going down?  no: left is allowed */
    for (int i = 0; i < 9 * 40; i++) send(EV_TICK, 0, 0, 0);      /* run into a wall */
    redraw(); CHECK(has("GAME OVER") && has("Best 1"));
    int f = ffind("SNAKE.SCR"); CHECK(f >= 0 && fsize[f] == 1 && fdata[f][0] == '1');
    uint32_t h = hash_canvas();
    send(EV_KEY, 0, 0, ' '); redraw();                            /* restart keeps the best score */
    CHECK(!has("GAME OVER") && has("Score 0   Best 1"));
    return h;
}
static uint32_t scenario_doodle(void) {
    send(EV_INIT, 0, 0, 0);
    redraw();
    CHECK(at(8 + 2 * 22 + 4, 10) == 0xE03030);                    /* palette swatch 2 is red */
    CHECK(at(20, 40) == 0xFFFFFF);                                /* canvas starts white */
    send(EV_CLICK, 8 + 2 * 22 + 4, 10, 0);                        /* pick red */
    send(EV_CLICK, 18, 52, 0);
    send(EV_MOVE, 40, 52, 0); send(EV_MOVE, 40, 90, 0);
    send(EV_RELEASE, 40, 90, 0);
    send(EV_MOVE, 100, 100, 0);                                   /* after release: no more painting */
    redraw();
    CHECK(at(30, 52 - 32 + 32) == 0xE03030);                      /* on the first stroke */
    CHECK(at(40 - 8 + 8, 70) == 0xE03030);                        /* on the second stroke */
    CHECK(at(100, 100) == 0xFFFFFF);
    send(EV_CLICK, 8 + 8 * 22 + 6, 10, 0);                        /* Clear */
    redraw(); CHECK(at(30, 52) == 0xFFFFFF);
    send(EV_CLICK, 8 + 5 * 22 + 4, 10, 0); send(EV_CLICK, 60, 60, 0); send(EV_RELEASE, 60, 60, 0);
    redraw();
    CHECK(at(60, 60) == 0x30B040);
    return hash_canvas();
}

static int rd_file(const char *path, uint8_t *buf, int max) {
    int fd = sc(5, (int)path, 0, 0);                              /* open(path, O_RDONLY) */
    if (fd < 0) return -1;
    int n = 0, r;
    while (n < max && (r = sc(3, fd, (int)(buf + n), max - n)) > 0) n += r;
    sc(6, fd, 0, 0);
    return n;
}
static void write_ppm(const char *path) {
    int fd = sc(5, (int)path, 0x241, 0644);                       /* O_WRONLY|O_CREAT|O_TRUNC */
    if (fd < 0) return;
    char head[32]; int k = 0;
    const char *p = "P6\n"; while (*p) head[k++] = *p++;
    int v[2] = { ow, oh };
    for (int q = 0; q < 2; q++) { char t[8]; int m = 0, n = v[q]; do { t[m++] = '0' + n % 10; n /= 10; } while (n); while (m) head[k++] = t[--m]; head[k++] = q ? '\n' : ' '; }
    const char *e = "255\n"; while (*e) head[k++] = *e++;
    sc(4, fd, (int)head, k);
    static uint8_t row[CW * 3];
    for (int j = 0; j < oh; j++) {
        for (int i = 0; i < ow; i++) { uint32_t c = cv[(oy + j) * CW + ox + i]; row[i * 3] = c >> 16; row[i * 3 + 1] = c >> 8; row[i * 3 + 2] = c; }
        sc(4, fd, (int)row, ow * 3);
    }
    sc(6, fd, 0, 0);
}

static void cell_click(int cx, int cy) { send(EV_CLICK, cx * 24 + 5, 34 + cy * 24 + 5, 0); }
static int is_mine(int x, int y) { return (y == 8 && x <= 7) || (y == 7 && x >= 7 && x <= 8); }
static void mines_seed(void) {         /* row 8 up to x=7, plus (7,7) and (8,7): the corner (8,8) is walled in */
    for (int i = 0; i < 8; i++) { rnd_seq[2 * i] = i; rnd_seq[2 * i + 1] = 8; }
    rnd_seq[16] = 7; rnd_seq[17] = 7; rnd_seq[18] = 8; rnd_seq[19] = 7; rnd_i = 0;
}
static uint32_t scenario_minesweeper(void) {
    send(EV_INIT, 0, 0, 0); redraw();
    CHECK(has("Mines 10") && has("New|") && has("Flag|"));
    CHECK(at(10, 34 + 10) == 0xC0C0C0);                           /* closed square */
    send(EV_CLICK, 130, 10, 0);                                   /* Flag mode */
    cell_click(3, 3); redraw(); CHECK(has("Mines 09"));
    cell_click(3, 3); redraw(); CHECK(has("Mines 10"));           /* second click removes it */
    send(EV_CLICK, 130, 10, 0);                                   /* back to opening mode */
    mines_seed();
    cell_click(0, 0); redraw();                                   /* first click: floods the safe area */
    CHECK(at(10, 34 + 10) == 0xDEDEDE && at(10, 34 + 8 * 24 + 10) == 0xC0C0C0 && at(8 * 24 + 10, 34 + 8 * 24 + 10) == 0xC0C0C0);
    redraw(); CHECK(!has("YOU WIN"));
    now += 1234; send(EV_TICK, 0, 0, 0); redraw(); CHECK(has("012|"));
    for (int y = 0; y < 9; y++) for (int x = 0; x < 9; x++) if (!is_mine(x, y)) cell_click(x, y);
    redraw(); CHECK(has("YOU WIN") && has("Best 12s"));
    int f = ffind("MINES.BST"); CHECK(f >= 0 && fsize[f] == 2 && fdata[f][0] == '1' && fdata[f][1] == '2');
    uint32_t h = hash_canvas();
    send(EV_KEY, 0, 0, 'n'); redraw(); CHECK(has("Mines 10") && has("Best 12s") && !has("YOU WIN"));
    mines_seed(); cell_click(0, 0); cell_click(0, 8);             /* step on a mine */
    redraw(); CHECK(has("BOOM") && at(4, 34 + 8 * 24 + 4) == 0xFF5050 && nbeeps >= 1);
    cell_click(5, 0); redraw(); CHECK(has("BOOM"));               /* the board is frozen */
    return h;
}
static uint32_t scenario_tetris(void) {
    rnd_seq[0] = 6; rnd_seq[1] = 5; rnd_seq[2] = 4; rnd_seq[3] = 3; rnd_seq[4] = 2; rnd_seq[5] = 1;
    send(EV_INIT, 0, 0, 0); redraw();
    CHECK(has("SCORE|0|") && has("LEVEL|1|") && has("NEXT"));
    send(EV_KEY, 0, 0, 'p'); redraw(); CHECK(has("PAUSED"));
    for (int i = 0; i < 100; i++) send(EV_TICK, 0, 0, 0);         /* paused: nothing moves */
    send(EV_KEY, 0, 0, 'p'); redraw(); CHECK(!has("PAUSED"));
    for (int i = 0; i < 30 * 20 * 14; i++) send(EV_TICK, 0, 0, 0);   /* gravity alone stacks the pieces up */
    redraw(); CHECK(has("GAME OVER") && ffind("TETRIS.SCR") < 0);
    send(EV_KEY, 0, 0, 'r'); redraw(); CHECK(!has("GAME OVER") && has("SCORE|0|"));
    send(EV_KEY, 0, 0, LK_LEFT); send(EV_KEY, 0, 0, LK_UP); send(EV_KEY, 0, 0, LK_RIGHT); send(EV_KEY, 0, 0, LK_DOWN);
    redraw(); CHECK(!has("SCORE|0|"));                            /* soft drop scored */
    for (int i = 0; i < 40; i++) send(EV_KEY, 0, 0, ' ');         /* hard drops until the stack reaches the top */
    redraw(); CHECK(has("GAME OVER") && nbeeps >= 1);
    int f = ffind("TETRIS.SCR"); CHECK(f >= 0 && fsize[f] >= 1 && fdata[f][0] >= '1' && fdata[f][0] <= '9');
    return hash_canvas();
}
static uint32_t scenario_2048(void) {
    rnd_seq[0] = 0; rnd_seq[1] = 1; rnd_seq[2] = 14; rnd_seq[3] = 1;   /* a 2 at the top left and one at the bottom right */
    rnd_seq[4] = 0; rnd_seq[5] = 1; rnd_seq[6] = 0; rnd_seq[7] = 1;    /* then new 2s in the first free squares */
    send(EV_INIT, 0, 0, 0); redraw();
    CHECK(at(12, 58) == 0xEEE4DA && at(12 + 3 * 76, 58 + 3 * 76) == 0xEEE4DA && at(12 + 76, 58) == 0xCDC1B4);
    send(EV_KEY, 0, 0, LK_LEFT); redraw();                        /* bottom 2 slides left, new 2 appears next to the top one */
    CHECK(at(12, 58 + 3 * 76) == 0xEEE4DA && at(12 + 76, 58) == 0xEEE4DA && has("SCORE|0|"));
    send(EV_KEY, 0, 0, LK_LEFT); redraw();                        /* the pair merges into a 4 */
    CHECK(at(12, 58) == 0xEDE0C8 && has("SCORE|4|"));
    int f = ffind("G2048.SCR"); CHECK(f >= 0 && fsize[f] == 1 && fdata[f][0] == '4');
    send(EV_KEY, 0, 0, 'r'); redraw(); CHECK(has("SCORE|0|") && has("BEST|4|"));
    for (int i = 0; i < 400; i++) send(EV_KEY, 0, 0, (i % 4) == 0 ? LK_LEFT : (i % 4) == 1 ? LK_DOWN : (i % 4) == 2 ? LK_RIGHT : LK_UP);
    redraw(); CHECK(has("GAME OVER") || !has("SCORE|0|"));       /* played on or jammed: either is fine, nothing crashed */
    return hash_canvas();
}
static uint32_t scenario_pong(void) {
    send(EV_INIT, 0, 0, 0); redraw(); CHECK(has("Space = serve"));
    CHECK(at(15, 116) == 0xF0F0F0 && at(15, 115) == 0x0A0A10);    /* left paddle starts centred */
    send(EV_KEY, 0, 0, LK_UP); redraw(); CHECK(at(15, 116 - 14) == 0xF0F0F0);
    send(EV_MOVE, 0, 200, 0); redraw(); CHECK(at(15, 200) == 0xF0F0F0);
    send(EV_MOVE, 0, 9999, 0); redraw(); CHECK(at(15, 279) == 0xF0F0F0);   /* clamped inside the table */
    for (int t = 0; t < 40000 && !has("YOU "); t++) {
        if (t % 50 == 0) { send(EV_KEY, 0, 0, ' '); }
        send(EV_TICK, 0, 0, 0);
        if (t % 50 == 49) redraw();
    }
    redraw(); CHECK(has("YOU WIN") || has("YOU LOSE")); CHECK(nbeeps > 3);
    uint32_t h = hash_canvas();
    send(EV_KEY, 0, 0, ' '); redraw(); CHECK(has("Space = serve") && !has("YOU "));
    return h;
}
static uint32_t scenario_stopwatch(void) {
    send(EV_INIT, 0, 0, 0); redraw(); CHECK(has("00:00.00") && has("STOPWATCH") && has("Start|"));
    send(EV_KEY, 0, 0, ' '); now += 1234; redraw(); CHECK(has("00:12.34") && has("Stop|"));
    send(EV_KEY, 0, 0, 'l'); now += 100; send(EV_KEY, 0, 0, 'l'); redraw();
    CHECK(has("1  00:12.34|") && has("2  00:01.00|"));
    send(EV_CLICK, 20, 100, 0); now += 500; redraw(); CHECK(has("00:13.34") && has("Start|"));   /* Stop button */
    send(EV_KEY, 0, 0, 'r'); redraw(); CHECK(has("00:00.00") && !has("1  00:"));
    send(EV_CLICK, 200, 100, 0); redraw(); CHECK(has("COUNTDOWN") && has("Set: 1 min"));          /* Timer button */
    send(EV_KEY, 0, 0, LK_UP); redraw(); CHECK(has("Set: 2 min") && has("02:00.00"));
    send(EV_KEY, 0, 0, ' '); now += 6000; redraw(); CHECK(has("01:00.00"));
    send(EV_KEY, 0, 0, LK_UP); redraw(); CHECK(has("Set: 2 min"));                                 /* ignored while running */
    now += 6000; send(EV_TICK, 0, 0, 0); redraw();
    CHECK(nbeeps == 2 && has("00:00.00") && has("Start|"));
    send(EV_KEY, 0, 0, ' '); now += 3000; redraw(); CHECK(has("01:30.00"));                        /* restarts from the full time */
    return hash_canvas();
}

static int run_one(int which, uint32_t *hash) {
    reset_state();
    uint32_t base = (uint32_t)(uintptr_t)arena[which];
    const char *err = lapp_load(filebuf, &hdr, arena[which], base);
    if (err) { out("  load error: "); out(err); out("\n"); return 1; }
    entry = (entry_fn)(uintptr_t)(arena[which] + hdr.entry_off);
    ox = 0; oy = 0; ow = hdr.win_w; oh = hdr.win_h;
    if (nameeq(hdr.name, "Hello")) *hash = scenario_hello();
    else if (nameeq(hdr.name, "Snake")) *hash = scenario_snake();
    else if (nameeq(hdr.name, "Doodle")) *hash = scenario_doodle();
    else if (nameeq(hdr.name, "Minesweeper")) *hash = scenario_minesweeper();
    else if (nameeq(hdr.name, "Tetris")) *hash = scenario_tetris();
    else if (nameeq(hdr.name, "2048")) *hash = scenario_2048();
    else if (nameeq(hdr.name, "Pong")) *hash = scenario_pong();
    else if (nameeq(hdr.name, "Stopwatch")) *hash = scenario_stopwatch();
    else { out("  no scenario for this app; only loaded + drew\n"); send(EV_INIT, 0, 0, 0); redraw(); *hash = hash_canvas(); }
    return 0;
}

void c_main(int argc, char **argv) {
    if (argc < 2) { out("usage: run32 app.lapp [out.ppm]\n"); sc(1, 2, 0, 0); }
    int n = rd_file(argv[1], filebuf, sizeof filebuf);
    CHECK(n > 0);
    const char *err = lapp_check(filebuf, n, &hdr);
    if (err) { out("  check failed: "); out(err); out("\n"); sc(1, 1, 0, 0); }
    out(argv[1]); out(" ("); out(hdr.name); out(") at two addresses\n");
    uint32_t h0 = 0, h1 = 0;
    CHECK((uint32_t)(uintptr_t)arena[0] != (uint32_t)(uintptr_t)arena[1]);
    if (run_one(0, &h0)) fails++;
    if (argc > 2) write_ppm(argv[2]);
    if (run_one(1, &h1)) fails++;
    CHECK(h0 == h1);                                              /* same pictures from both copies */
    /* a damaged file must be refused */
    filebuf[LAPP_HDR_SIZE + 5] ^= 0x40; struct lapp_header t;
    CHECK(lapp_check(filebuf, n, &t) != 0);
    filebuf[LAPP_HDR_SIZE + 5] ^= 0x40;
    out(fails ? "  RESULT: FAILED\n" : "  RESULT: ok\n");
    sc(1, fails ? 1 : 0, 0, 0);
}
__asm__(".globl _start\n_start:\n  xor %ebp,%ebp\n  mov (%esp),%eax\n  lea 4(%esp),%edx\n  and $-16,%esp\n  sub $8,%esp\n  push %edx\n  push %eax\n  call c_main\n  hlt\n");
void *memcpy(void *d, const void *s, unsigned n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned n) { char *a = d; while (n--) *a++ = c; return d; }
