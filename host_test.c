/* Host-side test harness: runs the OS logic on a PC, renders frames to PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define HOST 1
#include "kernel.c"

static void ppm(const char *fn) {
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { u8 p[3] = { back[i] >> 16, back[i] >> 8, back[i] }; fwrite(p, 1, 3, f); }
    fclose(f);
}
static void typestr(const char *s) { for (; *s; s++) app_key(A_TERM, *s); }
static void cmd(const char *s) { typestr(s); app_key(A_TERM, '\n'); }
static void click(int x, int y) { on_press(x, y); on_release(); }
static void frame_out(const char *fn) { ticks += 25; os_draw(); ppm(fn); }

int main(void) {
    os_init(0x2BADB002, (u32[]){ 1, 639, 261120 });
    assert(wins[A_TERM].open);
    cmd("help");
    cmd("ls");
    cmd("echo hello world > a.txt");
    cmd("echo second line >> a.txt");
    cmd("cat a.txt");
    cmd("calc 12 * 7");
    cmd("calc 5 / 0");
    cmd("rm a.txt");
    cmd("super");
    cmd("disk");
    cmd("date");
    cmd("mem");
    cmd("nosuch");
    int i = fs_find("A.TXT"); assert(i >= 0 && fsdir[i].size == 24);
    assert(!memcmp(fsdata[i], "hello world\nsecond line\n", 24));
    frame_out("t1_term.ppm");

    /* shift / caps mapping */
    kbd_scancode(0x2A); kbd_scancode(0x02); kbd_scancode(0x82); kbd_scancode(0xAA);   /* shift+1 -> ! */
    assert(kq_pop() == '!');
    kbd_scancode(0x1D); kbd_scancode(0x1F); kbd_scancode(0x9D);                       /* ctrl+s */
    assert(kq_pop() == 19);
    kbd_scancode(0xE0); kbd_scancode(0x48); assert(kq_pop() == K_UP);

    /* mouse packet: move right 10, down 5 (dy = -5 in PS/2), left button */
    int ox = mx, oy = my;
    mouse_byte(0x09 | 0x20); mouse_byte(10); mouse_byte((u8)-5);
    assert(mx == ox + 10 && my == oy + 5 && (mbtn & 1));
    mouse_byte(0x08); mouse_byte(0); mouse_byte(0);
    mx = 320; my = 240;

    /* installer + persistence */
    open_app(A_INSTALL);
    assert(!fs_on_disk);
    int cx = CX(A_INSTALL) + CW(A_INSTALL) / 2, cy = CY(A_INSTALL) + 134 + 5;
    click(cx, cy); assert(inst_state == 1);
    frame_out("t2_install_confirm.ppm");
    click(cx, cy); assert(inst_state == 2 && fs_on_disk);
    frame_out("t3_install_ok.ppm");
    /* simulate reboot: reload from the fake disk */
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata); fs_on_disk = 0;
    assert(fs_check()); fs_load();
    i = fs_find("A.TXT"); assert(i >= 0 && fsdir[i].size == 24 && !memcmp(fsdata[i], "hello world\nsecond line\n", 24));
    assert(fs_find("README.TXT") >= 0);
    /* write after install persists */
    cmd("echo persisted > b.txt");
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata);
    fs_load(); assert(fs_find("B.TXT") >= 0);
    cmd("rm b.txt"); fs_load(); assert(fs_find("B.TXT") < 0);
    close_app(A_INSTALL);

    /* paint */
    open_app(A_PAINT);
    int px = CX(A_PAINT), py = CY(A_PAINT);
    click(px + 8 + 3 * 28 + 5, py + 305); assert(pcol == 3);
    on_press(px + 40, py + 40); on_move_held(px + 200, py + 120); on_move_held(px + 300, py + 60); on_release();
    click(px + 8 + 8 * 28 + 5, py + 305); assert(pcol == 8);
    on_press(px + 60, py + 200); on_move_held(px + 250, py + 230); on_release();
    assert(canvas[40 - 8 + 0][40 - 8] == 3);

    /* notepad */
    note_open_file("a.txt");
    note_key('X'); note_key('\n'); note_key('!');
    note_save();
    frame_out("t4_windows.ppm");

    open_app(A_FILES); fsel = fs_find("A.TXT");
    open_app(A_CLOCK);
    open_app(A_ABOUT);
    start_open = 1; mx = 100; my = 300;
    frame_out("t5_all.ppm");
    start_open = 0;

    /* drag a window */
    int a = A_ABOUT, ox2 = wins[a].x;
    on_press(wins[a].x + 50, wins[a].y + 10); on_move_held(wins[a].x + 150, wins[a].y + 60); on_release();
    assert(wins[a].x != ox2);
    /* close via X */
    click(wins[a].x + wins[a].w - 15, wins[a].y + 15); assert(!wins[a].open);
    /* minimize via taskbar */
    int top = top_app(); int bx = 74; for (int k = 0; k < top; k++) if (wins[k].open) bx += 108;
    click(bx + 10, H - 12); assert(wins[top].min);
    click(bx + 10, H - 12); assert(!wins[top].min && top_app() == top);
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    wins[A_FILES].x = 10; wins[A_FILES].y = 20; wins[A_CLOCK].x = 350; wins[A_CLOCK].y = 20;
    open_app(A_FILES); open_app(A_CLOCK); mx = 600; my = 400;
    frame_out("t6_files_clock.ppm");
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
