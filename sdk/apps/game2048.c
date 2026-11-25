/* game2048.c - 2048.  Arrow keys (or WASD) slide the tiles; equal tiles merge.
 * R starts over. Best score is kept in G2048.SCR. */
#include "../leact_util.h"

#define T 70
#define GAP 6
#define OX 10
#define OY 56

static const struct leact_api *A;
static int g[4][4], score, best, over, won, keep, moved_flag;

static void load_best(void) {
    char b[12] = { 0 }; int n = A->file_read("G2048.SCR", b, 11), v = 0;
    for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + b[i] - '0';
    best = v;
}
static void save_best(void) { char b[12]; lu_fmt(b, sizeof b, "%d", best); A->file_write("G2048.SCR", b, lu_strlen(b)); }

static void add_tile(void) {
    int fx[16], fy[16], n = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (!g[y][x]) { fx[n] = x; fy[n++] = y; }
    if (!n) return;
    int k = A->rand() % n;
    g[fy[k]][fx[k]] = (A->rand() % 10) ? 2 : 4;
}
static int can_move(void) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        if (!g[y][x]) return 1;
        if (x < 3 && g[y][x] == g[y][x + 1]) return 1;
        if (y < 3 && g[y][x] == g[y + 1][x]) return 1;
    }
    return 0;
}
static void reset(void) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) g[y][x] = 0;
    score = 0; over = 0; won = 0; keep = 0;
    add_tile(); add_tile();
}
/* slide one line (4 values, ordered towards the direction of travel) */
static void slide_line(int *v) {
    int out[4] = { 0, 0, 0, 0 }, n = 0, last = 0;
    for (int i = 0; i < 4; i++) {
        if (!v[i]) continue;
        if (last && out[n - 1] == v[i]) { out[n - 1] *= 2; score += out[n - 1]; last = 0; if (out[n - 1] == 2048) won = 1; }
        else { out[n++] = v[i]; last = 1; }
    }
    for (int i = 0; i < 4; i++) { if (v[i] != out[i]) moved_flag = 1; v[i] = out[i]; }
}
static void move(int dir) {   /* 0 left, 1 right, 2 up, 3 down */
    moved_flag = 0;
    for (int i = 0; i < 4; i++) {
        int v[4];
        for (int j = 0; j < 4; j++) {
            switch (dir) {
            case 0: v[j] = g[i][j]; break;
            case 1: v[j] = g[i][3 - j]; break;
            case 2: v[j] = g[j][i]; break;
            default: v[j] = g[3 - j][i]; break;
            }
        }
        slide_line(v);
        for (int j = 0; j < 4; j++) {
            switch (dir) {
            case 0: g[i][j] = v[j]; break;
            case 1: g[i][3 - j] = v[j]; break;
            case 2: g[j][i] = v[j]; break;
            default: g[3 - j][i] = v[j]; break;
            }
        }
    }
    if (!moved_flag) return;
    add_tile(); A->beep(500, 15);
    if (score > best) { best = score; save_best(); }
    if (!can_move()) { over = 1; A->beep(140, 200); }
}
static uint32_t tile_col(int v) {
    switch (v) {
    case 0: return RGB(205, 193, 180);   case 2: return RGB(238, 228, 218);   case 4: return RGB(237, 224, 200);
    case 8: return RGB(242, 177, 121);   case 16: return RGB(245, 149, 99);   case 32: return RGB(246, 124, 95);
    case 64: return RGB(246, 94, 59);    case 128: return RGB(237, 207, 114); case 256: return RGB(237, 204, 97);
    case 512: return RGB(237, 200, 80);  case 1024: return RGB(237, 197, 63); default: return RGB(237, 194, 46);
    }
}
static void draw(void) {
    A->rect(0, 0, 320, 380, A->c_face);
    char b[24];
    A->text_scaled(10, 8, "2048", RGB(119, 110, 101), 3);
    A->rect(150, 6, 76, 40, RGB(187, 173, 160)); A->text(158, 12, "SCORE", RGB(238, 228, 218));
    lu_fmt(b, sizeof b, "%u", score); A->text(158, 28, b, RGB(255, 255, 255));
    A->rect(232, 6, 76, 40, RGB(187, 173, 160)); A->text(240, 12, "BEST", RGB(238, 228, 218));
    lu_fmt(b, sizeof b, "%u", best); A->text(240, 28, b, RGB(255, 255, 255));
    A->rect(OX - GAP, OY - GAP, 4 * T + 5 * GAP, 4 * T + 5 * GAP, RGB(187, 173, 160));
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        int px = OX + x * (T + GAP), py = OY + y * (T + GAP);
        A->rect(px, py, T, T, tile_col(g[y][x]));
        if (g[y][x]) {
            lu_fmt(b, sizeof b, "%d", g[y][x]);
            int len = lu_strlen(b), sc = len <= 2 ? 3 : 2;
            A->text_scaled(px + (T - len * 8 * sc) / 2, py + (T - 8 * sc) / 2, b, g[y][x] <= 4 ? RGB(119, 110, 101) : RGB(255, 255, 255), sc);
        }
    }
    A->text(10, 366, "Arrows/WASD slide   R = restart", A->c_dark);
    if (over || (won && !keep)) {
        A->rect(OX + 30, OY + 110, 4 * T + 3 * GAP - 60, 56, RGB(0, 0, 0)); A->frame(OX + 30, OY + 110, 4 * T + 3 * GAP - 60, 56, RGB(255, 255, 255));
        A->text(OX + 80, OY + 122, over ? "GAME OVER" : "YOU MADE 2048!", RGB(255, 255, 255));
        A->text(OX + 60, OY + 142, over ? "R = try again" : "any move keeps going", RGB(200, 200, 200));
    }
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: load_best(); reset(); break;
    case EV_KEY: {
        int k = ev->key;
        if (k == 'r' || k == 'R') { reset(); break; }
        if (over) break;
        if (won) keep = 1;
        if (k == LK_LEFT || k == 'a') move(0);
        else if (k == LK_RIGHT || k == 'd') move(1);
        else if (k == LK_UP || k == 'w') move(2);
        else if (k == LK_DOWN || k == 's') move(3);
        break;
    }
    case EV_DRAW: draw(); break;
    }
    return 0;
}
