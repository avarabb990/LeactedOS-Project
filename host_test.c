/* Host-side test harness: runs the OS logic on a PC, renders frames to PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define HOST 1
#include "kernel.c"

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
    for (int r = 0; r < TR; r++) if (strstr(tg[r], needle)) return 1;
    return 0;
}
static int find_ent(const char *n) { for (int i = 0; i < fm_n; i++) if (!strcmp(fm_ents[i].name, n)) return i; return -1; }
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
    fakedisk[0] = calloc(4096, 512); fakesect[0] = 4096;               /* C: blank */
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
    fm_sel = find_ent("big.bin"); fm_import(); assert(fsdir[fs_find("BIG.BIN")].size == 4096);
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
    tclear(); cmd("run nosuch"); assert(term_has("App not found"));
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
    assert(xapi.file_read("NOPE", 0, 0) == -1 && xapi.file_write("X", "y", 5000) == -1);
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

    /* the same screens in the other themes */
    theme_apply(TH_UNITY); open_app(A_FILES); fm_pick_drive(2); fm_sel = find_ent("pic24.bmp"); fm_open_sel(); frame_out("t27_unity_openwith.ppm");
    ow_launch(); frame_out("t28_unity_image.ppm");
    theme_apply(TH_KDE); frame_out("t29_kde_image.ppm");
    for (int k = 0; k < N_APPS; k++) if (wins[k].open) close_app(k);
    theme_apply(TH_XP);
    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
