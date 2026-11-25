/* stopwatch.c - Stopwatch with laps and a countdown timer.
 * Space or the Start button starts/stops. L or Lap records a lap. R resets. T switches to the countdown;
 * in countdown mode Up/Down set the minutes before starting and it beeps when it reaches zero. */
#include "../leact_util.h"

#define MAXLAP 8

static const struct leact_api *A;
static int running, countdown, acc, t_start, lapn, setmin = 1, fired;   /* acc / t_start in centiseconds */
static int laps[MAXLAP];

static int now_cs(void) { return acc + (running ? (int)A->ticks() - t_start : 0); }
static void fmt_time(char *b, int n, int cs) {
    if (cs < 0) cs = 0;
    int m = cs / 6000, s = (cs / 100) % 60, c = cs % 100;
    lu_fmt(b, n, "%02d:%02d.%02d", m, s, c);
}
static int shown_cs(void) { return countdown ? setmin * 6000 - now_cs() : now_cs(); }
static void toggle(void) {
    if (running) { acc += (int)A->ticks() - t_start; running = 0; }
    else {
        if (countdown && shown_cs() <= 0) { acc = 0; fired = 0; }
        t_start = (int)A->ticks(); running = 1;
    }
}
static void reset(void) { running = 0; acc = 0; lapn = 0; fired = 0; }
static void lap(void) {
    if (!running || countdown) return;
    if (lapn < MAXLAP) laps[lapn++] = now_cs();
    else { for (int i = 1; i < MAXLAP; i++) laps[i - 1] = laps[i]; laps[MAXLAP - 1] = now_cs(); }
}
static void set_mode(int cd) { countdown = cd; reset(); }
static void bump(int d) { if (countdown && !running && now_cs() == 0) { setmin += d; if (setmin < 1) setmin = 1; if (setmin > 99) setmin = 99; } }
static void draw(void) {
    A->rect(0, 0, 280, 270, A->c_face);
    char b[32];
    int cs = shown_cs();
    uint32_t col = (countdown && cs <= 0) ? RGB(220, 0, 0) : A->c_text;
    A->rect(10, 10, 260, 60, RGB(20, 28, 20));
    fmt_time(b, sizeof b, cs);
    A->text_scaled(44, 28, b, (countdown && cs <= 0) ? RGB(255, 80, 80) : RGB(90, 255, 110), 3);
    A->text(12, 78, countdown ? "COUNTDOWN" : "STOPWATCH", col);
    if (countdown) { lu_fmt(b, sizeof b, "Set: %d min (Up/Down)", setmin); A->text(100, 78, b, A->c_dark); }
    A->button(10, 94, 80, 28, running ? "Stop" : "Start", running);
    A->button(100, 94, 80, 28, countdown ? "Reset" : "Lap", 0);
    A->button(190, 94, 80, 28, countdown ? "Stopwatch" : "Timer", 0);
    if (!countdown) {
        A->text(12, 134, "Laps", A->c_text);
        for (int i = 0; i < lapn; i++) {
            char t[16]; fmt_time(t, sizeof t, laps[i] - (i ? laps[i - 1] : 0));
            lu_fmt(b, sizeof b, "%d  %s", i + 1, t); A->text(12, 150 + i * 14, b, A->c_text);
            fmt_time(t, sizeof t, laps[i]); A->text(150, 150 + i * 14, t, A->c_dark);
        }
    }
}
LAPP_MAIN {
    A = api;
    switch (ev->type) {
    case EV_INIT: reset(); break;
    case EV_TICK:
        if (running && countdown && !fired && shown_cs() <= 0) { fired = 1; running = 0; acc = setmin * 6000; A->beep(880, 150); A->beep(1175, 250); }
        break;
    case EV_KEY:
        if (ev->key == ' ') toggle();
        else if (ev->key == 'l' || ev->key == 'L') lap();
        else if (ev->key == 'r' || ev->key == 'R') reset();
        else if (ev->key == 't' || ev->key == 'T') set_mode(!countdown);
        else if (ev->key == LK_UP) bump(1);
        else if (ev->key == LK_DOWN) bump(-1);
        break;
    case EV_CLICK:
        if (ev->y >= 94 && ev->y < 122) {
            if (ev->x >= 10 && ev->x < 90) toggle();
            else if (ev->x >= 100 && ev->x < 180) { if (countdown) reset(); else lap(); }
            else if (ev->x >= 190 && ev->x < 270) set_mode(!countdown);
        }
        break;
    case EV_DRAW: draw(); break;
    }
    return 0;
}
