/* pong.c - Pong against the computer.  Move the left paddle with Up/Down (or W/S) or by dragging with the mouse (hold the button).
 * First to 7 wins. Space serves / restarts. Fixed-point (x256) physics, no floats. */
#include "../leact_util.h"

#define W 400
#define H 280
#define PH 48
#define PW 8
#define BS 8
#define WIN 7

static const struct leact_api *A;
static int py, cy;                 /* paddle tops (pixels) */
static int bx, by, vx, vy;         /* ball position x256, velocity x256 per tick */
static int ps, cs, serving, over, hold;

static void serve(int dir) {
    bx = (W / 2) * 256; by = (H / 2) * 256;
    vx = dir * 3 * 256; vy = ((int)(A->rand() % 5) - 2) * 128;
    if (!vy) vy = 128;
    serving = 0;
}
static void reset(void) { py = cy = (H - PH) / 2; ps = cs = 0; over = 0; serving = 1; hold = 0; bx = (W / 2) * 256; by = (H / 2) * 256; vx = vy = 0; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void bounce(int paddle_top, int dir) {
    int off = (by / 256 + BS / 2) - (paddle_top + PH / 2);        /* -PH/2 .. PH/2 */
    vy = off * 40;
    int sp = vx < 0 ? -vx : vx;
    sp += 20; if (sp > 7 * 256) sp = 7 * 256;
    vx = dir * sp;
    A->beep(dir > 0 ? 440 : 330, 15);
}
static void step(void) {
    if (over) return;
    if (hold) { hold--; return; }
    if (serving) return;
    bx += vx; by += vy;
    if (by < 0) { by = 0; vy = -vy; }
    if (by > (H - BS) * 256) { by = (H - BS) * 256; vy = -vy; }
    int x = bx / 256, y = by / 256;
    if (vx < 0 && x <= 14 + PW && x >= 14 - 4 && y + BS >= py && y <= py + PH) { bx = (14 + PW) * 256; bounce(py, 1); }
    else if (vx > 0 && x + BS >= W - 14 - PW && x + BS <= W - 14 + 4 && y + BS >= cy && y <= cy + PH) { bx = (W - 14 - PW - BS) * 256; bounce(cy, -1); }
    /* computer paddle: follows the ball with a capped speed (beatable) */
    int target = y + BS / 2 - PH / 2, d = target - cy;
    int sp = 2 + (vx > 0 ? 1 : 0);
    if (vx > 0 || (x > W / 2)) cy += clampi(d, -sp, sp);
    cy = clampi(cy, 0, H - PH);
    if (x < -BS) { cs++; serving = 1; hold = 30; A->beep(200, 120); }
    else if (x > W) { ps++; serving = 1; hold = 30; A->beep(660, 120); }
    if (ps >= WIN || cs >= WIN) { over = 1; A->beep(ps >= WIN ? 880 : 140, 200); }
}
static void draw(void) {
    A->rect(0, 0, W, H, RGB(10, 10, 16));
    for (int y = 4; y < H; y += 16) A->rect(W / 2 - 1, y, 2, 8, RGB(90, 90, 100));
    A->rect(14, py, PW, PH, RGB(240, 240, 240));
    A->rect(W - 14 - PW, cy, PW, PH, RGB(240, 240, 240));
    if (!serving || hold || over) A->rect(bx / 256, by / 256, BS, BS, RGB(255, 220, 60));
    char b[8];
    lu_fmt(b, sizeof b, "%d", ps); A->text_scaled(W / 2 - 60, 10, b, RGB(200, 200, 210), 3);
    lu_fmt(b, sizeof b, "%d", cs); A->text_scaled(W / 2 + 36, 10, b, RGB(200, 200, 210), 3);
    if (over) { A->text_scaled(W / 2 - 84, H / 2 - 12, ps >= WIN ? "YOU WIN" : "YOU LOSE", RGB(255, 255, 255), 3); A->text(W / 2 - 52, H / 2 + 24, "Space = again", RGB(200, 200, 200)); }
    else if (serving && !hold) A->text(W / 2 - 44, H / 2 + 20, "Space = serve", RGB(200, 200, 200));
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: reset(); break;
    case EV_TICK: step(); break;
    case EV_KEY:
        if (ev->key == LK_UP || ev->key == 'w') py = clampi(py - 14, 0, H - PH);
        else if (ev->key == LK_DOWN || ev->key == 's') py = clampi(py + 14, 0, H - PH);
        else if (ev->key == ' ') { if (over) reset(); else if (serving && !hold) serve((A->rand() & 1) ? 1 : -1); }
        break;
    case EV_MOVE: py = clampi(ev->y - PH / 2, 0, H - PH); break;
    case EV_CLICK: if (serving && !hold && !over) serve(1); break;
    case EV_DRAW: draw(); break;
    }
    return 0;
}
