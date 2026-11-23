/* doodle.c - draw with the mouse. Pick a colour, drag to paint, Clear to start over. */
#include "../leact_util.h"

#define CW 240
#define CH 130
static const struct leact_api *A;
static uint8_t px[CW * CH];
static int color = 1, lx = -1, ly = -1;
static const uint32_t pal[8] = { 0xFFFFFF, 0x000000, 0xE03030, 0xF0A020, 0xF0E030, 0x30B040, 0x3060E0, 0x9040C0 };

static void dot(int x, int y) {
    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
        int a = x + i, b = y + j;
        if (a >= 0 && b >= 0 && a < CW && b < CH) px[b * CW + a] = color;
    }
}
static void stroke(int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        dot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void clear(void) { for (int i = 0; i < CW * CH; i++) px[i] = 0; }
static void draw(void) {
    A->rect(0, 0, CW + 16, CH + 50, A->c_face);
    for (int i = 0; i < 8; i++) {
        A->rect(8 + i * 22, 6, 18, 18, pal[i]);
        A->frame(8 + i * 22, 6, 18, 18, i == color ? RGB(255, 0, 255) : A->c_dark);
    }
    A->button(8 + 8 * 22, 4, 52, 22, "Clear", 0);
    for (int y = 0; y < CH; y++) {
        int x = 0;
        while (x < CW) {
            int s = x, c = px[y * CW + x];
            while (x < CW && px[y * CW + x] == c) x++;
            A->rect(8 + s, 32 + y, x - s, 1, pal[c]);
        }
    }
    A->frame(7, 31, CW + 2, CH + 2, A->c_dark);
    A->text(8, CH + 38, "Drag in the white area to draw", A->c_text);
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: clear(); break;
    case EV_CLICK:
        if (ev->y >= 6 && ev->y < 24 && ev->x >= 8 && ev->x < 8 + 8 * 22) color = (ev->x - 8) / 22 % 8;
        else if (ev->y >= 4 && ev->y < 26 && ev->x >= 8 + 8 * 22) clear();
        else { lx = ev->x - 8; ly = ev->y - 32; stroke(lx, ly, lx, ly); }
        break;
    case EV_MOVE:
        if (lx >= 0) { int x = ev->x - 8, y = ev->y - 32; stroke(lx, ly, x, y); lx = x; ly = y; }
        break;
    case EV_RELEASE: lx = -1; break;
    case EV_DRAW: draw(); break;
    }
    return 0;
}
