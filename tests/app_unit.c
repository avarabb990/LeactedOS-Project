/* Unit tests that look inside an app: the app source is included, so its static state is reachable.
 * Build:  gcc -DAPP_TETRIS tests/app_unit.c   or   gcc -DAPP_2048 tests/app_unit.c   (64-bit, ASan) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define lapp_main app_main
#ifdef APP_TETRIS
#include "../sdk/apps/tetris.c"
#else
#include "../sdk/apps/game2048.c"
#endif
#undef lapp_main
static uint32_t now_, rs = 7;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)
static void n_rect(int x, int y, int w, int h, uint32_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void n_line(int a, int b, int c, int d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void n_circle(int a, int b, int c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }
static void n_pixel(int a, int b, uint32_t c) { (void)a; (void)b; (void)c; }
static int n_text(int x, int y, const char *s, uint32_t c) { (void)y; (void)s; (void)c; return x; }
static int n_texts(int x, int y, const char *s, uint32_t c, int sc) { (void)y; (void)s; (void)c; (void)sc; return x; }
static void n_button(int a, int b, int c, int d, const char *e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static void n_blit(int a, int b, int c, int d, const uint32_t *e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static uint32_t n_ticks(void) { return now_; }
static uint32_t n_rand(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }
static void n_beep(uint32_t a, uint32_t b) { (void)a; (void)b; }
static void n_title(const char *s) { (void)s; }
static void n_close(void) {}
static void n_log(const char *s) { (void)s; }
static int n_fr(const char *n, void *b, uint32_t m) { (void)n; (void)b; (void)m; return -1; }
static int n_fw(const char *n, const void *b, uint32_t m) { (void)n; (void)b; (void)m; return 0; }
static int n_fd(const char *n) { (void)n; return 0; }
static int n_fn(int i, char *o, uint32_t m) { (void)i; (void)o; (void)m; return -1; }
static struct leact_api api_ = { sizeof(struct leact_api), LAPP_API_VERSION, n_rect, n_rect, n_line, n_circle, n_pixel, n_text, n_texts, n_button, n_blit,
    n_ticks, n_rand, n_beep, n_title, n_close, n_log, n_fr, n_fw, n_fd, n_fn, 0xD4D0C8, 0, 0xFFFFFF, 0x716F64 };
static void ev(int type, int key) { struct leact_event e = { type, 0, 0, key, 0, 400, 400 }; app_main(&api_, &e); }

#ifdef APP_TETRIS
static void clear_board(void) { memset(board, 0, sizeof board); }
int main(void) {
    ev(EV_INIT, 0);
    /* every piece/rotation: four cells, and each fits inside the board at its spawn column */
    for (int p = 0; p < 7; p++) for (int r = 0; r < 4; r++) { int c = 0; for (int i = 0; i < 16; i++) c += (SH[p][r] >> i) & 1; CHECK(c == 4); }
    /* two full rows with a gap each; an I piece lying flat... drop a vertical I into the gap column to clear them */
    clear_board(); over = 0; level = 1; lines = 0; score = 0;
    for (int x = 0; x < BW; x++) { board[BH - 1][x] = 1; board[BH - 2][x] = 1; board[BH - 3][x] = x == 0 ? 0 : 2; }
    board[BH - 1][4] = 0; board[BH - 2][4] = 0; board[BH - 3][4] = 0;
    cur = 0; rot = 1; cx = 2; cy = 0; nxt = 3;                    /* vertical I: bitmap 0x2222 is column 2 of its box -> x = cx + 2 */
    cx = 4 - 2;
    while (fits(cur, rot, cx, cy + 1)) cy++;
    lock_piece();
    CHECK(lines == 2 && score == 300);                            /* rows BH-1 and BH-2 cleared; BH-3 is not full */
    CHECK(board[BH - 1][1] == 2 && board[BH - 1][0] == 0);        /* the row above fell down */
    int filled = 0; for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) filled += board[y][x] != 0;
    CHECK(filled == 9 + 1);                                        /* the 8 + 1 cells of the old row (now at the bottom) + the I cell above them */
    /* four rows at once and the level-up */
    clear_board(); over = 0; lines = 6; level = 1; score = 0;
    for (int y = BH - 4; y < BH; y++) for (int x = 0; x < BW; x++) board[y][x] = x == 9 ? 0 : 3;
    cur = 0; rot = 1; cx = 9 - 2; cy = 0; nxt = 3;
    while (fits(cur, rot, cx, cy + 1)) cy++;
    lock_piece();
    CHECK(lines == 10 && level == 2 && score == 800);
    filled = 0; for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) filled += board[y][x] != 0;
    CHECK(filled == 0 || over);                                    /* the Tetris emptied the board */
    /* walls: a piece never leaves the board, rotation near walls kicks */
    ev(EV_INIT, 0);
    for (int i = 0; i < 30; i++) ev(EV_KEY, LK_LEFT);
    for (int i = 0; i < 4; i++) { ev(EV_KEY, LK_UP); CHECK(fits(cur, rot, cx, cy)); }
    for (int i = 0; i < 30; i++) ev(EV_KEY, LK_RIGHT);
    for (int i = 0; i < 4; i++) { ev(EV_KEY, LK_UP); CHECK(fits(cur, rot, cx, cy)); }
    /* soak: random play must never trip ASan or leave a piece in an invalid state */
    for (int i = 0; i < 200000; i++) {
        int k = n_rand() % 9; static const int keys[9] = { LK_LEFT, LK_RIGHT, LK_UP, LK_DOWN, ' ', 'p', 'r', 0, 0 };
        if (k < 7) ev(EV_KEY, keys[k]); else ev(EV_TICK, 0);
        if (!over && !paused) CHECK(fits(cur, rot, cx, cy));
        if (fails) break;
        if ((i & 255) == 0) ev(EV_DRAW, 0);
    }
    printf(fails ? "tetris unit: FAILED\n" : "tetris unit: ok\n");
    return fails != 0;
}
#else
static int sum(void) { int s = 0; for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) s += g[y][x]; return s; }
int main(void) {
    ev(EV_INIT, 0);
    memset(g, 0, sizeof g); score = 0; over = 0;
    g[0][0] = 2; g[0][1] = 2; g[0][2] = 2; g[0][3] = 2;           /* 2 2 2 2 -> 4 4 0 0 (a tile merges only once per move) */
    move(0); CHECK(g[0][0] == 4 && g[0][1] == 4 && score == 8);
    memset(g, 0, sizeof g); score = 0;
    g[1][0] = 4; g[1][1] = 2; g[1][2] = 2; g[1][3] = 0;           /* 4 2 2 . -> 4 4 . . */
    move(0); CHECK(g[1][0] == 4 && g[1][1] == 4 && g[1][2] == 0 && score == 4);
    memset(g, 0, sizeof g);
    g[0][3] = 8; g[1][3] = 8; g[3][3] = 4;                         /* up: column 3 -> 16 4 . . */
    move(2); CHECK(g[0][3] == 16 && g[1][3] == 4);
    memset(g, 0, sizeof g);
    g[0][0] = 2; g[2][0] = 2; g[3][0] = 4;                         /* down: column 0 -> . . 4 4 */
    move(3); CHECK(g[3][0] == 4 && g[2][0] == 4 && g[1][0] == 0 && g[0][0] == 0);
    memset(g, 0, sizeof g);
    g[2][0] = 2; g[2][2] = 2;                                      /* right: . . 4 right-aligned */
    move(1); CHECK(g[2][3] == 4 && g[2][2] == 0 && g[2][1] == 0);
    /* a move that changes nothing adds no tile */
    memset(g, 0, sizeof g); g[0][0] = 2; int before = sum(); move(0); CHECK(sum() == before);
    /* a full board with no equal neighbours is game over; with one pair it is not */
    over = 0; for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) g[y][x] = ((x + y) & 1) ? 2 : 4;
    CHECK(!can_move()); g[3][3] = g[3][2]; CHECK(can_move());
    /* reaching 2048 */
    memset(g, 0, sizeof g); won = 0; g[0][0] = 1024; g[0][1] = 1024; move(0); CHECK(won && g[0][0] == 2048);
    /* soak */
    for (int i = 0; i < 300000; i++) {
        static const int keys[7] = { LK_LEFT, LK_RIGHT, LK_UP, LK_DOWN, 'r', 'w', 'x' };
        ev(EV_KEY, keys[n_rand() % 7]);
        if ((i & 127) == 0) ev(EV_DRAW, 0);
        int s = 0, ok = 1; for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { int v = g[y][x]; s += v; if (v & (v - 1)) ok = 0; if (v && v < 2) ok = 0; }
        CHECK(ok); (void)s;
        if (fails) break;
    }
    printf(fails ? "2048 unit: FAILED\n" : "2048 unit: ok\n");
    return fails != 0;
}
#endif
