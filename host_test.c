/* Host-side test harness: runs the OS logic on a PC, renders frames to PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define HOST 1
#include "kernel.c"
#include "tests/fakenet.h"
static int host_nic_send(const u8 *f, int n) { return fake_send(f, n); }
static int host_nic_recv(u8 *b, int max) { return fake_recv(b, max); }
static void net_steps(int n) { for (int i = 0; i < n; i++) { net_poll(); srv_tick(); tjob_tick(); br_check(); ticks += 1; } }
static int net_idle(int max) { for (int i = 0; i < max; i++) { net_steps(1); if (!tjob && !br_wait && !http_active()) return i; } return -1; }

static u8 *loadimg(const char *fn, u32 *secs) {
    FILE *f = fopen(fn, "rb"); assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(n); assert(fread(b, 1, n, f) == (size_t)n); fclose(f);
    *secs = n / 512; return b;
}
static u8 *readfile(const char *fn, long *len) {
    FILE *f = fopen(fn, "rb"); assert(f);
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc(*len + 1); assert(fread(b, 1, *len, f) == (size_t)*len); fclose(f);
    return b;
}
extern int hello_main(const struct leact_api *, const struct leact_event *);
extern int snake_main(const struct leact_api *, const struct leact_event *);
extern int doodle_main(const struct leact_api *, const struct leact_event *);
static int term_has(const char *needle) {
    for (int r = 0; r < TB; r++) if (strstr(tg[r], needle)) return 1;
    return 0;
}
static void term_dump(void) { for (int r = 0; r < TB; r++) if (tg[r][0]) fprintf(stderr, "| %s\n", tg[r]); }
static int find_ent(const char *n) { for (int i = 0; i < fm_n; i++) if (!strcmp(fm_ents[i].name, n)) return i; return -1; }
static void ppm(const char *fn) {
    FILE *f = fopen(fn, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { u8 p[3] = { back[i] >> 16, back[i] >> 8, back[i] }; fwrite(p, 1, 3, f); }
    fclose(f);
}
static void typestr(const char *s) { for (; *s; s++) app_key(A_TERM, *s); }
static u8 *slurp(const char *path, int *n) {
    FILE *f = fopen(path, "rb"); assert(f); fseek(f, 0, SEEK_END); *n = (int)ftell(f); fseek(f, 0, SEEK_SET);
    u8 *b = malloc((size_t)*n + 1); assert(fread(b, 1, (size_t)*n, f) == (size_t)*n); fclose(f); return b;
}
static void typestr_to(int a, const char *s) { for (; *s; s++) app_key(a, (unsigned char)*s); }
static int br_has(const char *needle) {
    int nl = (int)strlen(needle);
    for (int r = 0; r < br_nrows; r++) {
        int rl = br_rows[r].len; const char *t = br_t + br_rows[r].off;
        for (int i = 0; i + nl <= rl; i++) if (!memcmp(t + i, needle, (size_t)nl)) return 1;
    }
    return 0;
}
static void cmd(const char *s) { typestr(s); app_key(A_TERM, '\n'); }
static void click(int x, int y) { on_press(x, y); on_release(); }
static void frame_out(const char *fn) { ticks += 25; os_draw(); ppm(fn); }

int main(void) {
    fakedisk[0] = calloc(20000, 512); fakesect[0] = 20000;               /* C: blank */
    fakedisk[1] = loadimg("tests/out/fat16.img", &fakesect[1]);        /* D: FAT16 with MBR */
    fakedisk[2] = loadimg("tests/out/fat32.img", &fakesect[2]);        /* E: FAT32 */
    fakedisk[3] = loadimg("tests/out/fat12.img", &fakesect[3]);        /* F: FAT12 */
    os_init(0x2BADB002, (u32[]){ 1, 639, 261120 });
    assert(drv[0].type == DT_EMPTY && drv[1].type == DT_FAT && drv[1].fatbits == 16);
    assert(drv[2].type == DT_FAT && drv[2].fatbits == 32 && drv[3].type == DT_FAT && drv[3].fatbits == 12);
    assert(!fs_on_disk && fs_drive == -1);
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
    assert(inst_sel == 0);
    int cx = CX(A_INSTALL) + CW(A_INSTALL) / 2, cy = CY(A_INSTALL) + 164 + 5;
    click(cx, cy); assert(inst_state == 1);
    frame_out("t2_install_confirm.ppm");
    click(cx, cy); assert(inst_state == 2 && fs_on_disk && fs_drive == 0 && drv[0].type == DT_LEACTED);
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

    open_app(A_FILES);
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
    int top = top_app(); int bx = TB_X0; for (int k = 0; k < top; k++) if (wins[k].open) bx += tb_bw() + 4;
    click(bx + 10, H - 12); assert(wins[top].min);
    click(bx + 10, H - 12); assert(!wins[top].min && top_app() == top);
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    wins[A_FILES].x = 10; wins[A_FILES].y = 20; wins[A_CLOCK].x = 350; wins[A_CLOCK].y = 20;
    open_app(A_FILES); open_app(A_CLOCK); mx = 600; my = 400;
    frame_out("t6_files_clock.ppm");
    /* ---- themes, settings, task manager ---- */
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    wins[A_TERM].x = 90; wins[A_TERM].y = 24;
    open_app(A_TERM); cmd("theme unity"); assert(th_id == TH_UNITY);
    cmd("theme"); cmd("help");
    open_app(A_SETTINGS); open_app(A_TASK);
    mx = 300; my = 300; start_open = 0;
    frame_out("t7_unity.ppm");
    start_open = 1; frame_out("t7b_unity_dash.ppm"); start_open = 0;
    /* launcher click toggles an app */
    click(20, LAUNCH_TOP + A_CLOCK * LAUNCH_SLOT + 10); assert(wins[A_CLOCK].open);
    click(20, LAUNCH_TOP + A_CLOCK * LAUNCH_SLOT + 10); assert(wins[A_CLOCK].min);
    /* unity close button on the left */
    int cx0, cy0, cw0, ch0; close_rect(A_CLOCK, &cx0, &cy0, &cw0, &ch0); raise_app(A_CLOCK);
    click(cx0 + 5, cy0 + 5); assert(!wins[A_CLOCK].open);
    cmd("theme kde"); assert(th_id == TH_KDE);
    frame_out("t8_kde.ppm");
    start_open = 1; frame_out("t8b_kde_menu.ppm"); start_open = 0;
    cmd("theme xp"); assert(th_id == TH_XP);
    frame_out("t9_xp.ppm");
    /* settings persistence: theme survives a reload */
    cmd("theme unity");
    th_id = 0; show_icons = 1; term_color_idx = 0;
    settings_load(); assert(th_id == TH_UNITY);
    /* settings window click: pick KDE row, toggle icons, term colour */
    raise_app(A_SETTINGS); int sx = CX(A_SETTINGS), sy = CY(A_SETTINGS);
    click(sx + 100, sy + SET_ROW(2) + 10); assert(th_id == TH_KDE);
    click(sx + 20, sy + 172); assert(!show_icons);
    click(sx + 20 + 2 * 40, sy + 215); assert(term_color_idx == 2);
    frame_out("t10_kde_settings_noicons.ppm");
    click(sx + 20, sy + 172); assert(show_icons);
    /* task manager: end task */
    tsel = A_SETTINGS;
    raise_app(A_TASK); int tx = CX(A_TASK), ty = CY(A_TASK);
    click(tx + 20, ty + 172); assert(!wins[A_SETTINGS].open);
    for (int k = 0; k < 3; k++) { theme_apply(k); for (int a2 = 0; a2 < N_APPS; a2++) { open_app(a2); assert(wins[a2].y >= area_y0); } }
    /* ---- calculator ---- */
    cmd("theme xp");
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    open_app(A_CALC);
    char b[24];
    #define KEYS(str) do { for (const char *p_ = (str); *p_; p_++) calc_key(*p_); } while (0)
    calc_reset(); KEYS("12.5*4="); calc_fmt(c_acc, b); assert(!strcmp(b, "50"));
    calc_reset(); KEYS("10/4="); calc_fmt(c_acc, b); assert(!strcmp(b, "2.5"));
    calc_reset(); KEYS("1/3="); calc_fmt(c_acc, b); assert(!strcmp(b, "0.33"));
    calc_reset(); KEYS("5/0="); assert(c_err);
    calc_press('C'); assert(!c_err);
    calc_reset(); KEYS("7+8*2="); calc_fmt(c_acc, b); assert(!strcmp(b, "30"));      /* left to right, like a basic calculator */
    calc_reset(); KEYS("9-12="); calc_fmt(c_acc, b); assert(!strcmp(b, "-3"));
    calc_reset(); KEYS("3.14"); calc_fmt_entry(b); assert(!strcmp(b, "3.14"));
    KEYS("\b"); calc_fmt_entry(b); assert(!strcmp(b, "3.1"));
    calc_reset(); KEYS("50%"); calc_fmt_entry(b); assert(!strcmp(b, "0.50"));
    calc_reset(); KEYS("5"); calc_press('N'); KEYS("*3="); calc_fmt(c_acc, b); assert(!strcmp(b, "-15"));
    calc_reset(); KEYS("99999999*99999999="); assert(c_err);
    calc_reset(); KEYS("2*3="); KEYS("+4="); calc_fmt(c_acc, b); assert(!strcmp(b, "10"));  /* continue after = */
    calc_reset();
    /* clicking buttons: 6 * 7 = */
    int kx = CX(A_CALC), ky = CY(A_CALC), cox = (CW(A_CALC) - 196) / 2;
    raise_app(A_CALC);
    #define BTN(i) click(kx + cox + ((i) % 4) * 50 + 20, ky + 58 + ((i) / 4) * 42 + 15)
    BTN(10); BTN(7); BTN(4); BTN(19);   /* 6 * 7 = */
    calc_fmt(c_acc, b); assert(!strcmp(b, "42"));
    frame_out("t11_calc_xp.ppm");
    open_app(A_ABOUT); wins[A_ABOUT].x = 40; wins[A_ABOUT].y = 20; wins[A_CALC].x = 300;
    frame_out("t12_about_calc_xp.ppm");
    start_open = 1; mx = 40; my = 200; frame_out("t13_xp_menu.ppm"); start_open = 0;
    splash(60, "Files and settings"); ppm("t14_splash.ppm");
    theme_apply(TH_UNITY); frame_out("t15_unity_logo.ppm");
    /* ================= drives, FAT, File Manager, Open With, viewers ================= */
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    theme_apply(TH_XP);
    open_app(A_FILES);
    assert(fm_drive == fs_drive);                       /* LeactedFS drive is selected first */
    int fx = CX(A_FILES), fy = CY(A_FILES);
    /* click drive D: (row 1) = FAT16 on an MBR partition */
    click(fx + 20, fy + 34 + 1 * 36 + 10); assert(fm_drive == 1);
    assert(!strcmp(fm_path, "D:\\") && fm_n == 10);
    assert(fm_ents[0].isdir && !strcmp(fm_ents[0].name, "Documents") && !strcmp(fm_ents[1].name, "Many") && fm_ents[1].isdir);
    assert(find_ent("Hello World.txt") >= 0 && find_ent("notes.md") >= 0 && find_ent("README.TXT") >= 0);
    assert(find_ent("A Very Long File Name Indeed 1234567890.txt") >= 0);
    int ei = find_ent("EMPTY.TXT"); assert(ei >= 0 && fm_ents[ei].size == 0);
    ei = find_ent("big.bin"); assert(fm_ents[ei].size == 10000 && !fm_ents[ei].isdir);
    frame_out("t20_files_fat16.ppm");

    /* README.TXT -> Open With dialog (recommended: Notepad) -> content */
    ei = find_ent("README.TXT");
    click(fx + 104 + 20, fy + 47 + ei * 11 + 4); assert(fm_sel == ei);
    click(fx + 104 + 20, fy + 47 + ei * 11 + 4);        /* second click = double-click */
    assert(wins[A_OPENWITH].open && ow_rec == 0 && ow_sel == 0 && !strcmp(ow_ref.name, "README.TXT"));
    frame_out("t21_openwith.ppm");
    int owx = CX(A_OPENWITH), owy = CY(A_OPENWITH);
    click(owx + CW(A_OPENWITH) - 146 + 10, owy + CH(A_OPENWITH) - 30 + 5);   /* Open */
    assert(!wins[A_OPENWITH].open && wins[A_NOTE].open);
    assert(!strncmp(nbuf, "Hello from a FAT drive!\nSecond line.\n", 37) && nlen == 37);
    close_app(A_NOTE);

    /* big.bin (multi-cluster) -> Hex Viewer */
    fm_sel = find_ent("big.bin"); fm_open_sel();
    assert(ow_rec == 2); ow_launch();
    { long n; u8 *ex = readfile("tests/out/exp_big.bin", &n); assert(hlen == (u32)n && !memcmp(hbuf, ex, n)); free(ex); }
    hex_key(K_PGDN); assert(hscroll > 0); hex_key(K_END); hex_key(K_HOME); assert(hscroll == 0);
    frame_out("t22_hex.ppm");
    close_app(A_HEX);

    /* 24-bit and 8-bit BMP -> Image Viewer */
    fm_sel = find_ent("pic24.bmp"); fm_open_sel(); assert(ow_rec == 1); ow_launch();
    assert(iok && iw == 64 && ih == 48 && ibpp == 24);
    assert(img_px(10, 20) == (u32)((128 << 16) | ((20 * 255 / 48) << 8) | (10 * 255 / 64)));
    frame_out("t23_image24.ppm");
    fm_sel = find_ent("pic8.bmp"); fm_open_sel(); ow_launch();
    assert(iok && iw == 40 && ih == 30 && ibpp == 8);
    assert(img_px(3, 4) == (u32)((49 << 16) | (248 << 8) | 7));
    close_app(A_IMG);
    fm_sel = find_ent("README.TXT"); fm_open_sel(); ow_sel = 1; ow_launch();       /* text as an image */
    assert(!iok && imsg); close_app(A_IMG);
    fm_sel = find_ent("EMPTY.TXT"); fm_open_sel(); ow_sel = 2; ow_launch(); assert(hlen == 0); close_app(A_HEX);

    /* folders: E: = FAT32, go 3 levels deep and back */
    click(fx + 20, fy + 34 + 2 * 36 + 10); assert(fm_drive == 2 && !strcmp(fm_path, "E:\\"));
    fm_sel = find_ent("Documents"); fm_open_sel(); assert(!strcmp(fm_path, "E:\\Documents\\") && fm_depth == 1);
    assert(find_ent("Report 2026.txt") >= 0 && find_ent("deep") >= 0);
    fm_sel = find_ent("deep"); fm_open_sel();
    fm_sel = find_ent("More Stuff"); fm_open_sel();
    assert(!strcmp(fm_path, "E:\\Documents\\deep\\More Stuff\\") && fm_depth == 3 && fm_n == 1 && !strcmp(fm_ents[0].name, "y.txt"));
    { struct fref f = { 2, 2, fm_ents[0].cluster, fm_ents[0].size, "y.txt" }; u8 t[8] = {0}; assert(fref_load(&f, t, 8) == 2 && !memcmp(t, "y\n", 2)); }
    click(fx + 20, fy + 8); assert(fm_depth == 2);       /* Up button */
    files_key('\b'); files_key('\b'); assert(fm_depth == 0 && !strcmp(fm_path, "E:\\"));
    files_key('\b'); assert(fm_depth == 0);
    /* a folder with 40 files: scrolling */
    fm_sel = find_ent("Many"); fm_open_sel(); assert(fm_n == 40);
    files_key(K_END); assert(fm_sel == 39 && fm_scroll == 40 - FM_ROWS);
    frame_out("t24_files_scroll.ppm");
    files_key(K_HOME); assert(fm_sel == 0 && fm_scroll == 0);
    files_key(K_PGDN); assert(fm_sel == FM_ROWS);
    click(fx + 104 + (CW(A_FILES) - 110) - 13 + 5, fy + 47 + FM_ROWS * 11 - 12 + 5); assert(fm_scroll == 4);
    files_key('\b');

    /* FAT12 with a 900 KB file: FAT entries straddle sector boundaries */
    click(fx + 20, fy + 34 + 3 * 36 + 10); assert(fm_drive == 3);
    ei = find_ent("huge.bin"); assert(ei >= 0 && fm_ents[ei].size == 900000);
    { long n; u8 *ex = readfile("tests/out/exp_huge.bin", &n); u8 *got = malloc(1000000);
      struct fref f = { 2, 3, fm_ents[ei].cluster, fm_ents[ei].size, "huge.bin" };
      assert(fref_load(&f, got, 1000000) == (u32)n && !memcmp(got, ex, n)); free(ex); free(got); }

    /* import a FAT file into LeactedFS (C:) and make sure it was saved to the drive */
    click(fx + 20, fy + 34 + 1 * 36 + 10); assert(fm_drive == 1);
    fm_sel = find_ent("notes.md"); click(fx + 68 + 10, CH(A_FILES) + fy - 22 + 5);   /* Import button */
    assert(fs_find("NOTES.MD") >= 0 && !strncmp((char *)fsdata[fs_find("NOTES.MD")], "# lowercase 8.3 name\n", 21));
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata); fs_load(); assert(fs_find("NOTES.MD") >= 0);
    fm_sel = find_ent("big.bin"); fm_import(); assert(fsdir[fs_find("BIG.BIN")].size == 10000);   /* v2: no more 4 KB cut */
    { long en; u8 *ex = readfile("tests/out/exp_big.bin", &en); assert(en == 10000 && !memcmp(fsdata[fs_find("BIG.BIN")], ex, 10000)); free(ex); }
    /* LeactedFS view: new + delete via buttons */
    click(fx + 20, fy + 34 + 0 * 36 + 10); assert(fm_drive == 0 && fm_is_leacted());
    int before = fm_n; click(fx + 196 + 10, CH(A_FILES) + fy - 22 + 5); assert(fm_n == before + 1 && wins[A_NOTE].open);
    close_app(A_NOTE);
    fm_sel = find_ent("NEW1.TXT"); click(fx + 134 + 10, CH(A_FILES) + fy - 22 + 5); assert(find_ent("NEW1.TXT") < 0);
    /* unformatted/empty & delete refusal on FAT */
    click(fx + 20, fy + 34 + 2 * 36 + 10); fm_sel = 0; fm_delete(); assert(!strncmp(fm_msg, "Only LeactedFS", 14));
    frame_out("t25_files_fat32_msg.ppm");

    /* installer lists all drives and can target another one */
    open_app(A_INSTALL); frame_out("t26_installer.ppm"); close_app(A_INSTALL);


    /* ================= v7: LeactedFS v2 (64 files of 128 KB) and v1 compatibility ================= */
    {
    assert(fs_ver == 2 && fs_max_files == 64 && fs_slot_limit == 131072 && fs_on_disk);
    u8 *big = malloc(131072 + 8); for (int i = 0; i < 131072 + 8; i++) big[i] = (u8)(i * 7 + i / 251);
    int bi = fs_create("BIGFILE.DAT"); assert(bi >= 0);
    assert(fs_store(bi, big, 120000) == 0 && fsdir[bi].size == 120000);
    assert(fs_store(bi, big, 200000) == 0 && fsdir[bi].size == 131072);     /* clamped to the slot size */
    assert(fs_store(bi, big, 120000) == 0 && fsdata[bi][120001] == 0);       /* shrinking clears the old tail */
    for (int i = 0; fs_count() < FS_MAX; i++) { char nm[24]; snprintf(nm, 24, "F%02d.TXT", i); int k = fs_create(nm); assert(k >= 0); fs_store(k, nm, strlen(nm)); }
    assert(fs_count() == 64 && fs_create("ONE_TOO_MANY.TXT") < 0);
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata); fs_load();   /* everything must come back from the disk */
    assert(fs_ver == 2 && fs_count() == 64);
    bi = fs_find("BIGFILE.DAT"); assert(bi >= 0 && fsdir[bi].size == 120000 && !memcmp(fsdata[bi], big, 120000));
    assert(!strcmp((char *)fsdata[fs_find("F05.TXT")], "F05.TXT"));
    assert(fs_delete(fs_find("F05.TXT")) == 0 && fs_create("NEW.TXT") >= 0 && fs_find("F05.TXT") < 0);
    fs_delete(fs_find("NEW.TXT"));
    assert(xapi.file_write("API.BIN", big, 100000) == 0 && xapi.file_write("API2.BIN", big, 140000) == -1);   /* apps get the new limit too */
    assert(fs_find("API2.BIN") < 0 || fsdir[fs_find("API2.BIN")].size == 0); if (fs_find("API2.BIN") >= 0) fs_delete(fs_find("API2.BIN"));
    assert(fs_delete(fs_find("API.BIN")) == 0);
    tclear(); cmd("echo hello v2 > V2NOTE.TXT"); assert(fs_find("V2NOTE.TXT") >= 0);

    /* a disk that is too small for v2 is refused cleanly */
    { u32 keep = drv[0].sectors; drv[0].sectors = 10000; assert(!fs_install(0)); drv[0].sectors = keep; }

    /* ---- an old v1 disk must still mount, work and upgrade safely ---- */
    {
      u8 *dk = fakedisk[0]; memset(dk, 0, (size_t)20000 * 512);
      u8 *sb = dk + 512; memcpy(sb, "LEACTEDFS\x01", 10); u32 tot = 20000; memcpy(sb + 16, &tot, 4);
      u8 *dir = dk + 2 * 512; memcpy(dir, "LDIR", 4);
      struct fent *fe = (struct fent *)(dir + 4);
      strcpy(fe[0].name, "OLD1.TXT"); fe[0].size = 13; fe[0].used = 1; memcpy(dk + 8 * 512, "hello from v1", 13);
      strcpy(fe[1].name, "OLD2.BIN"); fe[1].size = 4096; fe[1].used = 1; for (int i = 0; i < 4096; i++) dk[(8 + 8) * 512 + i] = (u8)(i * 3);
      drive_probe(0); assert(drv[0].type == DT_LEACTED); fs_check(); fs_load();
      assert(fs_ver == 1 && fs_max_files == 15 && fs_slot_limit == 4096 && fs_count() == 2);
      assert(!memcmp(fsdata[fs_find("OLD1.TXT")], "hello from v1", 13) && fsdir[fs_find("OLD2.BIN")].size == 4096 && fsdata[fs_find("OLD2.BIN")][100] == (u8)300);
      int k = fs_create("V1NEW.TXT"); assert(k >= 0 && fs_store(k, big, 9000) == 0 && fsdir[k].size == 4096);   /* still capped at 4 KB */
      for (int i = 0; fs_count() < 15; i++) { char nm[24]; snprintf(nm, 24, "G%02d", i); assert(fs_create(nm) >= 0); }
      assert(fs_create("SIXTEENTH") < 0);
      tclear(); cmd("disk"); cmd("fsupgrade");                              /* writes real data into the old volume first */
      assert(fs_ver == 2); fs_load(); assert(fs_ver == 2 && fs_count() == 15);   /* upgraded: reloads as v2 */
      /* redo: rebuild the same v1 disk, then cut the power after every possible number of writes */
      static u8 *snap; if (!snap) snap = malloc((size_t)20000 * 512);
      memset(dk, 0, (size_t)20000 * 512); memcpy(sb, "LEACTEDFS\x01", 10); memcpy(sb + 16, &tot, 4); memcpy(dir, "LDIR", 4);
      strcpy(fe[0].name, "OLD1.TXT"); fe[0].size = 13; fe[0].used = 1; memcpy(dk + 8 * 512, "hello from v1", 13);
      strcpy(fe[1].name, "OLD2.BIN"); fe[1].size = 4096; fe[1].used = 1; for (int i = 0; i < 4096; i++) dk[(8 + 8) * 512 + i] = (u8)(i * 3);
      memcpy(snap, dk, (size_t)20000 * 512);
      int saw_fail = 0, saw_ok = 0;
      for (int K = 0; K < 12; K++) {
          memcpy(dk, snap, (size_t)20000 * 512); drive_probe(0); fs_check(); fs_load(); assert(fs_ver == 1);
          host_fail_after = K; int r = fs_upgrade(); host_fail_after = -1;
          fs_load();                                                          /* what the next boot would see */
          assert(fs_count() == 2 && !memcmp(fsdata[fs_find("OLD1.TXT")], "hello from v1", 13) && fsdata[fs_find("OLD2.BIN")][100] == (u8)300);
          if (r == 0) { assert(fs_ver == 1); saw_fail++; } else { assert(r == 1 && fs_ver == 2); saw_ok++; }
      }
      assert(saw_fail > 3 && saw_ok > 0);                                     /* both outcomes happened, never a half-way state */
    }
    /* fresh v2 install again for the rest of the tests */
    memset(fakedisk[0], 0, (size_t)20000 * 512); drive_probe(0); fs_check(); fs_load();
    assert(fs_install(0) && fs_ver == 2 && fs_on_disk);
    free(big);
    }


    /* ================= v7: terminal paths, FAT drives and the new commands ================= */
    {
    char hx[64]; long en; u8 *ex;
    is_root = 0; open_app(A_TERM); tclear();
    tclear(); cmd("ls D:"); assert(term_has("Documents") && term_has("<DIR>") && term_has("README.TXT") && term_has("Hello World.txt"));
    tclear(); cmd("ls Z:"); assert(term_has("Cannot use that drive"));
    tclear(); cmd("ls D:/nosuch"); assert(term_has("Not found: D:/nosuch"));
    tclear(); cmd("cat D:/README.TXT"); assert(term_has("Hello from a FAT drive!") && term_has("Second line."));
    tclear(); cmd("cat \"D:/Hello World.txt\""); assert(term_has("long name with spaces"));
    tclear(); cmd("cat d:\\documents\\deep\\x.txt"); assert(term_has("deep file"));
    tclear(); cmd("cat D:/Documents"); assert(term_has("That is a folder"));
    tclear(); cmd("cat D:/big.bin");                                        /* binary data prints safely */
    for (int r = 0; r < TB; r++) for (int c = 0; tg[r][c]; c++) assert((u8)tg[r][c] >= 32 && (u8)tg[r][c] < 127);
    tclear(); cmd("tree D:/Documents"); assert(term_has("deep\\") && term_has("More Stuff\\") && term_has("y.txt") && term_has("3 file(s), 2 folder(s)"));
    /* working folder */
    cmd("cd D:/Documents/deep"); assert(tcwd.drive == 1 && tcwd.depth == 2 && strstr(prompt(), "D:\\Documents\\deep\\>"));
    tclear(); cmd("pwd"); assert(term_has("D:\\Documents\\deep\\"));
    tclear(); cmd("ls"); assert(term_has("More Stuff") && term_has("x.txt") && term_has("1 file(s), 1 folder(s)"));
    tclear(); cmd("cat x.txt"); assert(term_has("deep file"));
    cmd("cd \"More Stuff\""); tclear(); cmd("cat y.txt"); assert(term_has("y"));
    cmd("cd ..\\.."); tclear(); cmd("pwd"); assert(term_has("D:\\Documents\\") && !strstr(prompt(), "deep"));
    cmd("cd .."); cmd("cd .."); assert(tcwd.depth == 0 && tcwd.drive == 1);
    tclear(); cmd("cd nosuch"); assert(term_has("Not found: nosuch"));
    tclear(); cmd("cd README.TXT"); assert(term_has("Not a folder"));
    tclear(); cmd("cat README.TXT"); assert(term_has("Hello from a FAT drive!"));          /* relative to D:\ */
    tclear(); cmd("cat C:README.TXT"); assert(term_has("Welcome to LeactedOS"));            /* drive prefix switches volume */
    cmd("cd"); assert(tcwd.drive < 0 && !strcmp(prompt(), "leacted> "));
    /* reading commands */
    tclear(); cmd("wc D:/README.TXT"); assert(term_has("2 lines  7 words  39 bytes"));
    tclear(); cmd("head -n 1 D:/README.TXT"); assert(term_has("Hello from") && !term_has("Second"));
    tclear(); cmd("tail -n 1 D:/README.TXT"); assert(term_has("Second line.") && !term_has("Hello from"));
    tclear(); cmd("tail D:/Many/F07.TXT"); assert(term_has("file 7"));
    tclear(); cmd("grep -n second D:/README.TXT"); assert(term_has("(no match)"));
    tclear(); cmd("grep -i -n second D:/README.TXT"); assert(term_has("2:Second line."));
    tclear(); cmd("grep -c Hello D:/README.TXT"); assert(term_has("1"));
    cmd("cd D:/Many"); tclear(); cmd("grep 7"); assert(term_has("F07.TXT:file 7") && term_has("F37.TXT:file 37") && !term_has("F08"));
    cmd("cd");
    ex = readfile("tests/out/exp_big.bin", &en);
    snprintf(hx, sizeof hx, "%02x %02x %02x %02x %02x %02x %02x %02x", ex[0], ex[1], ex[2], ex[3], ex[4], ex[5], ex[6], ex[7]);
    tclear(); cmd("hexdump D:/big.bin"); assert(term_has(hx) && term_has("(more: hexdump D:/big.bin 128)"));
    snprintf(hx, sizeof hx, "%02x %02x %02x %02x %02x %02x %02x %02x", ex[8000], ex[8001], ex[8002], ex[8003], ex[8004], ex[8005], ex[8006], ex[8007]);
    tclear(); cmd("hexdump D:/big.bin 8000"); assert(term_has("01f40") && term_has(hx));
    tclear(); cmd("hexdump D:/README.TXT 9999"); assert(term_has("Offset is past the end"));
    free(ex);
    /* cp / mv / touch (LeactedFS is the only writable place) */
    tclear(); cmd("cp D:/notes.md COPYNOTE.MD"); assert(fs_find("COPYNOTE.MD") >= 0 && !strncmp((char *)fsdata[fs_find("COPYNOTE.MD")], "# lowercase 8.3 name\n", 21));
    tclear(); cmd("cp D:/notes.md COPYNOTE.MD"); assert(term_has("exists. Use 'super'"));
    cmd("super"); tclear(); cmd("cp D:/notes.md COPYNOTE.MD"); assert(term_has("Saved")); cmd("super"); assert(!is_root);
    tclear(); cmd("cp D:/Many/F03.TXT C:"); assert(!strcmp((char *)fsdata[fs_find("F03.TXT")], "file 3\n"));
    tclear(); cmd("cp D:/notes.md E:/x"); assert(term_has("can only be copied into LeactedFS"));
    tclear(); cmd("cp D:/notes.md a\\b"); assert(term_has("no folders"));
    tclear(); cmd("cp nosuch X"); assert(term_has("Not found: nosuch"));
    tclear(); cmd("cp D:/Documents X"); assert(term_has("That is a folder"));
    tclear(); cmd("cp COPYNOTE.MD COPYNOTE.MD"); assert(term_has("same file"));
    ex = readfile("tests/out/exp_big.bin", &en);
    tclear(); cmd("cp D:/big.bin BIGCOPY.BIN"); assert(fsdir[fs_find("BIGCOPY.BIN")].size == 10000 && !memcmp(fsdata[fs_find("BIGCOPY.BIN")], ex, 10000)); free(ex);
    ex = readfile("tests/out/exp_huge.bin", &en);
    tclear(); cmd("cp F:/huge.bin HUGE.BIN"); assert(term_has("Copied the first 128 KB of 900000 bytes") && fsdir[fs_find("HUGE.BIN")].size == 131072 && !memcmp(fsdata[fs_find("HUGE.BIN")], ex, 131072)); free(ex);
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata); fs_load();             /* all of it is really on the disk */
    assert(fs_find("COPYNOTE.MD") >= 0 && fsdir[fs_find("HUGE.BIN")].size == 131072);
    tclear(); cmd("mv COPYNOTE.MD RENAMED.MD"); assert(term_has("Renamed.") && fs_find("RENAMED.MD") >= 0 && fs_find("COPYNOTE.MD") < 0);
    memset(fsdir, 0, sizeof fsdir); memset(fsdata, 0, sizeof fsdata); fs_load(); assert(fs_find("RENAMED.MD") >= 0 && !strncmp((char *)fsdata[fs_find("RENAMED.MD")], "# lowercase", 11));
    tclear(); cmd("mv RENAMED.MD F03.TXT"); assert(term_has("already exists"));
    tclear(); cmd("mv nosuch X"); assert(term_has("Not found in LeactedFS"));
    tclear(); cmd("rename RENAMED.MD C:AGAIN.MD"); assert(fs_find("AGAIN.MD") >= 0);
    tclear(); cmd("touch EMPTYNEW.TXT"); assert(fs_find("EMPTYNEW.TXT") >= 0 && fsdir[fs_find("EMPTYNEW.TXT")].size == 0);
    { const char *del[] = { "AGAIN.MD", "F03.TXT", "BIGCOPY.BIN", "HUGE.BIN", "EMPTYNEW.TXT" }; for (int q = 0; q < 5; q++) fs_delete(fs_find(del[q])); }
    /* history and scrollback */
    tclear(); cmd("history"); assert(term_has("touch EMPTYNEW.TXT") && term_has("rename RENAMED.MD C:AGAIN.MD") && nhist == NHIST);
    tclear(); cmd("tree D:/Many"); cmd("ls D:/Many"); cmd("ls D:/Many");
    assert(trow > TR + 20);
    raise_app(A_TERM); app_key(A_TERM, K_PGUP); assert(tscroll == TR - 3); frame_out("t41_term_scrollback.ppm");
    app_key(A_TERM, K_PGUP); app_key(A_TERM, K_PGUP); app_key(A_TERM, K_PGUP); assert(tscroll <= trow - TR + 1);
    app_key(A_TERM, K_PGDN); app_key(A_TERM, K_PGDN); app_key(A_TERM, K_PGDN); app_key(A_TERM, K_PGDN); app_key(A_TERM, K_PGDN); assert(tscroll == 0);
    app_key(A_TERM, K_PGUP); app_key(A_TERM, 'x'); assert(tscroll == 0);                    /* typing jumps back to the bottom */
    app_key(A_TERM, '\b');
    typestr("cat D:/Documents/deep/some-quite-long-name-that-goes-past-the-edge-of-the-window.txt"); raise_app(A_TERM); frame_out("t42_term_longline.ppm");
    for (int i = 0; i < 100; i++) app_key(A_TERM, '\b');
    cmd("cd D:/Documents/deep"); tclear(); cmd("ls"); raise_app(A_TERM); frame_out("t43_term_paths.ppm"); cmd("cd");
    }

    /* ================= v6: loadable apps (.lapp) ================= */
    {
    long hn, sn, dn;
    u8 *hello = readfile("sdk/out/hello.lapp", &hn), *snake = readfile("sdk/out/snake.lapp", &sn), *doodle = readfile("sdk/out/doodle.lapp", &dn);
    struct lapp_header h;
    assert(!lapp_check(hello, hn, &h) && !strcmp(h.name, "Hello") && h.win_w == 340 && h.win_h == 190 && h.nreloc > 0 && h.entry_off == 0);
    assert(!lapp_check(snake, sn, &h) && !strcmp(h.name, "Snake") && h.win_w == 320 && h.win_h == 248);
    assert(!lapp_check(doodle, dn, &h) && !strcmp(h.name, "Doodle") && h.bss_size > 30000);
    /* damaged / hostile files are refused with a clear message */
    u8 *bad = malloc(hn + 16);
    memcpy(bad, hello, hn); bad[0] ^= 1;               assert(!strcmp(lapp_check(bad, hn, &h), "Not a LeactedOS app (bad signature)"));
    memcpy(bad, hello, hn); bad[100] ^= 0x10;          assert(!strcmp(lapp_check(bad, hn, &h), "App file is damaged (checksum)"));
    memcpy(bad, hello, hn);                            assert(!strcmp(lapp_check(bad, hn - 3, &h), "App file is truncated"));
    assert(!strcmp(lapp_check(bad, 10, &h), "Not a LeactedOS app (file too small)"));
    memcpy(bad, hello, hn); bad[32] = 5; bad[33] = 0;  assert(!strcmp(lapp_check(bad, hn, &h), "App has a bad window size"));
    memcpy(bad, hello, hn); bad[8] = 99;               assert(!strcmp(lapp_check(bad, hn, &h), "App needs a newer LeactedOS"));
    memcpy(bad, hello, hn); bad[4] = 7;                assert(!strcmp(lapp_check(bad, hn, &h), "Unsupported app file version"));
    memcpy(bad, hello, hn); bad[12] = 0xFF; bad[13] = 0xFF; assert(!strcmp(lapp_check(bad, hn, &h), "App has a bad entry point"));
    memcpy(bad, hello, hn); bad[16] = 0xFF; bad[17] = 0xFF; assert(!strcmp(lapp_check(bad, hn, &h), "App is too big (64 KB limit)"));
    { /* a relocation that points outside the image is caught while loading (CRC fixed up) */
      memcpy(bad, hello, hn); struct lapp_header t; lapp_check(bad, hn, &t);
      u8 *last = bad + LAPP_HDR_SIZE + t.image_size + (t.nreloc - 1) * 4; last[0] = 0xF0; last[1] = 0xFF;
      u32 c = lapp_crc32(bad + LAPP_HDR_SIZE, t.image_size + t.nreloc * 4); memcpy(bad + 28, &c, 4);
      assert(!lapp_check(bad, hn, &t));
      static u8 mem[LAPP_MAX_MEM]; assert(!strcmp(lapp_load(bad, &t, mem, 0x100000), "App has a bad relocation")); }
    /* relocation arithmetic: every listed word moves by exactly the base */
    { struct lapp_header t; lapp_check(hello, hn, &t);
      static u8 m0[LAPP_MAX_MEM], m1[LAPP_MAX_MEM];
      assert(!lapp_load(hello, &t, m0, 0) && !lapp_load(hello, &t, m1, 0x00400000));
      const u8 *rel = hello + LAPP_HDR_SIZE + t.image_size; int diff = 0;
      for (u32 i = 0; i < t.image_size; i++) if (m0[i] != m1[i]) diff++;
      assert(diff > 0);
      for (u32 i = 0; i < t.nreloc; i++) { u32 o = lapp_rd32(rel + i * 4); assert(lapp_rd32(m1 + o) - lapp_rd32(m0 + o) == 0x00400000); } }

    /* apps from the terminal */
    int hi = fs_create("HELLO.LAPP"); assert(hi >= 0 && fs_store(hi, hello, hn) >= 0);
    int si = fs_create("SNAKE.LAPP"); assert(si >= 0 && fs_store(si, snake, sn) >= 0);
    int bi = fs_create("BAD.LAPP");   assert(bi >= 0 && fs_store(bi, "this is not an app, just text, long enough to pass the size check.....", 64) >= 0);
    tclear(); cmd("apps"); assert(term_has("HELLO.LAPP") && term_has("SNAKE.LAPP"));
    tclear(); cmd("appinfo hello"); assert(term_has("Hello") && term_has("340x190"));
    tclear(); cmd("appinfo bad"); assert(term_has("Not a LeactedOS app"));
    tclear(); cmd("run nosuch"); assert(term_has("Not found: nosuch"));
    tclear(); cmd("run bad"); assert(term_has("Cannot run: Not a LeactedOS app") && !wins[A_EXT0].open);
    tclear(); cmd("run hello");                                    /* loads and opens a window (not executed on the PC test build) */
    assert(wins[A_EXT0].open && xapp[0].used && !strcmp(wins[A_EXT0].title, "Hello") && wins[A_EXT0].w == 346 && wins[A_EXT0].h == 190 + 6 + TITLE_H);
    tclear(); cmd("apps"); assert(term_has("Running:") && term_has("Hello"));
    close_app(A_EXT0); assert(!xapp[0].used && !wins[A_EXT0].open);

    /* real execution of the same sources (built natively for this test) through the real dispatcher */
    tclear();
    assert(!lapp_start(hello, hn, "HELLO.LAPP", hello_main));
    assert(wins[A_EXT0].open && top_app() == A_EXT0 && term_has("hello.lapp started"));
    int ex = CX(A_EXT0), ey = CY(A_EXT0);
    frame_out("t30_app_hello.ppm");
    click(ex + 100, ey + 190 - 30 + 5);                            /* Files button: app asks the kernel for the file list and logs */
    assert(term_has("hello.lapp: listed files"));
    app_key(A_EXT0, 'q');
    /* drag the window by its title bar */
    { int ox0 = wins[A_EXT0].x, oy0 = wins[A_EXT0].y;
      on_press(ox0 + 100, oy0 + 8); on_move_held(ox0 + 160, oy0 + 48); on_release();
      assert(wins[A_EXT0].x == ox0 + 60 && wins[A_EXT0].y == oy0 + 40); }
    /* minimise via the title bar button; the app keeps getting ticks */
    { int bx, by, bw, bh; min_rect(A_EXT0, &bx, &by, &bw, &bh); click(bx + 3, by + 3); assert(wins[A_EXT0].min); toggle_app(A_EXT0); assert(!wins[A_EXT0].min); }
    /* the app closes itself through the API */
    ex = CX(A_EXT0); ey = CY(A_EXT0);
    click(ex + 200, ey + 190 - 30 + 5);
    frame_out("t31_after_self_close.ppm");
    assert(!wins[A_EXT0].open && !xapp[0].used && term_has("hello.lapp closed"));

    /* kernel API rules */
    { u32 before = back[5 * W + 5]; xapi.rect(0, 0, 640, 480, 0x123456); assert(back[5 * W + 5] == before); }   /* drawing outside EV_DRAW does nothing */
    assert(xapi.file_write("T1.BIN", "abc", 3) == 0 && xapi.file_write("T1.BIN", "abcd", 4) == 0);
    { char b[8] = {0}; assert(xapi.file_read("t1.bin", b, 8) == 4 && !strcmp(b, "abcd")); assert(xapi.file_read("t1.bin", b, 2) == 2); }
    assert(xapi.file_read("NOPE", 0, 0) == -1 && xapi.file_write("X", "y", 200000) == -1);
    { char nm[24]; int k, found = 0; for (k = 0; k < FS_MAX; k++) if (xapi.file_name(k, nm, sizeof nm) >= 0 && !strcmp(nm, "T1.BIN")) found = 1; assert(found); }
    assert(xapi.file_delete("T1.BIN") == 0 && xapi.file_delete("T1.BIN") == -1);
    assert(xapi.size == sizeof(struct leact_api) && xapi.version == LAPP_API_VERSION);
    { u32 a = xapi.rand(), b = xapi.rand(); assert(a != b); }

    /* four at once, the fifth is refused, a freed slot can be reused */
    for (int i = 0; i < 4; i++) assert(!lapp_start(hello, hn, "HELLO.LAPP", hello_main));
    assert(xapp[0].used && xapp[3].used);
    assert(!strcmp(lapp_start(hello, hn, "HELLO.LAPP", hello_main), "Too many apps running (4 at most)"));
    frame_out("t32_four_apps.ppm");
    { open_app(A_TASK); tsel = A_EXT1; int tw = CX(A_TASK), ty = CY(A_TASK);
      frame_out("t33_taskmgr_apps.ppm");
      click(tw + 6 + 10, ty + 166 + 5);                            /* End Task */
      assert(!xapp[1].used && !wins[A_EXT1].open && xapp[2].used);
      close_app(A_TASK); }
    assert(!lapp_start(snake, sn, "SNAKE.LAPP", snake_main) && xapp[1].used && !strcmp(wins[A_EXT1].title, "Snake"));
    for (int s = 0; s < N_EXT; s++) if (wins[A_EXT0 + s].open) close_app(A_EXT0 + s);
    for (int s = 0; s < N_EXT; s++) assert(!xapp[s].used);

    /* Snake: tick-driven, keyboard-driven */
    assert(!lapp_start(snake, sn, "SNAKE.LAPP", snake_main));
    for (int i = 0; i < 20; i++) { app_key(top_app(), K_DOWN); ticks += 3; os_draw(); }
    frame_out("t34_app_snake.ppm");
    for (int i = 0; i < 300; i++) { ticks += 3; os_draw(); }       /* runs into a wall */
    frame_out("t35_snake_over.ppm");
    close_app(A_EXT0);

    /* Doodle: mouse press, drag and release reach the app */
    assert(!lapp_start(doodle, dn, "DOODLE.LAPP", doodle_main));
    ex = CX(A_EXT0); ey = CY(A_EXT0);
    click(ex + 8 + 2 * 22 + 4, ey + 10);                           /* pick red */
    on_press(ex + 8 + 18, ey + 32 + 20); on_move_held(ex + 8 + 60, ey + 32 + 20); on_move_held(ex + 8 + 60, ey + 32 + 70); on_release();
    click(ex + 8 + 5 * 22 + 4, ey + 10);                           /* pick green */
    on_press(ex + 8 + 100, ey + 32 + 40); on_move_held(ex + 8 + 150, ey + 32 + 90); on_release();
    xapp_tick(); os_draw();
    assert(back[(ey + 32 + 20) * W + ex + 8 + 40] == 0xE03030);
    assert(back[(ey + 32 + 50) * W + ex + 8 + 60] == 0xE03030);
    assert(back[(ey + 32 + 65) * W + ex + 8 + 125] == 0x30B040);
    assert(back[(ey + 32 + 110) * W + ex + 8 + 200] == 0xFFFFFF);
    frame_out("t36_app_doodle.ppm");
    close_app(A_EXT0);

    /* File Manager -> Open With -> Run as app, from LeactedFS and from a FAT drive */
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/apps.img", &fakesect[3]); drive_probe(3);
    assert(drv[3].type == DT_FAT);
    tclear(); cmd("run F:/hello.lapp"); assert(xapp[0].used && !strcmp(wins[A_EXT0].title, "Hello")); close_app(A_EXT0);
    tclear(); cmd("appinfo F:/snake"); assert(term_has("Snake") && term_has("320x248"));
    tclear(); cmd("run F:/README.TXT"); assert(term_has("Cannot run: Not a LeactedOS app"));
    open_app(A_FILES); fm_pick_drive(0);
    assert(find_ent("HELLO.LAPP") >= 0); fm_sel = find_ent("BAD.LAPP"); fm_open_sel();
    assert(wins[A_OPENWITH].open && ow_rec == 3 && ow_sel == 3);
    ow_launch(); assert(wins[A_OPENWITH].open && !strcmp(ow_msg, "Not a LeactedOS app (bad signature)"));
    frame_out("t37_openwith_error.ppm");
    ow_key(K_UP); assert(ow_sel == 2); ow_key(K_DOWN); ow_key(K_DOWN); assert(ow_sel == 3);
    close_app(A_OPENWITH);
    fm_sel = find_ent("HELLO.LAPP"); fm_open_sel(); assert(ow_rec == 3);
    frame_out("t38_openwith_app.ppm");
    ow_launch(); assert(!wins[A_OPENWITH].open && xapp[0].used && wins[A_EXT0].open);
    close_app(A_EXT0);
    fm_pick_drive(3); assert(find_ent("snake.lapp") >= 0 && find_ent("README.TXT") >= 0);
    fm_sel = find_ent("snake.lapp"); fm_open_sel(); assert(ow_rec == 3 && ow_ref.kind == 2);
    ow_launch(); assert(xapp[0].used && !strcmp(wins[A_EXT0].title, "Snake"));
    close_app(A_EXT0);
    fm_sel = find_ent("README.TXT"); fm_open_sel(); assert(ow_rec == 0);
    ow_sel = 3; ow_launch(); assert(!strcmp(ow_msg, "Not a LeactedOS app (bad signature)")); close_app(A_OPENWITH);
    /* the same app window in the other themes */
    assert(!lapp_start(hello, hn, "HELLO.LAPP", hello_main));
    theme_apply(TH_UNITY); frame_out("t39_unity_app.ppm");
    theme_apply(TH_KDE); frame_out("t40_kde_app.ppm");
    theme_apply(TH_XP);
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    free(hello); free(snake); free(doodle); free(bad);
    }


    /* ---- Leact-get ---- */
    {
    is_root = 0; open_app(A_TERM); tclear();
    memset(fakedisk[0], 0, (size_t)20000 * 512); drive_probe(0); fs_check(); fs_load();
    /* no repository anywhere */
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/norepo.img", &fakesect[3]); drive_probe(3);
    tclear(); cmd("leact-get list"); assert(term_has("No repository found"));
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/badindex.img", &fakesect[3]); drive_probe(3);
    tclear(); cmd("leact-get list"); assert(term_has("missing LEACT-REPO"));
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/repo.img", &fakesect[3]); drive_probe(3);
    tclear(); cmd("leact-get"); assert(term_has("leact-get install NAME"));
    tclear(); cmd("leact-get repo"); assert(term_has("Repository: F:"));
    tclear(); cmd("leact-get list"); assert(term_has("5 app(s)") && term_has("Classic snake") && term_has("skipped 1 bad"));
    assert(!term_has("bad name"));
    tclear(); cmd("leact-get info snake"); assert(term_has("version 1.1") && term_has("Installed: no"));
    tclear(); cmd("leact-get info nosuch"); assert(term_has("No app called nosuch"));
    tclear(); cmd("leact-get install snake"); assert(term_has("Installed snake. Start it with: run snake"));
    assert(fs_find("snake.lapp") >= 0);
    tclear(); cmd("leact-get install SNAKE"); assert(term_has("already installed and up to date"));
    tclear(); cmd("leact-get hello"); assert(term_has("Installed hello"));
    tclear(); cmd("leact-get install broken"); assert(term_has("Could not install broken"));
    assert(fs_find("broken.lapp") < 0);
    tclear(); cmd("leact-get install ghost"); assert(term_has("Could not install ghost"));
    tclear(); cmd("leact-get install nosuch"); assert(term_has("No app called nosuch"));
    tclear(); cmd("leact-get list"); assert(term_has("* hello") && term_has("* snake") && !term_has("* doodle"));
    tclear(); cmd("run snake"); assert(xapp[0].used && !strcmp(wins[A_EXT0].title, "Snake")); close_app(A_EXT0);
    tclear(); cmd("apps"); assert(term_has("SNAKE.LAPP") && term_has("HELLO.LAPP"));
    /* update: a repository whose hello package is different must not clobber the installed one if it is invalid */
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/repo_bad.img", &fakesect[3]); drive_probe(3);
    tclear(); cmd("leact-get update"); assert(term_has("hello:") || term_has("up to date"));
    assert(fs_find("hello.lapp") >= 0);
    tclear(); cmd("run hello"); assert(xapp[0].used); close_app(A_EXT0);
    tclear(); cmd("leact-get update doodle"); assert(term_has("not installed"));
    /* remove */
    tclear(); cmd("leact-get remove snake"); assert(term_has("Removed snake.lapp") || term_has("Removed SNAKE.LAPP"));
    assert(fs_find("snake.lapp") < 0);
    tclear(); cmd("leact-get remove snake"); assert(term_has("snake is not installed"));
    /* choose a repository by path */
    tclear(); cmd("leact-get repo F:/pkg"); assert(term_has("No leact-repo.txt"));
    tclear(); cmd("leact-get repo F:/"); assert(term_has("Repository: F:"));
    tclear(); cmd("leact-get repo F:/hello.lapp"); assert(term_has("Not a folder") || term_has("Not found"));
    tclear(); cmd("leact-get list"); frame_out("t44_leactget.ppm");
    tclear(); cmd("leact-get install snake"); cmd("leact-get install hello"); frame_out("t45_leactget_install.ppm");
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    }

    /* ---- networking: terminal commands and the Browser, against the fake network ---- */
    {
    is_root = 0;
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    open_app(A_TERM); tclear();
    memset(fakedisk[0], 0, (size_t)20000 * 512); drive_probe(0); fs_check(); fs_load();
    tclear(); cmd("ifconfig"); assert(term_has("No network card"));
    tclear(); cmd("ping local.test"); assert(term_has("No network card"));
    tclear(); cmd("wget http://local.test/"); assert(term_has("-nic user,model=rtl8139"));
    open_app(A_BROWSER); br_go("local.test", 1); assert(!br_wait); assert(!strcmp(br_url, "local.test")); close_app(A_BROWSER);
    /* plug in the card */
    host_nic_on = 1; net_init(); assert(net_have_nic && dhcp_state == 1);
    net_steps(30); assert(dhcp_state == 3 && net_ip == cli_ip);
    open_app(A_TERM); tclear();
    cmd("ifconfig"); assert(term_has("10.0.2.15") && term_has("bound") && term_has("52:54:00:12:34:56") && term_has("10.0.2.2") && term_has("10.0.2.3"));
    tclear(); cmd("nslookup example.test"); assert(tjob); assert(net_idle(2000) >= 0); assert(term_has("93.184.216.34"));
    tclear(); cmd("nslookup nosuch.test"); net_idle(2000); assert(term_has("Host not found: nosuch.test"));
    tclear(); cmd("ping local.test"); assert(tjob == TJ_PING);
    assert(net_idle(3000) >= 0); assert(term_has("Pinging local.test [10.0.2.100]") && term_has("4 sent, 4 received"));
    tclear(); cmd("ping 10.0.2.200"); for (int i = 0; i < 3000 && tjob; i++) net_steps(1);
    assert(!tjob && term_has("Request timed out") && term_has("4 sent, 0 received"));
    tclear(); cmd("ping"); assert(term_has("Usage: ping")); assert(!tjob);
    tclear(); cmd("dhcp"); assert(tjob == TJ_DHCP); net_idle(2000); assert(term_has("Address 10.0.2.15"));
    /* typing while a job runs is ignored; Ctrl+C stops it */
    tclear(); cmd("ping 10.0.2.200"); typestr("abc"); assert(ilen == 0); app_key(A_TERM, 3); assert(!tjob && term_has("^C"));
    for (int i = 0; i < 200; i++) net_steps(1);
    tclear(); cmd("wget http://local.test/app.bin payload.bin"); assert(tjob == TJ_WGET);
    tclear(); { char *av[] = { "ping", "x" }; cmd_ping(av, 2); } assert(term_has("busy")); app_key(A_TERM, 3); assert(!tjob);
    tclear(); cmd("wget http://local.test/app.bin payload.bin"); assert(net_idle(3000) >= 0);
    assert(term_has("Saved PAYLOAD.BIN (3000 bytes)")); { int i = fs_find("payload.bin"); assert(i >= 0 && fsdir[i].size == 3000); for (int k = 0; k < 3000; k++) assert(fsdata[i][k] == pat(k)); }
    tclear(); cmd("wget http://local.test/final"); net_idle(3000); assert(term_has("Saved FINAL (18 bytes)"));
    tclear(); cmd("wget http://local.test/"); net_idle(3000); assert(term_has("Saved INDEX.HTML") || term_has("Saved index.html"));
    tclear(); cmd("wget http://local.test/big"); net_idle(5000); assert(term_has("Saved BIG (100000 bytes)"));
    tclear(); cmd("wget http://local.test/missing"); net_idle(3000); assert(term_has("answered 404"));
    tclear(); cmd("wget http://nosuch.test/x"); net_idle(5000); assert(term_has("Failed: Host not found"));
    http_proxy_on = 0; tclear(); cmd("wget https://local.test/x"); net_idle(100); assert(term_has("Failed: HTTPS")); http_proxy_on = 1;
    tclear(); cmd("wget"); assert(term_has("Usage: wget"));
    fs_check();

    /* ---- the Browser window ---- */
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    open_app(A_BROWSER); raise_app(A_BROWSER);
    assert(!strcmp(br_url, "about:home") && br_nrows > 8 && br_nlinks == 4 && br_focus);
    assert(!strcmp(br_title, "LeactedOS Browser") && !strcmp(wins[A_BROWSER].title, "LeactedOS Browser - Browser"));
    frame_out("t46_browser_home.ppm");
    /* type an address and press Enter */
    typestr_to(A_BROWSER, "local.test/page"); app_key(A_BROWSER, '\n'); assert(br_wait);
    assert(net_idle(3000) >= 0); assert(!br_wait && !strcmp(br_url, "http://local.test/page"));
    assert(!strcmp(br_title, "Test & Demo Page"));
    { /* the text really was laid out: find strings in the row buffer */
      #define BR_HAS(str) br_has(str)
      assert(br_has("Big Heading") && br_has("bold text, an absolute link") && br_has("First item") && br_has("Second <item> \"quoted\" AB"));
      assert(br_has("* First item") && br_has("1. One") && br_has("2. Two") && br_has("preformatted   text") && br_has("keeps    spaces"));
      assert(br_has("Name | Value") && br_has("alpha | 1") && br_has("[a picture]"));
      assert(!br_has("hidden") && !br_has("not shown") && !br_has("color: red") && !br_has("document.write"));
      assert(br_has("dash-end") && br_has("\"quotes\"") && br_has("cafe"));
    }
    assert(br_nlinks == 4);                                                  /* mailto and #top are not links */
    assert(!strcmp(br_links[1], "http://local.test/final") && !strcmp(br_links[2], "http://local.test/sub/rel.html") && !strcmp(br_links[3], "http://local.test/up.html"));
    assert(!strcmp(br_links[4], "http://example.test/final"));
    for (int r = 0; r < br_nrows; r++) assert(br_rows[r].len <= 71);          /* nothing wider than the window */
    frame_out("t47_browser_page.ppm");
    /* click the first link */
    { int lr = -1, lc = -1;
      for (int r = 0; r < br_nrows && lr < 0; r++) for (int i = 0; i < br_rows[r].len; i++) if (br_lk[br_rows[r].off + i] == 1) { lr = r; lc = i; break; }
      assert(lr >= 0); br_scroll = lr > 3 ? lr - 3 : 0;
      int y = BR_TB + 3; for (int r = br_scroll; r < lr; r++) y += br_rowh(r);
      app_click(A_BROWSER, 4 + br_rows[lr].indent * 8 + lc * 8 + 3, y + 3); }
    assert(br_wait); assert(net_idle(3000) >= 0); assert(!strcmp(br_url, "http://local.test/final") && br_has("final page") && br_nh == 2);
    /* back */
    app_key(A_BROWSER, '\b'); assert(br_wait); net_idle(3000); assert(!strcmp(br_url, "http://local.test/page") && br_nh == 1);
    app_key(A_BROWSER, '\b'); assert(!br_wait && !strcmp(br_url, "about:home") && br_nh == 0);     /* back to the start page */
    app_key(A_BROWSER, '\b'); assert(!br_wait && !strcmp(br_url, "about:home"));                     /* nothing further back */
    br_go("http://local.test/page", 1); net_idle(3000); assert(br_has("Big Heading") && br_nh == 1);
    /* plain text, a download, errors, searches, redirects */
    br_focus = 1; br_sel = 1; typestr_to(A_BROWSER, "http://local.test/plain"); app_key(A_BROWSER, '\n'); net_idle(3000);
    assert(br_has("line one") && br_has("    indented three") && !strcmp(wins[A_BROWSER].title, "Browser"));
    br_go("http://local.test/app.bin", 1); net_idle(3000); assert(br_has("not a web page") && br_has("Saved APP.BIN (3000 bytes)") && fs_find("app.bin") >= 0);
    br_go("http://nosuch.test/", 1); net_idle(6000); assert(br_has("Cannot open this page") && br_has("Host not found"));
    frame_out("t48_browser_error.ppm");
    http_proxy_on = 0; br_go("https://local.test/", 1); net_idle(100); assert(br_has("HTTPS")); http_proxy_on = 1;
    br_go("http://local.test/redir", 1); net_idle(3000); assert(!strcmp(br_url, "http://local.test/final"));
    { char u[330]; br_search_url(u, sizeof u, "hello world & more"); assert(!strcmp(u, "http://frogfind.com/?q=hello+world+%26+more")); }
    /* stop while loading */
    br_go("http://local.test/big", 1); net_steps(3); assert(br_wait); app_key(A_BROWSER, K_ESC); assert(!br_wait && !http_active()); assert(!strcmp(br_status, "Stopped"));
    net_steps(200);
    br_go("http://local.test/final", 1); net_idle(3000); assert(br_has("final page"));
    /* a very long page must not overflow the buffers */
    br_go("http://local.test/huge", 1); net_idle(20000); assert(br_nrows > 100 && br_nrows <= BR_ROWS && br_n < BR_TXT && br_has("[Page cut off"));
    /* scrolling */
    br_focus = 0; br_scroll = 0; app_key(A_BROWSER, K_DOWN); assert(br_scroll == 1); app_key(A_BROWSER, K_END); assert(br_scroll == br_maxscroll()); app_key(A_BROWSER, K_HOME); assert(br_scroll == 0);
    app_key(A_BROWSER, K_UP); assert(br_scroll == 0);
    app_key(A_BROWSER, K_PGDN); assert(br_scroll > 10); app_key(A_BROWSER, K_PGUP); assert(br_scroll == 0 || br_scroll < br_maxscroll()); app_key(A_BROWSER, K_HOME);
    frame_out("t49_browser_long.ppm");
    /* closing the window mid-load cancels the request */
    br_go("http://local.test/big", 1); net_steps(3); assert(br_wait); close_app(A_BROWSER); assert(!br_wait && !http_active()); net_steps(300);
    for (int i = 0; i < NT; i++) assert(!tcps[i].used);
    /* the browser and a terminal job must not fight over the network */
    open_app(A_BROWSER); open_app(A_TERM); tclear(); cmd("wget http://local.test/big"); br_go("local.test", 1); assert(!br_wait && term_has("Downloading"));
    app_key(A_TERM, 3); net_steps(100);
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    host_nic_on = 0; net_have_nic = 0; http_tcp_up = 0; dhcp_state = 0; net_ip = 0;
    }

    /* ---- Leact-get from an online repository (plain http, then https through the gateway) ---- */
    {
    host_nic_on = 1; net_init(); net_steps(30); assert(net_ip == cli_ip);
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/norepo.img", &fakesect[3]); drive_probe(3);   /* a disk without a repository */
    open_app(A_TERM); tclear(); is_root = 0;
    memset(fakedisk[0], 0, (size_t)20000 * 512); drive_probe(0); fs_check(); fs_load();
    int hn, sn, dn; u8 *hello = slurp("sdk/out/hello.lapp", &hn), *snake = slurp("sdk/out/snake.lapp", &sn), *doodle = slurp("sdk/out/doodle.lapp", &dn);
    const char *idx = "LEACT-REPO 1\r\n# online test\r\nhello|pkg/hello.lapp|1.0|Hello demo\r\nsnake|pkg/snake.lapp|1.1|Classic snake\r\n"
                      "doodle|pkg/doodle.lapp|1.0|Drawing pad\r\nbroken|pkg/broken.lapp|0.1|Corrupt\r\nghost|pkg/ghost.lapp|0.1|Missing\r\nbad name!|x|1|skipped\r\n";
    const char *roots[] = { "/repo/", "/avarabb990/leactedos-project/main/repo/" };       /* the same files for plain http and the github path */
    for (int r = 0; r < 2; r++) {
        char pth[200];
        snprintf(pth, sizeof pth, "%sleact-repo.txt", roots[r]); fake_add_file(pth, (const u8 *)idx, (int)strlen(idx));
        snprintf(pth, sizeof pth, "%spkg/hello.lapp", roots[r]); fake_add_file(pth, hello, hn);
        snprintf(pth, sizeof pth, "%spkg/snake.lapp", roots[r]); fake_add_file(pth, snake, sn);
        snprintf(pth, sizeof pth, "%spkg/doodle.lapp", roots[r]); fake_add_file(pth, doodle, dn);
        snprintf(pth, sizeof pth, "%spkg/broken.lapp", roots[r]); fake_add_file(pth, (const u8 *)"NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE-NOPE", 80);
    }
    /* with no local repository and a network card, the default is online (through the gateway: https) */
    cmd("leact-get repo auto"); tclear(); cmd("leact-get repo"); assert(term_has("Online repository"));
    http_proxy_on = 1; fake_gw_hits = 0;
    tclear(); cmd("leact-get list"); assert(tjob == TJ_LGET); assert(net_idle(5000) >= 0);
    assert(term_has("Online repository: 5 app(s)") && term_has("Classic snake") && term_has("skipped 1 bad") && fake_gw_hits == 1 && !strcmp(fake_gw_host, "raw.githubusercontent.com") && fake_gw_tls);
    tclear(); cmd("leact-get info snake"); net_idle(5000); assert(term_has("version 1.1") && term_has("Installed: no"));
    tclear(); cmd("leact-get info nosuch"); net_idle(5000); assert(term_has("No app called nosuch"));
    tclear(); cmd("leact-get install snake"); assert(tjob == TJ_LGET); net_idle(8000); assert(term_has("Installed snake. Start it with: run snake") && fs_find("snake.lapp") >= 0);
    tclear(); cmd("leact-get install SNAKE"); net_idle(8000); assert(term_has("already installed and up to date"));
    tclear(); cmd("leact-get hello"); net_idle(8000); assert(term_has("Installed hello") && fs_find("hello.lapp") >= 0);
    tclear(); cmd("leact-get install broken"); net_idle(8000); assert(term_has("Could not install broken") && fs_find("broken.lapp") < 0);
    tclear(); cmd("leact-get install ghost"); net_idle(8000); assert(term_has("Could not install ghost") && term_has("404"));
    tclear(); cmd("leact-get install nosuch"); net_idle(8000); assert(term_has("No app called nosuch"));
    tclear(); cmd("leact-get list"); net_idle(5000); assert(term_has("* hello") && term_has("* snake") && !term_has("* doodle"));
    tclear(); cmd("run snake"); assert(xapp[0].used); close_app(A_EXT0);
    /* update: publish a different (valid) build of hello and leave the others alone */
    { u8 *nh = malloc((size_t)hn); memcpy(nh, hello, (size_t)hn); struct lapp_header h; memcpy(&h, nh, sizeof h);
      nh[LAPP_HDR_SIZE + h.image_size - 1] ^= 0x5A; u32 c = lapp_crc32(nh + LAPP_HDR_SIZE, h.image_size + h.nreloc * 4u); memcpy(nh + 28, &c, 4);
      fake_add_file("/avarabb990/leactedos-project/main/repo/pkg/hello.lapp", nh, hn); free(nh); }
    u32 old_crc = lapp_rd32(fsdata[fs_find("hello.lapp")] + 28);
    tclear(); cmd("leact-get update"); assert(tjob == TJ_LGET); net_idle(15000); assert(term_has("Updated hello") && !term_has("Updated snake"));
    assert(lapp_rd32(fsdata[fs_find("hello.lapp")] + 28) != old_crc);
    tclear(); cmd("leact-get update"); net_idle(15000); assert(term_has("Everything is up to date"));
    tclear(); cmd("leact-get update doodle"); net_idle(5000); assert(term_has("not installed"));
    /* a repository chosen by URL (plain http, no gateway involved) */
    tclear(); cmd("leact-get repo http://local.test/repo"); assert(term_has("http://local.test/repo/"));
    fake_gw_hits = 0; tclear(); cmd("leact-get install doodle"); net_idle(8000); assert(term_has("Installed doodle") && fake_gw_hits == 0 && fs_find("doodle.lapp") >= 0);
    tclear(); cmd("leact-get remove doodle"); assert(term_has("Removed") && fs_find("doodle.lapp") < 0);
    /* failures: gateway off, gateway down, wrong address, cancel */
    tclear(); cmd("leact-get repo default"); http_proxy_on = 0; cmd("leact-get list"); net_idle(5000); assert(term_has("Cannot reach the repository") && term_has("gateway"));
    http_proxy_on = 1; fake_gw_down = 1; tclear(); cmd("leact-get list"); net_idle(20000); assert(term_has("Cannot reach the repository") && term_has("Gateway not reachable")); fake_gw_down = 0;
    tclear(); cmd("leact-get repo http://local.test/nothere/"); cmd("leact-get list"); net_idle(5000); assert(term_has("answered 404"));
    tclear(); cmd("leact-get repo http://local.test/repo/"); cmd("leact-get list"); net_steps(2); assert(tjob == TJ_LGET); app_key(A_TERM, 3); assert(!tjob && !http_active()); net_steps(50);
    for (int i = 0; i < NT; i++) assert(!tcps[i].used);
    /* the proxy command */
    tclear(); cmd("proxy"); assert(term_has("HTTPS gateway: on  10.0.2.2:8080"));
    tclear(); cmd("proxy off"); assert(term_has("gateway: off") && !http_proxy_on);
    tclear(); cmd("proxy 192.168.1.5:9000"); assert(term_has("on  192.168.1.5:9000") && http_proxy_port == 9000 && !strcmp(http_proxy_host, "192.168.1.5"));
    tclear(); cmd("proxy 10.0.2.2"); assert(term_has("10.0.2.2:8080")); tclear(); cmd("proxy x:99999"); assert(term_has("Bad port"));
    /* a local repository disk takes over again with "auto" */
    free(fakedisk[3]); fakedisk[3] = loadimg("tests/out/repo.img", &fakesect[3]); drive_probe(3);
    tclear(); cmd("leact-get repo auto"); cmd("leact-get list"); assert(term_has("Repository F:") && !tjob);
    free(hello); free(snake); free(doodle);
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    host_nic_on = 0; net_have_nic = 0; http_tcp_up = 0; dhcp_state = 0; net_ip = 0; http_proxy_on = 0;
    }
    /* the same screens in the other themes */
    theme_apply(TH_UNITY); open_app(A_FILES); fm_pick_drive(2); fm_sel = find_ent("pic24.bmp"); fm_open_sel(); frame_out("t27_unity_openwith.ppm");
    ow_launch(); frame_out("t28_unity_image.ppm");
    theme_apply(TH_KDE); frame_out("t29_kde_image.ppm");
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    theme_apply(TH_XP);
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
