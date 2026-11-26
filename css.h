/* css.h - a small CSS engine for the LeactedOS Browser (included by kernel.c).
 *
 * Needs from the includer:  u8 u16 u32 i32, memcpy/memset.
 *
 * What it understands
 *   selectors   tag  .class  #id  tag.class  *  descendant ("a b") and child ("a > b") chains, comma lists
 *               (rules with :hover, [attr], + or ~ are skipped; :link/:visited/:root/:first-child are ok)
 *   at-rules    @media (everything except print) is applied, @import/@font-face/@keyframes are skipped
 *   properties  color, background / background-color, font-weight, font-size, text-align, display,
 *               visibility, text-transform, text-decoration, margin*, padding*, list-style, white-space
 *   values      #rgb #rrggbb rgb() rgba() and the usual colour names; px em rem % pt and size keywords
 * Cascade: !important, then specificity, then source order; inline style="" wins over sheets.
 */
#ifndef LEACT_CSS_H
#define LEACT_CSS_H

#define CSS_MAXRULES 480
#define CSS_DECLPOOL 49152
#define CSS_MAXDEPTH 48
#define CSS_NONE (-1)

struct cstyle {
    i32 fg;            /* 0xRRGGBB or CSS_NONE (inherited)                         */
    i32 bg;            /* background of this element or CSS_NONE                  */
    u8 bold, big;      /* font-weight >= 600; font-size >= 22px (double size)      */
    u8 align;          /* 0 left, 1 left(explicit), 2 centre, 3 right              */
    u8 xform;          /* text-transform: 0 none, 1 upper, 2 lower                 */
    u8 deco;           /* text-decoration: 0 default, 1 none, 2 underline          */
    u8 nolist;         /* list-style: none                                         */
    u8 hidden;         /* display:none here or in an ancestor                      */
    u8 dnone;          /* the hiding came from display:none (a later display value undoes it) */
    u8 disp;           /* 0 default, 1 block, 2 inline                             */
    u8 vtop, vbot;     /* a vertical margin/padding of about a line or more        */
    u8 pre;            /* white-space: pre / pre-wrap                              */
    u8 cind;           /* indentation in characters (inherited, accumulates)       */
};

struct csspart { char tag[10], id[16], cls[3][12]; u8 ncls, comb; };   /* comb: 1 = child of the next part */
struct csssel { struct csspart p[4]; u8 n; u16 spec, decl, order; };
struct cel { char tag[10], id[16], cls[4][12]; u8 ncls; struct cstyle st; short ei; u8 jslink; };   /* ei/jslink: used by the Browser */

static struct csssel css_sel[CSS_MAXRULES]; static int css_nsel;
static char css_pool[CSS_DECLPOOL]; static int css_plen;
static int css_order;

static void css_reset(void) { css_nsel = 0; css_plen = 0; css_order = 0; }

/* ------------------------------ values ------------------------------ */
static int css_isws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int css_lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int css_ieq(const char *a, int an, const char *b) {          /* a[0..an) equals b, ignoring case */
    int i = 0;
    for (; i < an && b[i]; i++) if (css_lc((unsigned char)a[i]) != css_lc((unsigned char)b[i])) return 0;
    return i == an && !b[i];
}
static int css_hex(int c) { c = css_lc(c); return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static const struct { const char *n; i32 v; } css_names[] = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF },
    { "yellow", 0xFFFF00 }, { "orange", 0xFFA500 }, { "purple", 0x800080 }, { "gray", 0x808080 }, { "grey", 0x808080 },
    { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "navy", 0x000080 }, { "teal", 0x008080 }, { "lime", 0x00FF00 },
    { "aqua", 0x00FFFF }, { "cyan", 0x00FFFF }, { "fuchsia", 0xFF00FF }, { "magenta", 0xFF00FF }, { "olive", 0x808000 },
    { "pink", 0xFFC0CB }, { "brown", 0xA52A2A }, { "gold", 0xFFD700 }, { "crimson", 0xDC143C }, { "indigo", 0x4B0082 },
    { "violet", 0xEE82EE }, { "tomato", 0xFF6347 }, { "coral", 0xFF7F50 }, { "salmon", 0xFA8072 }, { "khaki", 0xF0E68C },
    { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 }, { "darkgray", 0xA9A9A9 }, { "darkgrey", 0xA9A9A9 },
    { "lightblue", 0xADD8E6 }, { "darkblue", 0x00008B }, { "darkgreen", 0x006400 }, { "darkred", 0x8B0000 },
    { "lightgreen", 0x90EE90 }, { "skyblue", 0x87CEEB }, { "royalblue", 0x4169E1 }, { "steelblue", 0x4682B4 },
    { "dodgerblue", 0x1E90FF }, { "firebrick", 0xB22222 }, { "forestgreen", 0x228B22 }, { "orangered", 0xFF4500 },
    { "chocolate", 0xD2691E }, { "tan", 0xD2B48C }, { "beige", 0xF5F5DC }, { "ivory", 0xFFFFF0 }, { "lavender", 0xE6E6FA },
    { "whitesmoke", 0xF5F5F5 }, { "gainsboro", 0xDCDCDC }, { "slategray", 0x708090 }, { "slategrey", 0x708090 },
    { "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "turquoise", 0x40E0D0 }, { "plum", 0xDDA0DD }, { "orchid", 0xDA70D6 },
    { "midnightblue", 0x191970 }, { "seagreen", 0x2E8B57 }, { "limegreen", 0x32CD32 }, { "hotpink", 0xFF69B4 },
    { "deeppink", 0xFF1493 }, { "goldenrod", 0xDAA520 }, { "wheat", 0xF5DEB3 }, { "aliceblue", 0xF0F8FF }, { "azure", 0xF0FFFF },
    { "snow", 0xFFFAFA }, { "linen", 0xFAF0E6 }, { "honeydew", 0xF0FFF0 }, { "mintcream", 0xF5FFFA }, { "cornsilk", 0xFFF8DC },
};
/* a colour at s[0..n): returns 1 and sets *out, or 0 (also for "transparent", which sets *out = CSS_NONE and returns 1) */
static int css_color(const char *s, int n, i32 *out) {
    while (n > 0 && css_isws(s[0])) { s++; n--; }
    while (n > 0 && css_isws(s[n - 1])) n--;
    if (n <= 0) return 0;
    if (s[0] == '#') {
        int d[8], k = 0;
        for (int i = 1; i < n && k < 8; i++) { int h = css_hex((unsigned char)s[i]); if (h < 0) return 0; d[k++] = h; }
        if (k == 3 || k == 4) { *out = (d[0] * 17) << 16 | (d[1] * 17) << 8 | (d[2] * 17); return 1; }
        if (k == 6 || k == 8) { *out = (d[0] * 16 + d[1]) << 16 | (d[2] * 16 + d[3]) << 8 | (d[4] * 16 + d[5]); return 1; }
        return 0;
    }
    if (n > 4 && css_lc((unsigned char)s[0]) == 'r' && css_lc((unsigned char)s[1]) == 'g' && css_lc((unsigned char)s[2]) == 'b') {
        int i = 3, v[3], k = 0, alpha0 = 0;
        if (css_lc((unsigned char)s[i]) == 'a') i++;
        if (i >= n || s[i] != '(') return 0;
        i++;
        while (i < n && k < 4) {
            while (i < n && (css_isws(s[i]) || s[i] == ',' || s[i] == '/')) i++;
            if (i >= n || s[i] == ')') break;
            int val = 0, frac = 0, any = 0, scale = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') { val = val * 10 + (s[i] - '0'); i++; any = 1; }
            if (i < n && s[i] == '.') { i++; if (i < n && s[i] >= '0' && s[i] <= '9') frac = s[i] - '0'; while (i < n && s[i] >= '0' && s[i] <= '9') i++; any = 1; }
            if (i < n && s[i] == '%') { scale = 1; i++; }
            if (!any) return 0;
            if (k < 3) { if (scale) val = val * 255 / 100; if (val > 255) val = 255; v[k] = val; }
            else if (val == 0 && frac == 0) alpha0 = 1;
            k++;
        }
        if (k < 3) return 0;
        *out = alpha0 ? CSS_NONE : (v[0] << 16 | v[1] << 8 | v[2]);
        return 1;
    }
    if (css_ieq(s, n, "transparent")) { *out = CSS_NONE; return 1; }
    for (unsigned k = 0; k < sizeof css_names / sizeof css_names[0]; k++)
        if (css_ieq(s, n, css_names[k].n)) { *out = css_names[k].v; return 1; }
    return 0;
}
/* a length like "12px", "1.5em", "50%": returns pixels (16 px = 1 em) or -1 if it is not a length; "0" is 0, "auto" is 0 */
static int css_len(const char *s, int n) {
    while (n > 0 && css_isws(s[0])) { s++; n--; }
    while (n > 0 && css_isws(s[n - 1])) n--;
    if (n <= 0) return -1;
    if (css_ieq(s, n, "auto") || css_ieq(s, n, "inherit") || css_ieq(s, n, "initial")) return 0;
    int i = 0, neg = 0, ip = 0, fp = 0, fd = 1, any = 0;
    if (s[i] == '-') { neg = 1; i++; } else if (s[i] == '+') i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') { if (ip < 100000) ip = ip * 10 + (s[i] - '0'); i++; any = 1; }
    if (i < n && s[i] == '.') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') { if (fd < 1000) { fp = fp * 10 + (s[i] - '0'); fd *= 10; } i++; any = 1; } }
    if (!any) return -1;
    int mul100 = ip * 100 + fp * 100 / fd, px;                      /* value * 100 */
    if (i == n) px = mul100 / 100;                                   /* unitless (only sensible for 0) */
    else if (css_ieq(s + i, n - i, "px")) px = mul100 / 100;
    else if (css_ieq(s + i, n - i, "em") || css_ieq(s + i, n - i, "rem")) px = mul100 * 16 / 100;
    else if (css_ieq(s + i, n - i, "pt")) px = mul100 * 4 / 3 / 100;
    else if (css_ieq(s + i, n - i, "%")) px = mul100 * 16 / 10000;
    else if (css_ieq(s + i, n - i, "ex") || css_ieq(s + i, n - i, "ch")) px = mul100 * 8 / 100;
    else return -1;
    return neg ? -px : px;
}

/* ---------------------------- declarations --------------------------- */
static void css_set_margin(struct cstyle *s, const char *v, int n, int what) {   /* what: 0 margin, 1 padding */
    (void)what;
    int tok[4][2], nt = 0, i = 0;
    while (i < n && nt < 4) {
        while (i < n && css_isws(v[i])) i++;
        if (i >= n) break;
        int st = i; while (i < n && !css_isws(v[i])) i++;
        tok[nt][0] = st; tok[nt][1] = i - st; nt++;
    }
    if (!nt) return;
    int top = css_len(v + tok[0][0], tok[0][1]);
    int right = nt > 1 ? css_len(v + tok[1][0], tok[1][1]) : top;
    int bottom = nt > 2 ? css_len(v + tok[2][0], tok[2][1]) : top;
    int left = nt > 3 ? css_len(v + tok[3][0], tok[3][1]) : right;
    if (top >= 12) s->vtop = 1;
    if (bottom >= 12) s->vbot = 1;
    if (left > 0) { int c = s->cind + left / 8; s->cind = (u8)(c > 24 ? 24 : c); }
}
static void css_set_one(struct cstyle *s, const char *p, int pn, const char *v, int vn) {
    while (vn > 0 && css_isws(v[0])) { v++; vn--; }
    while (vn > 0 && css_isws(v[vn - 1])) vn--;
    if (vn <= 0) return;
    i32 c;
    if (css_ieq(p, pn, "color")) { if (css_color(v, vn, &c) && c != CSS_NONE) s->fg = c; }
    else if (css_ieq(p, pn, "background-color")) { if (css_color(v, vn, &c)) s->bg = c; }
    else if (css_ieq(p, pn, "background")) {
        int i = 0;                                                  /* find a colour word anywhere in the shorthand */
        while (i < vn) {
            while (i < vn && css_isws(v[i])) i++;
            int st = i; int depth = 0;
            while (i < vn && (depth || !css_isws(v[i]))) { if (v[i] == '(') depth++; else if (v[i] == ')') depth--; i++; }
            if (i > st && css_color(v + st, i - st, &c)) { s->bg = c; break; }
        }
    }
    else if (css_ieq(p, pn, "font-weight")) {
        if (css_ieq(v, vn, "bold") || css_ieq(v, vn, "bolder")) s->bold = 1;
        else if (css_ieq(v, vn, "normal") || css_ieq(v, vn, "lighter")) s->bold = 0;
        else if (v[0] >= '0' && v[0] <= '9') s->bold = (u8)(css_len(v, vn) >= 600);
    }
    else if (css_ieq(p, pn, "font-size")) {
        int px;
        if (css_ieq(v, vn, "xx-small")) px = 9; else if (css_ieq(v, vn, "x-small")) px = 10; else if (css_ieq(v, vn, "small") || css_ieq(v, vn, "smaller")) px = 13;
        else if (css_ieq(v, vn, "medium")) px = 16; else if (css_ieq(v, vn, "large") || css_ieq(v, vn, "larger")) px = 18;
        else if (css_ieq(v, vn, "x-large")) px = 24; else if (css_ieq(v, vn, "xx-large")) px = 32; else px = css_len(v, vn);
        if (px > 0) s->big = (u8)(px >= 22);
    }
    else if (css_ieq(p, pn, "text-align")) {
        if (css_ieq(v, vn, "center")) s->align = 2; else if (css_ieq(v, vn, "right") || css_ieq(v, vn, "end")) s->align = 3;
        else if (css_ieq(v, vn, "left") || css_ieq(v, vn, "start") || css_ieq(v, vn, "justify")) s->align = 1;
    }
    else if (css_ieq(p, pn, "display")) {
        if (css_ieq(v, vn, "none")) { s->hidden = 1; s->dnone = 1; }
        else if (css_ieq(v, vn, "block") || css_ieq(v, vn, "flex") || css_ieq(v, vn, "grid") || css_ieq(v, vn, "list-item") || css_ieq(v, vn, "table")) s->disp = 1;
        else if (css_ieq(v, vn, "inline") || css_ieq(v, vn, "inline-block") || css_ieq(v, vn, "inline-flex")) s->disp = 2;
        if (s->dnone && !css_ieq(v, vn, "none") && (css_ieq(v, vn, "block") || css_ieq(v, vn, "flex") || css_ieq(v, vn, "grid") || css_ieq(v, vn, "list-item") || css_ieq(v, vn, "table") ||
            css_ieq(v, vn, "inline") || css_ieq(v, vn, "inline-block") || css_ieq(v, vn, "inline-flex"))) { s->hidden = 0; s->dnone = 0; }
    }
    else if (css_ieq(p, pn, "visibility")) { if (css_ieq(v, vn, "hidden") || css_ieq(v, vn, "collapse")) s->hidden = 1; }
    else if (css_ieq(p, pn, "text-transform")) {
        if (css_ieq(v, vn, "uppercase")) s->xform = 1; else if (css_ieq(v, vn, "lowercase")) s->xform = 2; else if (css_ieq(v, vn, "none")) s->xform = 0;
    }
    else if (css_ieq(p, pn, "text-decoration") || css_ieq(p, pn, "text-decoration-line")) {
        int has_u = 0, has_n = 0;
        for (int i = 0; i + 4 < vn + 1; i++) { if (i + 9 <= vn && css_ieq(v + i, 9, "underline")) has_u = 1; if (i + 4 <= vn && css_ieq(v + i, 4, "none")) has_n = 1; }
        if (has_n) s->deco = 1; else if (has_u) s->deco = 2;
    }
    else if (css_ieq(p, pn, "list-style") || css_ieq(p, pn, "list-style-type")) { if (css_ieq(v, vn, "none")) s->nolist = 1; else if (v[0] != 'n') s->nolist = 0; }
    else if (css_ieq(p, pn, "white-space")) s->pre = (u8)(css_ieq(v, vn, "pre") || css_ieq(v, vn, "pre-wrap") || css_ieq(v, vn, "pre-line"));
    else if (css_ieq(p, pn, "margin") || css_ieq(p, pn, "padding")) css_set_margin(s, v, vn, css_lc((unsigned char)p[0]) == 'p');
    else if (css_ieq(p, pn, "margin-top") || css_ieq(p, pn, "padding-top")) { if (css_len(v, vn) >= 12) s->vtop = 1; }
    else if (css_ieq(p, pn, "margin-bottom") || css_ieq(p, pn, "padding-bottom")) { if (css_len(v, vn) >= 12) s->vbot = 1; }
    else if (css_ieq(p, pn, "margin-left") || css_ieq(p, pn, "padding-left") || css_ieq(p, pn, "text-indent")) {
        int l = css_len(v, vn);
        if (l > 0) { int c = s->cind + l / 8; s->cind = (u8)(c > 24 ? 24 : c); }
    }
}
/* apply "prop: value; prop: value" (the text of a declaration block). only_important: 0 all, 1 only !important, 2 only normal ones */
static void css_decls(struct cstyle *s, const char *d, int n, int which) {
    int i = 0;
    while (i < n) {
        while (i < n && (css_isws(d[i]) || d[i] == ';')) i++;
        int ps = i; while (i < n && d[i] != ':' && d[i] != ';') i++;
        if (i >= n || d[i] != ':') continue;
        int pe = i; while (pe > ps && css_isws(d[pe - 1])) pe--;
        i++;
        int vs = i, depth = 0;
        while (i < n && (depth || d[i] != ';')) { if (d[i] == '(') depth++; else if (d[i] == ')' && depth) depth--; i++; }
        int ve = i, imp = 0;
        while (ve > vs && css_isws(d[ve - 1])) ve--;
        if (ve - vs >= 10 && css_ieq(d + ve - 10, 10, "!important")) { imp = 1; ve -= 10; }
        if (which == 1 && !imp) continue;
        if (which == 2 && imp) continue;
        if (pe > ps) css_set_one(s, d + ps, pe - ps, d + vs, ve - vs);
    }
}

/* ----------------------------- style sheets -------------------------- */
static int css_ident_char(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 0x80; }
/* parse one selector (no commas) into *out; returns 0 if it uses something we do not support */
static int css_parse_selector(const char *s, int n, struct csssel *out) {
    memset(out, 0, sizeof *out);
    int i = 0, np = 0, pending_child = 0;
    int ids = 0, classes = 0, tags = 0;
    while (i < n) {
        while (i < n && css_isws(s[i])) i++;
        if (i >= n) break;
        if (s[i] == '>') { pending_child = 1; i++; continue; }
        if (s[i] == '+' || s[i] == '~' || s[i] == '[') return 0;
        if (np >= 4) return 0;
        struct csspart *p = &out->p[np];
        int any = 0;
        if (pending_child && np > 0) out->p[np - 1].comb = 1;
        pending_child = 0;
        while (i < n && !css_isws(s[i]) && s[i] != '>') {
            if (s[i] == '*') { i++; any = 1; }
            else if (s[i] == '#') {
                i++; int l = 0; while (i < n && css_ident_char((unsigned char)s[i])) { if (l < 15) p->id[l++] = (char)css_lc((unsigned char)s[i]); i++; }
                if (!l) return 0;
                ids++; any = 1;
            } else if (s[i] == '.') {
                i++; int l = 0; char cn[12];
                while (i < n && css_ident_char((unsigned char)s[i])) { if (l < 11) cn[l++] = (char)css_lc((unsigned char)s[i]); i++; }
                if (!l) return 0;
                cn[l] = 0;
                if (p->ncls >= 3) return 0;
                memcpy(p->cls[p->ncls++], cn, (size_t)l + 1); classes++; any = 1;
            } else if (s[i] == ':') {
                int st = ++i; if (i < n && s[i] == ':') return 0;
                while (i < n && css_ident_char((unsigned char)s[i])) i++;
                int l = i - st;
                if (i < n && s[i] == '(') return 0;
                if (!(css_ieq(s + st, l, "link") || css_ieq(s + st, l, "visited") || css_ieq(s + st, l, "root") || css_ieq(s + st, l, "first-child"))) return 0;
                if (css_ieq(s + st, l, "root")) { memcpy(p->tag, "html", 5); tags++; }
                classes++; any = 1;
            } else if (css_ident_char((unsigned char)s[i])) {
                int l = 0; while (i < n && css_ident_char((unsigned char)s[i])) { if (l < 9) p->tag[l++] = (char)css_lc((unsigned char)s[i]); i++; }
                p->tag[l] = 0; tags++; any = 1;
            } else return 0;
        }
        if (!any) return 0;
        np++;
    }
    if (!np) return 0;
    out->n = (u8)np;
    out->spec = (u16)(ids * 100 + classes * 10 + tags);
    return 1;
}
static void css_add_rule(const char *sel, int sn, const char *decl, int dn, int media_depth) {
    (void)media_depth;
    if (css_plen + dn + 1 > CSS_DECLPOOL) return;
    int doff = css_plen; memcpy(css_pool + css_plen, decl, (size_t)dn); css_plen += dn; css_pool[css_plen++] = 0;
    int i = 0, added = 0;
    while (i <= sn) {
        int st = i; while (i < sn && sel[i] != ',') i++;
        if (css_nsel < CSS_MAXRULES && i > st) {
            struct csssel *r = &css_sel[css_nsel];
            if (css_parse_selector(sel + st, i - st, r)) { r->decl = (u16)doff; r->order = (u16)css_order++; css_nsel++; added = 1; }
        }
        i++;
    }
    (void)added;
}
/* add a style sheet (text of a <style> element or a .css file) */
static void css_add_sheet(const char *s, int n) {
    int i = 0, media = 0;
    int stack_skip[8], sp = 0;               /* sp > 0: inside an @media block we apply (value 1) */
    (void)stack_skip;
    while (i < n) {
        while (i < n && (css_isws(s[i]) || (s[i] == '<' && i + 3 < n && s[i + 1] == '!') )) { if (s[i] == '<') i += 4; else i++; }
        if (i >= n) break;
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i += 2; continue; }
        if (s[i] == '-' && i + 2 < n && s[i + 1] == '-' && s[i + 2] == '>') { i += 3; continue; }
        if (s[i] == '}') { if (media > 0) media--; i++; continue; }
        if (s[i] == '@') {
            int st = i; while (i < n && s[i] != '{' && s[i] != ';') i++;
            if (i >= n) break;
            if (s[i] == ';') { i++; continue; }
            int is_media = css_ieq(s + st, 6 < i - st ? 6 : i - st, "@media") && i - st >= 6;
            int print_only = 0;
            if (is_media) { for (int k = st; k + 5 <= i; k++) if (css_ieq(s + k, 5, "print")) print_only = 1; }
            if (is_media && !print_only) { media++; i++; continue; }
            int depth = 0;                                      /* skip a whole block */
            while (i < n) { if (s[i] == '{') depth++; else if (s[i] == '}') { depth--; if (depth <= 0) { i++; break; } } i++; }
            continue;
        }
        int ss = i; while (i < n && s[i] != '{' && s[i] != '}') i++;
        if (i >= n || s[i] == '}') continue;
        int se = i; i++;
        int ds = i; while (i < n && s[i] != '}') { if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { i += 2; while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++; i += 1; } i++; }
        int de = i; if (i < n) i++;
        while (se > ss && css_isws(s[se - 1])) se--;
        css_add_rule(s + ss, se - ss, s + ds, de - ds, media);
    }
    (void)sp;
}

/* -------------------------------- matching --------------------------- */
static int css_part_match(const struct csspart *p, const struct cel *e) {
    if (p->tag[0] && memcmp(p->tag, e->tag, 10) != 0) {
        int k = 0; while (p->tag[k] && p->tag[k] == e->tag[k]) k++;
        if (p->tag[k] || e->tag[k]) return 0;
    }
    if (p->id[0]) { int k = 0; while (p->id[k] && p->id[k] == e->id[k]) k++; if (p->id[k] || e->id[k]) return 0; }
    for (int c = 0; c < p->ncls; c++) {
        int found = 0;
        for (int d = 0; d < e->ncls && !found; d++) { int k = 0; while (p->cls[c][k] && p->cls[c][k] == e->cls[d][k]) k++; if (!p->cls[c][k] && !e->cls[d][k]) found = 1; }
        if (!found) return 0;
    }
    return 1;
}
/* does selector r match the element at stack[depth] (stack[0..depth] are the open elements, last = the element itself)? */
static int css_sel_match(const struct csssel *r, const struct cel *stack, int depth) {
    int pi = r->n - 1, ei = depth;
    if (!css_part_match(&r->p[pi], &stack[ei])) return 0;
    while (pi > 0) {
        int child = r->p[pi - 1].comb;
        pi--; ei--;
        if (child) { if (ei < 0 || !css_part_match(&r->p[pi], &stack[ei])) return 0; }
        else { while (ei >= 0 && !css_part_match(&r->p[pi], &stack[ei])) ei--; if (ei < 0) return 0; }
    }
    return 1;
}
static void css_ua_defaults(struct cstyle *s, const char *tag) {
    if (tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2]) { s->bold = 1; s->big = (u8)(tag[1] == '1'); }
    else if (!tag[1] && (tag[0] == 'b')) s->bold = 1;
    else if (!tag[1] && tag[0] == 'a') s->fg = 0x0000DC;
    else if (!tag[1] && tag[0] == 'u') s->deco = 2;
    else if (!tag[2] && tag[0] == 't' && tag[1] == 'h') s->bold = 1;
    else if (!tag[2] && tag[0] == 'e' && tag[1] == 'm') ;
    else if (!tag[6] && tag[0] == 's' && tag[1] == 't' && tag[2] == 'r' && tag[3] == 'o') s->bold = 1;
    else if (!tag[6] && tag[0] == 'c' && tag[1] == 'e' && tag[2] == 'n') s->align = 2;
}
/* Compute the style of stack[depth]. parent may be NULL. inl is the style="" text (may be NULL). */
static void css_compute(struct cstyle *out, const struct cstyle *parent, const struct cel *stack, int depth, const char *inl, int inl_len) {
    struct cstyle s;
    if (parent) {
        s = *parent; s.bg = CSS_NONE; s.vtop = s.vbot = 0; s.disp = 0;
    } else { memset(&s, 0, sizeof s); s.fg = CSS_NONE; s.bg = CSS_NONE; }
    css_ua_defaults(&s, stack[depth].tag);
    int m[40], nm = 0;                                              /* indexes of matching rules, sorted by (spec, order) */
    for (int r = 0; r < css_nsel; r++) {
        if (!css_sel_match(&css_sel[r], stack, depth)) continue;
        if (nm >= 40) break;
        int k = nm++;
        while (k > 0 && (css_sel[m[k - 1]].spec > css_sel[r].spec || (css_sel[m[k - 1]].spec == css_sel[r].spec && css_sel[m[k - 1]].order > css_sel[r].order))) { m[k] = m[k - 1]; k--; }
        m[k] = r;
    }
    int ind_before = s.cind;
    for (int k = 0; k < nm; k++) { const char *d = css_pool + css_sel[m[k]].decl; int l = 0; while (d[l]) l++; css_decls(&s, d, l, 2); }
    if (inl && inl_len > 0) css_decls(&s, inl, inl_len, 2);
    for (int k = 0; k < nm; k++) { const char *d = css_pool + css_sel[m[k]].decl; int l = 0; while (d[l]) l++; css_decls(&s, d, l, 1); }
    if (inl && inl_len > 0) css_decls(&s, inl, inl_len, 1);
    (void)ind_before;
    if (parent && parent->hidden) s.hidden = 1;
    *out = s;
}
static void css_el_init(struct cel *e, const char *tag, int tl, const char *id, int il, const char *cls, int cl) {
    memset(e, 0, sizeof *e);
    for (int i = 0; i < tl && i < 9; i++) e->tag[i] = (char)css_lc((unsigned char)tag[i]);
    for (int i = 0; i < il && i < 15; i++) e->id[i] = (char)css_lc((unsigned char)id[i]);
    int i = 0;
    while (i < cl && e->ncls < 4) {
        while (i < cl && css_isws(cls[i])) i++;
        int st = i; while (i < cl && !css_isws(cls[i])) i++;
        int l = i - st; if (l <= 0) break;
        for (int k = 0; k < l && k < 11; k++) e->cls[e->ncls][k] = (char)css_lc((unsigned char)cls[st + k]);
        e->ncls++;
    }
}
#endif
