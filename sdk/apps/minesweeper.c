/* minesweeper.c - Minesweeper, 9x9 with 10 mines.
 * Click a square to open it. The Flag button (or key F) switches to flag mode, where clicks place flags.
 * Click an opened number whose flags match it to open the squares around it. N or Space starts a new game.
 * Your best time is kept in MINES.BST. */
#include "../leact_util.h"

#define N 9
#define CELL 24
#define TOP 34
#define MINES 10

enum { PLAY, WON, LOST };

static const struct leact_api *A;
static uint8_t mine[N][N], shown[N][N], flg[N][N], cnt[N][N];
static int state, flagmode, nflags, nopen, placed, t0, secs, best, ex, ey;

static void load_best(void) {
    char b[12] = { 0 }; int n = A->file_read("MINES.BST", b, 11), v = 0;
    for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + b[i] - '0';
    best = v;
}
static void save_best(void) { char b[12]; lu_fmt(b, sizeof b, "%d", best); A->file_write("MINES.BST", b, lu_strlen(b)); }

static void reset(void) {
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) mine[y][x] = shown[y][x] = flg[y][x] = cnt[y][x] = 0;
    state = PLAY; nflags = 0; nopen = 0; placed = 0; secs = 0; ex = ey = -1;
}
static void count_all(void) {
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int c = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < N && ny < N) c += mine[ny][nx];
        }
        cnt[y][x] = (uint8_t)c;
    }
}
/* mines are placed on the first click so that it can never be a mine (nor touch one) */
static void place_mines(int sx, int sy) {
    int n = 0;
    while (n < MINES) {
        int x = A->rand() % N, y = A->rand() % N;
        if (mine[y][x] || (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1)) continue;
        mine[y][x] = 1; n++;
    }
    count_all(); placed = 1; t0 = (int)A->ticks();
}
static void flood(int x0, int y0) {
    int qx[N * N], qy[N * N], h = 0, t = 0;
    uint8_t queued[N][N];
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) queued[y][x] = 0;
    qx[t] = x0; qy[t++] = y0; queued[y0][x0] = 1;
    while (h < t) {
        int x = qx[h], y = qy[h++];
        if (shown[y][x] || flg[y][x]) continue;
        shown[y][x] = 1; nopen++;
        if (cnt[y][x]) continue;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || queued[ny][nx] || shown[ny][nx]) continue;
            queued[ny][nx] = 1; qx[t] = nx; qy[t++] = ny;
        }
    }
}
static void lose(int x, int y) { state = LOST; ex = x; ey = y; A->beep(120, 250); }
static void win(void) {
    state = WON; nflags = MINES;
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (mine[y][x]) flg[y][x] = 1;
    secs = ((int)A->ticks() - t0) / 100; if (secs < 1) secs = 1;
    if (!best || secs < best) { best = secs; save_best(); }
    A->beep(880, 60); A->beep(1175, 90);
}
static void open_cell(int x, int y) {
    if (flg[y][x] || shown[y][x]) return;
    if (mine[y][x]) { lose(x, y); return; }
    flood(x, y);
}
static void click_cell(int x, int y) {
    if (state != PLAY) return;
    if (flagmode) {
        if (!shown[y][x]) { flg[y][x] ^= 1; nflags += flg[y][x] ? 1 : -1; }
        return;
    }
    if (shown[y][x]) {                                   /* chord: open around a satisfied number */
        if (!cnt[y][x]) return;
        int f = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < N && ny < N) f += flg[ny][nx];
        }
        if (f != cnt[y][x]) return;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if ((dx || dy) && nx >= 0 && ny >= 0 && nx < N && ny < N && state == PLAY) open_cell(nx, ny);
        }
    } else {
        if (flg[y][x]) return;
        if (!placed) place_mines(x, y);
        open_cell(x, y);
    }
    if (state == PLAY && nopen == N * N - MINES) win();
}
static void draw_cell(int x, int y) {
    int px = x * CELL, py = TOP + y * CELL;
    static const uint32_t numcol[9] = { 0, RGB(0, 0, 220), RGB(0, 130, 0), RGB(220, 0, 0), RGB(0, 0, 130), RGB(130, 0, 0), RGB(0, 130, 130), RGB(0, 0, 0), RGB(100, 100, 100) };
    int open = shown[y][x] || (state == LOST && mine[y][x] && !flg[y][x]);
    if (!open) {
        A->rect(px, py, CELL, CELL, RGB(192, 192, 192));
        A->rect(px, py, CELL, 2, RGB(255, 255, 255)); A->rect(px, py, 2, CELL, RGB(255, 255, 255));
        A->rect(px, py + CELL - 2, CELL, 2, RGB(110, 110, 110)); A->rect(px + CELL - 2, py, 2, CELL, RGB(110, 110, 110));
        if (flg[y][x]) {
            A->rect(px + 11, py + 5, 2, 14, RGB(0, 0, 0)); A->rect(px + 7, py + 17, 10, 2, RGB(0, 0, 0));
            A->rect(px + 5, py + 5, 7, 6, RGB(230, 30, 30));
            if (state == LOST && !mine[y][x]) { A->line(px + 4, py + 4, px + CELL - 5, py + CELL - 5, RGB(0, 0, 0)); A->line(px + CELL - 5, py + 4, px + 4, py + CELL - 5, RGB(0, 0, 0)); }
        }
        return;
    }
    A->rect(px, py, CELL, CELL, (x == ex && y == ey) ? RGB(255, 80, 80) : RGB(222, 222, 222));
    A->frame(px, py, CELL, CELL, RGB(150, 150, 150));
    if (mine[y][x]) {
        A->circle(px + 12, py + 12, 6, RGB(0, 0, 0));
        A->line(px + 4, py + 12, px + 20, py + 12, RGB(0, 0, 0)); A->line(px + 12, py + 4, px + 12, py + 20, RGB(0, 0, 0));
        A->rect(px + 9, py + 9, 2, 2, RGB(255, 255, 255));
    } else if (cnt[y][x]) {
        char s[2] = { (char)('0' + cnt[y][x]), 0 };
        A->text_scaled(px + 4, py + 4, s, numcol[cnt[y][x]], 2);
    }
}
static void draw(void) {
    A->rect(0, 0, N * CELL, TOP + N * CELL, A->c_face);
    char b[40];
    if (state == WON) lu_fmt(b, sizeof b, "YOU WIN");
    else if (state == LOST) lu_fmt(b, sizeof b, "BOOM");
    else lu_fmt(b, sizeof b, "Mines %02d", MINES - nflags);
    A->text(6, 18, b, state == LOST ? RGB(200, 0, 0) : state == WON ? RGB(0, 140, 0) : A->c_text);
    A->button(78, 5, 40, 24, "New", 0);
    A->button(124, 5, 40, 24, "Flag", flagmode);
    lu_fmt(b, sizeof b, "%03d", secs > 999 ? 999 : secs);
    A->text(176, 13, b, A->c_text);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) draw_cell(x, y);
    if (best) { lu_fmt(b, sizeof b, "Best %ds", best); A->text(6, 6, b, A->c_dark); }
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: load_best(); reset(); break;
    case EV_TICK: if (state == PLAY && placed) secs = ((int)A->ticks() - t0) / 100; break;
    case EV_KEY:
        if (ev->key == 'f' || ev->key == 'F') flagmode = !flagmode;
        else if (ev->key == 'n' || ev->key == 'N' || ev->key == ' ') reset();
        break;
    case EV_CLICK:
        if (ev->y < TOP) {
            if (ev->x >= 78 && ev->x < 118 && ev->y >= 5 && ev->y < 29) reset();
            else if (ev->x >= 124 && ev->x < 164 && ev->y >= 5 && ev->y < 29) flagmode = !flagmode;
        } else {
            int x = ev->x / CELL, y = (ev->y - TOP) / CELL;
            if (x >= 0 && x < N && y >= 0 && y < N) click_cell(x, y);
        }
        break;
    case EV_DRAW: draw(); break;
    }
    return 0;
}
