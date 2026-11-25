/* tetris.c - Tetris.  Left/Right move, Up rotates, Down drops one row, Space drops all the way,
 * P pauses, R restarts. Seven-piece bag, ghost piece, levels every 10 lines. Best score in TETRIS.SCR. */
#include "../leact_util.h"

#define BW 10
#define BH 20
#define CS 16
#define PX 8
#define PY 8

/* each piece is four 4x4 bitmaps (one per rotation); bit 15 is the top-left square */
static const uint16_t SH[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },   /* I */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },   /* L */
    { 0x6600, 0x6600, 0x6600, 0x6600 },   /* O */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },   /* S */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   /* T */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },   /* Z */
};
static const uint32_t COL[8] = { 0, RGB(0, 220, 230), RGB(50, 90, 235), RGB(245, 150, 30), RGB(240, 220, 40), RGB(60, 210, 70), RGB(175, 65, 225), RGB(230, 55, 55) };

static const struct leact_api *A;
static uint8_t board[BH][BW];
static int cur, rot, cx, cy, nxt, score, lines, level, best, over, paused, timer;
static int bag[7], bagn;

static void load_best(void) {
    char b[12] = { 0 }; int n = A->file_read("TETRIS.SCR", b, 11), v = 0;
    for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + b[i] - '0';
    best = v;
}
static void save_best(void) { char b[12]; lu_fmt(b, sizeof b, "%d", best); A->file_write("TETRIS.SCR", b, lu_strlen(b)); }

static int next_piece(void) {
    if (!bagn) {
        for (int i = 0; i < 7; i++) bag[i] = i;
        for (int i = 6; i > 0; i--) { int j = A->rand() % (i + 1), t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
        bagn = 7;
    }
    return bag[--bagn];
}
static int fits(int p, int r, int x, int y) {
    uint16_t m = SH[p][r & 3];
    for (int i = 0; i < 16; i++) {
        if (!(m & (0x8000 >> i))) continue;
        int bx = x + (i & 3), by = y + (i >> 2);
        if (bx < 0 || bx >= BW || by >= BH) return 0;
        if (by >= 0 && board[by][bx]) return 0;
    }
    return 1;
}
static void end_game(void) {
    over = 1; A->beep(140, 220);
    if (score > best) { best = score; save_best(); }
}
static void spawn(void) {
    cur = nxt; nxt = next_piece(); rot = 0; cx = 3; cy = 0; timer = 0;
    if (!fits(cur, rot, cx, cy)) end_game();
}
static void restart(void) {
    for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) board[y][x] = 0;
    score = 0; lines = 0; level = 1; over = 0; paused = 0; bagn = 0;
    nxt = next_piece(); spawn();
}
static int delay(void) { int d = 30 - (level - 1) * 3; return d < 3 ? 3 : d; }
static void lock_piece(void) {
    uint16_t m = SH[cur][rot & 3];
    for (int i = 0; i < 16; i++)
        if (m & (0x8000 >> i)) { int bx = cx + (i & 3), by = cy + (i >> 2); if (by >= 0 && by < BH && bx >= 0 && bx < BW) board[by][bx] = (uint8_t)(cur + 1); }
    int cleared = 0;
    for (int y = BH - 1; y >= 0;) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!board[y][x]) { full = 0; break; }
        if (!full) { y--; continue; }
        for (int yy = y; yy > 0; yy--) for (int x = 0; x < BW; x++) board[yy][x] = board[yy - 1][x];
        for (int x = 0; x < BW; x++) board[0][x] = 0;
        cleared++;                                   /* the same row index now holds the row above: test it again */
    }
    if (cleared) {
        static const int pts[5] = { 0, 100, 300, 500, 800 };
        score += pts[cleared] * level; lines += cleared; level = 1 + lines / 10;
        A->beep(cleared >= 4 ? 1320 : 880, 40);
    }
    spawn();
}
static void drop_one(void) { if (fits(cur, rot, cx, cy + 1)) cy++; else lock_piece(); }
static void rotate(void) {
    static const int kick[5] = { 0, -1, 1, -2, 2 };
    for (int i = 0; i < 5; i++) if (fits(cur, rot + 1, cx + kick[i], cy)) { rot = (rot + 1) & 3; cx += kick[i]; return; }
}
static void block(int x, int y, uint32_t c) {
    A->rect(x, y, CS, CS, c);
    A->rect(x, y, CS, 2, RGB(255, 255, 255)); A->rect(x, y, 2, CS, RGB(255, 255, 255));
    A->rect(x, y + CS - 2, CS, 2, RGB(0, 0, 0)); A->rect(x + CS - 2, y, 2, CS, RGB(0, 0, 0));
    A->rect(x + 2, y + 2, CS - 4, CS - 4, c);
}
static void draw(void) {
    A->rect(0, 0, 290, 336, A->c_face);
    A->rect(PX, PY, BW * CS, BH * CS, RGB(18, 18, 26));
    for (int x = 1; x < BW; x++) A->rect(PX + x * CS, PY, 1, BH * CS, RGB(34, 34, 46));
    for (int y = 1; y < BH; y++) A->rect(PX, PY + y * CS, BW * CS, 1, RGB(34, 34, 46));
    for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++) if (board[y][x]) block(PX + x * CS, PY + y * CS, COL[board[y][x]]);
    if (!over) {
        int gy = cy; while (fits(cur, rot, cx, gy + 1)) gy++;
        uint16_t m = SH[cur][rot & 3];
        for (int i = 0; i < 16; i++) if (m & (0x8000 >> i)) {
            int bx = cx + (i & 3), by = gy + (i >> 2), cyy = cy + (i >> 2);
            if (by >= 0) A->frame(PX + bx * CS, PY + by * CS, CS, CS, COL[cur + 1]);
            if (cyy >= 0) block(PX + bx * CS, PY + cyy * CS, COL[cur + 1]);
        }
    }
    int sx = PX + BW * CS + 14;
    char b[24];
    A->text(sx, 12, "SCORE", A->c_text);  lu_fmt(b, sizeof b, "%u", score); A->text(sx, 24, b, A->c_text);
    A->text(sx, 44, "LEVEL", A->c_text);  lu_fmt(b, sizeof b, "%d", level);  A->text(sx, 56, b, A->c_text);
    A->text(sx, 76, "LINES", A->c_text);  lu_fmt(b, sizeof b, "%d", lines);  A->text(sx, 88, b, A->c_text);
    A->text(sx, 108, "BEST", A->c_text);  lu_fmt(b, sizeof b, "%d", best);   A->text(sx, 120, b, A->c_text);
    A->text(sx, 144, "NEXT", A->c_text);
    A->rect(sx, 156, 72, 56, RGB(18, 18, 26));
    uint16_t m = SH[nxt][0];
    for (int i = 0; i < 16; i++) if (m & (0x8000 >> i)) block(sx + 4 + (i & 3) * 16, 160 + (i >> 2) * 12 - 6, COL[nxt + 1]);
    A->text(sx, 232, "Arrows move", A->c_dark); A->text(sx, 244, "Up = turn", A->c_dark);
    A->text(sx, 256, "Space = drop", A->c_dark); A->text(sx, 268, "P pause", A->c_dark); A->text(sx, 280, "R restart", A->c_dark);
    if (over || paused) {
        A->rect(PX + 10, PY + 120, 140, 52, RGB(0, 0, 0)); A->frame(PX + 10, PY + 120, 140, 52, RGB(255, 255, 255));
        A->text(PX + (over ? 42 : 52), PY + 132, over ? "GAME OVER" : "PAUSED", over ? RGB(255, 80, 80) : RGB(255, 255, 255));
        A->text(PX + 22, PY + 150, over ? "R = play again" : "P = continue", RGB(255, 255, 255));
    }
}
static void key(int k) {
    if (k == 'r' || k == 'R') { restart(); return; }
    if (over) return;
    if (k == 'p' || k == 'P') { paused = !paused; return; }
    if (paused) return;
    if ((k == LK_LEFT || k == 'a') && fits(cur, rot, cx - 1, cy)) cx--;
    else if ((k == LK_RIGHT || k == 'd') && fits(cur, rot, cx + 1, cy)) cx++;
    else if (k == LK_UP || k == 'w') rotate();
    else if (k == LK_DOWN || k == 's') { if (fits(cur, rot, cx, cy + 1)) { cy++; score++; timer = 0; } else lock_piece(); }
    else if (k == ' ') { int d = 0; while (fits(cur, rot, cx, cy + 1)) { cy++; d++; } score += 2 * d; lock_piece(); }
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: load_best(); restart(); break;
    case EV_TICK: if (!over && !paused && ++timer >= delay()) { timer = 0; drop_one(); } break;
    case EV_KEY: key(ev->key); break;
    case EV_DRAW: draw(); break;
    }
    return 0;
}
