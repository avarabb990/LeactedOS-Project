/* Unit tests for css.h: colours, lengths, selectors, specificity, @media, inheritance */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32;
#include "../css.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("  FAIL: %s (line %d)\n", #c, __LINE__); fails++; } } while (0)
static struct cel st[8];
static struct cstyle compute(int depth, const char *inl) {      /* style of st[depth], computing the ancestors first */
    struct cstyle out; struct cstyle par; int have = 0;
    for (int i = 0; i <= depth; i++) {
        css_compute(&out, have ? &par : NULL, st, i, i == depth ? inl : NULL, inl && i == depth ? (int)strlen(inl) : 0);
        st[i].st = out; par = out; have = 1;
    }
    return out;
}
static void el(int i, const char *tag, const char *id, const char *cls) { css_el_init(&st[i], tag, (int)strlen(tag), id, (int)strlen(id), cls, (int)strlen(cls)); }
int main(void) {
    i32 c;
    CHECK(css_color("#f00", 4, &c) && c == 0xFF0000);
    CHECK(css_color("#1E90ff", 7, &c) && c == 0x1E90FF);
    CHECK(css_color("rgb(10, 20,30)", 14, &c) && c == 0x0A141E);
    CHECK(css_color("rgba(255,0,0,0.5)", 17, &c) && c == 0xFF0000);
    CHECK(css_color("rgb(100%,0%,0%)", 15, &c) && c == 0xFF0000);
    CHECK(css_color("  Tomato ", 9, &c) && c == 0xFF6347);
    CHECK(css_color("transparent", 11, &c) && c == CSS_NONE);
    CHECK(!css_color("#12", 3, &c) && !css_color("nosuchcolour", 12, &c) && !css_color("#gg0000", 7, &c));
    CHECK(css_len("16px", 4) == 16 && css_len("2em", 3) == 32 && css_len("1.5em", 5) == 24 && css_len("0", 1) == 0 && css_len("12pt", 4) == 16 && css_len("50%", 3) == 8);
    CHECK(css_len("bogus", 5) == -1 && css_len("-8px", 4) == -8 && css_len("auto", 4) == 0);

    css_reset();
    const char *sheet =
        "/* comment */ body { color: #333; background: #eee url(x.png) no-repeat }\n"
        "h1 { color: navy; font-size: 2em; text-align: center }\n"
        ".note { color: red; background-color: yellow; margin-left: 24px }\n"
        "p.note { color: green }\n"
        "#main .note { color: purple !important; }\n"
        "div > span { font-weight: bold }\n"
        "div span.x { text-transform: uppercase }\n"
        "a:hover { color: pink }\n"
        "li[data-x] { color: pink }\n"
        "ul.menu li, ol li { list-style: none }\n"
        "@media print { p { display: none } }\n"
        "@media screen and (max-width: 600px) { .small { font-size: 10px } }\n"
        "@font-face { font-family: X; src: url(a) }\n"
        "@import url(foo.css);\n"
        ".after { color: blue }\n";
    css_add_sheet(sheet, (int)strlen(sheet));
    CHECK(css_nsel >= 10);

    el(0, "html", "", ""); el(1, "body", "", ""); el(2, "div", "main", "");
    struct cstyle s = compute(1, NULL);
    CHECK(s.fg == 0x333333 && s.bg == 0xEEEEEE);
    el(3, "p", "", "note");
    s = compute(3, NULL);
    CHECK(s.fg == 0x800080);                                   /* #main .note !important beats p.note */
    CHECK(s.bg == 0xFFFF00 && s.cind == 3);
    el(2, "div", "other", "");
    el(3, "p", "", "note");
    s = compute(3, NULL);
    CHECK(s.fg == 0x008000);                                   /* p.note (spec 11) beats .note (10) */
    el(3, "p", "", "");
    s = compute(3, NULL);
    CHECK(s.fg == 0x333333 && s.bg == CSS_NONE && !s.hidden);  /* inherited colour, background not inherited, @media print skipped */
    el(3, "h1", "", "");
    s = compute(3, NULL);
    CHECK(s.fg == 0x000080 && s.big == 1 && s.align == 2 && s.bold == 1);
    el(3, "span", "", "x"); s = compute(3, NULL);
    CHECK(s.bold == 1 && s.xform == 1);                        /* div > span, div span.x */
    el(3, "p", "", ""); el(4, "span", "", "x"); s = compute(4, NULL);
    CHECK(s.bold == 0 && s.xform == 1);                        /* span is not a child of div here, but still a descendant */
    el(3, "a", "", ""); s = compute(3, NULL);
    CHECK(s.fg == 0x0000DC);                                   /* links are blue by default; a:hover is skipped */
    el(3, "li", "", ""); s = compute(3, NULL); CHECK(!s.nolist);
    el(3, "ul", "", "menu"); el(4, "li", "", ""); s = compute(4, NULL); CHECK(s.nolist);
    el(3, "p", "", "small"); s = compute(3, NULL); CHECK(s.big == 0);
    /* inline style wins over the sheet, !important in the sheet beats plain inline */
    el(3, "p", "", "note"); el(2, "div", "other", "");
    s = compute(3, "color: #00ff00; display:none; font-size: 30px"); CHECK(s.fg == 0x00FF00 && s.hidden && s.big == 1);
    el(2, "div", "main", ""); s = compute(3, "color: #00ff00"); CHECK(s.fg == 0x800080);
    /* hidden is inherited by children */
    el(2, "div", "", "gone"); css_add_sheet(".gone{display:none}", 18);
    el(3, "p", "", ""); s = compute(3, NULL); CHECK(s.hidden);
    /* vertical margins and padding shorthand */
    el(2, "div", "", ""); s = compute(2, "margin: 20px 0"); CHECK(s.vtop && s.vbot && s.cind == 0);
    s = compute(2, "padding: 0 0 0 40px"); CHECK(!s.vtop && s.cind == 5);
    s = compute(2, "margin-top:4px;margin-bottom:1em"); CHECK(!s.vtop && s.vbot);
    s = compute(2, "text-decoration: none; display: inline"); CHECK(s.deco == 1 && s.disp == 2);
    s = compute(2, "font-weight: 700; text-align: right; white-space: pre"); CHECK(s.bold && s.align == 3 && s.pre);
    /* garbage must not crash */
    css_reset();
    const char *junk = "}}} {{ a { b:: ;; } @media { p { } @@@ ,,, .{ #{ [ ] h1 { color: } p { color:;; ; } ";
    css_add_sheet(junk, (int)strlen(junk));
    css_add_sheet("p{color:red", 11);
    css_add_sheet("/* unterminated", 15);
    for (int i = 0; i < 2000; i++) { char b[40]; sprintf(b, ".c%d{color:red}", i); css_add_sheet(b, (int)strlen(b)); }
    CHECK(css_nsel <= CSS_MAXRULES);
    printf(fails ? "css test: FAILED\n" : "css test: ok\n");
    return fails != 0;
}
