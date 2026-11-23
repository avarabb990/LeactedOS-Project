/* hello.c - a tour of the LeactedOS app API. */
#include "../leact_util.h"

static const struct leact_api *A;
static int clicks, beeps;
static int last_key;
static int mx = -1, my = -1;
static char msg[40];

static void draw(const struct leact_event *ev) {
    int w = ev->w;
    A->rect(0, 0, w, ev->h, A->c_face);
    A->text_scaled(10, 10, "Hello, world!", RGB(0, 70, 190), 2);
    A->text(10, 36, "A real app from a .lapp file", A->c_text);

    char b[48];
    lu_fmt(b, sizeof b, "Running for %u seconds", A->ticks() / 100);
    A->text(10, 56, b, A->c_text);
    lu_fmt(b, sizeof b, "Clicks: %d   Last key: %d", clicks, last_key);
    A->text(10, 70, b, A->c_text);
    if (mx >= 0) { lu_fmt(b, sizeof b, "Mouse at %d,%d", mx, my); A->text(10, 84, b, A->c_dark); }

    A->button(10, ev->h - 34, 70, 22, "Beep", 0);
    A->button(88, ev->h - 34, 70, 22, "Files", 0);
    A->button(166, ev->h - 34, 70, 22, "Close", 0);
    A->text(10, ev->h - 48, msg, RGB(0, 110, 0));

    /* a little smiley using the drawing calls */
    int cx = w - 40, cy = 40;
    A->circle(cx, cy, 24, RGB(255, 210, 40));
    A->circle(cx - 8, cy - 7, 3, 0); A->circle(cx + 8, cy - 7, 3, 0);
    A->line(cx - 12, cy + 8, cx - 6, cy + 14, 0); A->line(cx - 6, cy + 14, cx + 6, cy + 14, 0);
    A->line(cx + 6, cy + 14, cx + 12, cy + 8, 0);
}

static void click(int x, int y, int h) {
    clicks++; mx = x; my = y;
    if (y >= h - 34 && y < h - 12) {
        if (x >= 10 && x < 80) { A->beep(660 + 110 * (beeps++ % 5), 120); lu_fmt(msg, sizeof msg, "Beep #%d", beeps); }
        else if (x >= 88 && x < 158) {
            char n[24]; int c = 0;
            for (int i = 0; i < 15; i++) if (A->file_name(i, n, sizeof n) >= 0) c++;
            lu_fmt(msg, sizeof msg, "LeactedFS has %d file(s)", c);
            A->log("hello.lapp: listed files\n");
        }
        else if (x >= 166 && x < 236) A->close();
    }
}

LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: A->log("hello.lapp started\n"); break;
    case EV_DRAW: draw(ev); break;
    case EV_KEY: last_key = ev->key; break;
    case EV_CLICK: click(ev->x, ev->y, ev->h); break;
    case EV_MOVE: mx = ev->x; my = ev->y; break;
    case EV_CLOSE: A->log("hello.lapp closed\n"); break;
    }
    return 0;
}
