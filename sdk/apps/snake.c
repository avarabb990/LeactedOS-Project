/* snake.c - Snake. Arrow keys or WASD, Space restarts. Keeps a high score in SNAKE.SCR. */
#include "../leact_util.h"

#define GW 20
#define GH 14
#define CELL 16
#define TOP 24

static const struct leact_api *A;
static uint8_t sx[GW * GH], sy[GW * GH];
static int len, dir, ndir, fx, fy, score, best, over, timer, delay;

static void save_best(void) { char b[12]; lu_fmt(b, sizeof b, "%d", best); A->file_write("SNAKE.SCR", b, lu_strlen(b)); }
static void load_best(void) {
    char b[12] = { 0 }; int n = A->file_read("SNAKE.SCR", b, 11), v = 0;
    for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + b[i] - '0';
    best = v;
}
static int on_snake(int x, int y) { for (int i = 0; i < len; i++) if (sx[i] == x && sy[i] == y) return 1; return 0; }
static void place_food(void) {
    do { fx = A->rand() % GW; fy = A->rand() % GH; } while (on_snake(fx, fy));
}
static void restart(void) {
    len = 3; dir = ndir = 1; score = 0; over = 0; timer = 0; delay = 9;
    for (int i = 0; i < len; i++) { sx[i] = 8 - i; sy[i] = 7; }
    place_food();
}
static void step(void) {
    dir = ndir;
    int nx = sx[0] + (dir == 1) - (dir == 3), ny = sy[0] + (dir == 2) - (dir == 0);
    if (nx < 0 || ny < 0 || nx >= GW || ny >= GH || on_snake(nx, ny)) {
        over = 1; A->beep(150, 200);
        if (score > best) { best = score; save_best(); }
        return;
    }
    int eat = nx == fx && ny == fy;
    if (eat) len++;
    for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
    sx[0] = nx; sy[0] = ny;
    if (eat) { score++; if (delay > 3 && score % 4 == 0) delay--; place_food(); A->beep(880, 30); }
}
static void draw(const struct leact_event *ev) {
    (void)ev;
    A->rect(0, 0, GW * CELL, TOP + GH * CELL, A->c_face);
    char b[40]; lu_fmt(b, sizeof b, "Score %d   Best %d", score, best);
    A->text(6, 8, b, A->c_text);
    A->rect(0, TOP, GW * CELL, GH * CELL, RGB(24, 30, 24));
    for (int y = 0; y < GH; y++) for (int x = (y & 1); x < GW; x += 2) A->rect(x * CELL, TOP + y * CELL, CELL, CELL, RGB(30, 38, 30));
    A->circle(fx * CELL + 8, TOP + fy * CELL + 8, 6, RGB(230, 50, 50));
    for (int i = len - 1; i >= 0; i--)
        A->rect(sx[i] * CELL + 1, TOP + sy[i] * CELL + 1, CELL - 2, CELL - 2, i ? RGB(70, 200, 90) : RGB(180, 255, 120));
    if (over) {
        A->rect(60, TOP + 80, 200, 50, RGB(0, 0, 0)); A->frame(60, TOP + 80, 200, 50, RGB(255, 255, 255));
        A->text(100, TOP + 92, "GAME OVER", RGB(255, 80, 80));
        A->text(76, TOP + 108, "Press Space to retry", RGB(255, 255, 255));
    }
}
static void key(int k) {
    if ((k == LK_UP || k == 'w') && dir != 2) ndir = 0;
    else if ((k == LK_RIGHT || k == 'd') && dir != 3) ndir = 1;
    else if ((k == LK_DOWN || k == 's') && dir != 0) ndir = 2;
    else if ((k == LK_LEFT || k == 'a') && dir != 1) ndir = 3;
    else if (k == ' ' && over) restart();
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: load_best(); restart(); break;
    case EV_TICK: if (!over && ++timer >= delay) { timer = 0; step(); } break;
    case EV_KEY: key(ev->key); break;
    case EV_DRAW: draw(ev); break;
    }
    return 0;
}
