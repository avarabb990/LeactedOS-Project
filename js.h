/* js.h - a small JavaScript interpreter for the LeactedOS Browser (included by kernel.c).
 *
 * Needs from the includer:  u8 u16 u32 i32, memcpy/memset/memmove/memcmp, strlen-like helpers are defined here.
 * No floating point and no libgcc: numbers are fixed point (32.32), so integers up to +-2 billion are exact and
 * decimals have about 9 digits.  Division by zero gives Infinity, bad numbers give NaN.
 *
 * Language: var/let/const, function declarations and expressions, arrow functions, closures, if/else, for,
 *   for-in, for-of, while, do-while, switch, break/continue/return, try/catch/finally/throw, new (on plain functions),
 *   template literals, object and array literals, the usual operators, typeof, ++/--, compound assignment.
 * Library: Math, String and Array methods, Object.keys/values, JSON.stringify, parseInt/parseFloat/isNaN, Number/String(),
 *   console.log, alert, setTimeout/setInterval/clearTimeout/clearInterval, Date.now.
 * Not supported: classes, regular expressions, generators/async, getters/setters, labels, Date objects, Symbol, Map/Set.
 * The browser connects the page through struct jshost (document, elements, window, location).
 */
#ifndef LEACT_JS_H
#define LEACT_JS_H

typedef long long js_s64;
typedef unsigned long long js_u64;
#define JS_ONE ((js_s64)1 << 32)
#define JS_NAN ((js_s64)(-0x7FFFFFFFFFFFFFFFLL - 1))
#define JS_INF ((js_s64)0x7FFFFFFFFFFFFFFFLL)

#define JS_HEAP 524288
#define JS_NODES 12000
#define JS_VPOOL 24576
#define JS_PROPS 12288
#define JS_OBJS 3072
#define JS_ARRS 2048
#define JS_FUNCS 2048
#define JS_SCOPES 6144
#define JS_LVARS 12288
#define JS_GVARS 2048
#define JS_CONSTS 2048
#define JS_ITAB 8192
#define JS_MAXSTEPS 8000000
#define JS_MAXDEPTH 40
#define JS_MAXTIMERS 16

enum { JT_UNDEF, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_ARR, JT_OBJ, JT_FUN, JT_NAT, JT_NS, JT_ELEM, JT_DOC, JT_STYLE, JT_WIN, JT_LOC };
struct jv { u8 t; js_s64 n; };

/* the page side: the browser fills this in */
struct jshost {
    int (*get)(struct jv obj, const char *prop, struct jv *out);                                  /* element/document/window property; 0 = unknown */
    int (*set)(struct jv obj, const char *prop, struct jv v);
    int (*call)(struct jv obj, const char *method, struct jv *args, int nargs, struct jv *out);   /* 1 if handled */
    void (*console)(const char *s);
    void (*alert)(const char *s);
    u32 (*now_ms)(void);
    u32 (*rnd)(void);
    int (*date)(int *f);                                                                         /* fills year, month(1-12), day, hour, minute, second, weekday(0=Sun) */
};
static struct jshost *js_host;

/* ------------------------------ memory ------------------------------ */
static char js_heap[JS_HEAP]; static u32 js_htop;
static struct jv js_vpool[JS_VPOOL]; static u32 js_vtop;
struct jprop { u32 key; struct jv v; u32 next; };
static struct jprop js_props[JS_PROPS]; static u32 js_ptop;
struct jobj { u32 first, last; };
static struct jobj js_objs[JS_OBJS]; static u32 js_otop;
struct jarr { u32 len, cap, data; };
static struct jarr js_arrs[JS_ARRS]; static u32 js_atop;
struct jfunc { u32 node, env; struct jv this_v; u8 arrow; };
static struct jfunc js_funcs[JS_FUNCS]; static u32 js_ftop;
struct jvar { u32 name; struct jv v; u32 next; };
struct jscope { u32 parent, first; };
static struct jscope js_scopes[JS_SCOPES]; static u32 js_stop;
static struct jvar js_lvars[JS_LVARS]; static u32 js_lvtop;
static struct jvar js_gvars[JS_GVARS]; static u32 js_gvtop;
static u32 js_itab[JS_ITAB];
static js_s64 js_consts[JS_CONSTS]; static u32 js_ctop;

/* ------------------------------ errors ------------------------------ */
static char js_errmsg[160];
static int js_fatal;                 /* parse error, out of memory, too slow: not catchable */
static int js_thrown;                /* an exception is propagating */
static struct jv js_exc;
static int js_steps, js_depth;
static int js_prevent;               /* a handler returned false or called preventDefault() */

static const struct jv js_undef = { JT_UNDEF, 0 };
static struct jv js_mk(int t, js_s64 n) { struct jv v; v.t = (u8)t; v.n = n; return v; }
static struct jv js_num_i(int n) { return js_mk(JT_NUM, (js_s64)n * JS_ONE); }
static struct jv js_bool(int b) { return js_mk(JT_BOOL, b ? 1 : 0); }
static void js_fail(const char *m) {
    if (js_fatal) return;
    js_fatal = 1; int i = 0; for (; m[i] && i < 158; i++) js_errmsg[i] = m[i];
    js_errmsg[i] = 0;
}

/* ------------------------- small string helpers --------------------- */
static int js_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int js_cmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static int js_isdig(int c) { return c >= '0' && c <= '9'; }
static int js_isid0(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80; }
static int js_isid(int c) { return js_isid0(c) || js_isdig(c); }
static int js_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static u32 js_alloc(u32 n) {                                       /* bytes in the string heap; 0 on failure */
    if (js_htop + n + 1 > JS_HEAP) { js_fail("out of memory (strings)"); return 0; }
    u32 o = js_htop; js_htop += n; return o;
}
static u32 js_newstr(const char *s, int n) {
    if (n <= 0) return 0;                                          /* offset 0 is the empty string */
    u32 o = js_alloc((u32)n + 1); if (!o) return 0;
    memcpy(js_heap + o, s, (size_t)n); js_heap[o + (u32)n] = 0; js_htop = o + (u32)n + 1;
    return o;
}
static struct jv js_str(const char *s) { return js_mk(JT_STR, js_newstr(s, js_len(s))); }
static const char *js_cs(struct jv v) { return js_heap + (u32)v.n; }
static u32 js_intern(const char *s, int n) {
    u32 h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    for (u32 k = 0; k < JS_ITAB; k++) {
        u32 slot = (h + k) & (JS_ITAB - 1), o = js_itab[slot];
        if (!o) { o = js_newstr(s, n); if (!o) return 0; js_itab[slot] = o; return o; }
        if (!memcmp(js_heap + o, s, (size_t)n) && !js_heap[o + (u32)n]) return o;
    }
    js_fail("too many names"); return 0;
}
static u32 js_intern_c(const char *s) { return js_intern(s, js_len(s)); }

/* ----------------------------- 64-bit maths ------------------------- */
static js_u64 js_udiv(js_u64 n, js_u64 d, js_u64 *rem) {            /* no libgcc: shift and subtract */
    js_u64 q = 0, r = 0;
    if (!d) { if (rem) *rem = 0; return 0; }
    if ((n >> 32) == 0 && (d >> 32) == 0) { if (rem) *rem = (u32)n % (u32)d; return (u32)n / (u32)d; }
    for (int i = 63; i >= 0; i--) { r = (r << 1) | ((n >> i) & 1); if (r >= d) { r -= d; q |= (js_u64)1 << i; } }
    if (rem) *rem = r;
    return q;
}
static int js_isnan(js_s64 n) { return n == JS_NAN; }
static js_s64 js_nadd(js_s64 a, js_s64 b) { if (js_isnan(a) || js_isnan(b)) return JS_NAN; return a + b; }
static js_s64 js_nsub(js_s64 a, js_s64 b) { if (js_isnan(a) || js_isnan(b)) return JS_NAN; return a - b; }
static js_s64 js_nmul(js_s64 a, js_s64 b) {
    if (js_isnan(a) || js_isnan(b)) return JS_NAN;
    int neg = (a < 0) != (b < 0);
    js_u64 ua = a < 0 ? (js_u64)(-a) : (js_u64)a, ub = b < 0 ? (js_u64)(-b) : (js_u64)b;
    js_u64 ah = ua >> 32, al = ua & 0xFFFFFFFFu, bh = ub >> 32, bl = ub & 0xFFFFFFFFu;
    js_u64 r = ((ah * bh) << 32) + ah * bl + al * bh + ((al * bl) >> 32);
    return neg ? -(js_s64)r : (js_s64)r;
}
static js_s64 js_ndiv(js_s64 a, js_s64 b) {
    if (js_isnan(a) || js_isnan(b)) return JS_NAN;
    if (b == 0) return a == 0 ? JS_NAN : a > 0 ? JS_INF : -JS_INF;
    int neg = (a < 0) != (b < 0);
    js_u64 ua = a < 0 ? (js_u64)(-a) : (js_u64)a, ub = b < 0 ? (js_u64)(-b) : (js_u64)b, r;
    js_u64 q = js_udiv(ua, ub, &r), frac = 0;
    for (int i = 0; i < 32; i++) { r <<= 1; frac <<= 1; if (r >= ub) { r -= ub; frac |= 1; } }
    js_u64 res = (q << 32) | frac;
    return neg ? -(js_s64)res : (js_s64)res;
}
static js_s64 js_nmod(js_s64 a, js_s64 b) {
    if (js_isnan(a) || js_isnan(b) || b == 0) return JS_NAN;
    js_u64 ua = a < 0 ? (js_u64)(-a) : (js_u64)a, ub = b < 0 ? (js_u64)(-b) : (js_u64)b, r;
    js_udiv(ua, ub, &r);
    return a < 0 ? -(js_s64)r : (js_s64)r;
}
static js_s64 js_floor_n(js_s64 n) { if (js_isnan(n) || n == JS_INF || n == -JS_INF) return n; return n & ~(JS_ONE - 1); }
static int js_toint(js_s64 n) { if (js_isnan(n)) return 0; if (n == JS_INF) return 0x7FFFFFFF; if (n == -JS_INF) return -0x7FFFFFFF - 1; js_s64 i = n < 0 ? -((-n) >> 32) : (n >> 32); return (int)i; }
static js_s64 js_sqrt_n(js_s64 n) {                                 /* Newton: r = (r + x/r) / 2 */
    if (js_isnan(n) || n < 0) return JS_NAN;
    if (n == 0 || n == JS_INF) return n;
    js_s64 r = (n >> 1) + JS_ONE;
    for (int i = 0; i < 60; i++) {
        js_s64 nr = (r + js_ndiv(n, r)) >> 1;
        if (nr == r || nr == r + 1 || nr == r - 1) { r = nr; break; }
        r = nr;
    }
    return r;
}

/* ------------------------------- numbers ---------------------------- */
static void js_fmt_num(js_s64 n, char *out, int max) {            /* shortest decimal, at most 8 digits after the point */
    if (max < 24) { if (max > 0) out[0] = 0; return; }
    if (js_isnan(n)) { memcpy(out, "NaN", 4); return; }
    if (n == JS_INF) { memcpy(out, "Infinity", 9); return; }
    if (n == -JS_INF) { memcpy(out, "-Infinity", 10); return; }
    int o = 0;
    js_u64 u = n < 0 ? (js_u64)(-n) : (js_u64)n;
    /* round to 8 decimals */
    js_u64 ip = u >> 32, fp = u & 0xFFFFFFFFu;
    js_u64 scaled = (fp * 100000000u + 0x80000000u) >> 32;           /* 0..100000000 */
    if (scaled >= 100000000u) { scaled -= 100000000u; ip++; }
    if (n < 0 && (ip || scaled)) out[o++] = '-';
    char tmp[24]; int t = 0; u32 ipv = (u32)ip;
    if (!ipv) tmp[t++] = '0';
    while (ipv) { tmp[t++] = (char)('0' + ipv % 10); ipv /= 10; }
    while (t) out[o++] = tmp[--t];
    if (scaled) {
        char d[9]; u32 s = (u32)scaled;
        for (int i = 7; i >= 0; i--) { d[i] = (char)('0' + s % 10); s /= 10; }
        int e = 8; while (e > 0 && d[e - 1] == '0') e--;
        out[o++] = '.'; for (int i = 0; i < e; i++) out[o++] = d[i];
    }
    out[o] = 0;
}
static js_s64 js_parse_num(const char *s, int n, int *used, int allow_partial) {   /* decimal number text -> value, JS_NAN if none */
    int i = 0, neg = 0;
    while (i < n && js_isspace(s[i])) i++;
    int st = i;
    if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
    if (i + 8 <= n && !memcmp(s + i, "Infinity", 8)) { if (used) *used = i + 8; return neg ? -JS_INF : JS_INF; }
    if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2; js_u64 v = 0; int any = 0;
        while (i < n) { int c = s[i], d = js_isdig(c) ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; if (d < 0) break; v = v * 16 + (js_u64)d; i++; any = 1; }
        if (!any) return JS_NAN;
        if (used) *used = i;
        return neg ? -(js_s64)(v << 32) : (js_s64)(v << 32);
    }
    js_u64 ip = 0; int any = 0;
    while (i < n && js_isdig(s[i])) { if (ip < 0x7FFFFFFFu) ip = ip * 10 + (js_u64)(s[i] - '0'); i++; any = 1; }
    js_u64 frac = 0, den = 1;
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && js_isdig(s[i])) { if (den < 1000000000u) { frac = frac * 10 + (js_u64)(s[i] - '0'); den *= 10; } i++; any = 1; }
    }
    if (!any) return JS_NAN;
    js_s64 val = (js_s64)(ip << 32);
    if (frac) val += (js_s64)js_udiv(frac << 32, den, 0);
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1, eneg = 0, ev = 0, eany = 0;
        if (j < n && (s[j] == '-' || s[j] == '+')) { eneg = s[j] == '-'; j++; }
        while (j < n && js_isdig(s[j])) { if (ev < 30) ev = ev * 10 + (s[j] - '0'); j++; eany = 1; }
        if (eany) { i = j; for (int k = 0; k < ev; k++) val = eneg ? js_ndiv(val, js_num_i(10).n) : js_nmul(val, js_num_i(10).n); }
    }
    if (!allow_partial) { while (i < n && js_isspace(s[i])) i++; if (i != n) return JS_NAN; }
    (void)st;
    if (used) *used = i;
    return neg ? -val : val;
}

/* ------------------------- objects, arrays, scopes ------------------- */
static struct jv js_new_obj(void) {
    if (js_otop >= JS_OBJS) { js_fail("out of memory (objects)"); return js_undef; }
    js_objs[js_otop].first = js_objs[js_otop].last = 0;
    return js_mk(JT_OBJ, js_otop++);
}
static u32 js_vp_alloc(u32 n) {
    if (js_vtop + n > JS_VPOOL) { js_fail("out of memory (values)"); return 0; }
    u32 o = js_vtop; js_vtop += n; return o;
}
static struct jv js_new_arr(u32 cap) {
    if (js_atop >= JS_ARRS) { js_fail("out of memory (arrays)"); return js_undef; }
    if (cap < 4) cap = 4;
    u32 d = js_vp_alloc(cap); if (!d && js_fatal) return js_undef;
    struct jarr *a = &js_arrs[js_atop]; a->len = 0; a->cap = cap; a->data = d;
    return js_mk(JT_ARR, js_atop++);
}
static void js_arr_push(struct jv arr, struct jv v) {
    struct jarr *a = &js_arrs[arr.n];
    if (a->len >= a->cap) {
        u32 nc = a->cap * 2, nd = js_vp_alloc(nc); if (js_fatal) return;
        memcpy(&js_vpool[nd], &js_vpool[a->data], a->len * sizeof(struct jv));
        a->data = nd; a->cap = nc;
    }
    js_vpool[a->data + a->len++] = v;
}
static struct jv js_arr_get(struct jv arr, u32 i) { struct jarr *a = &js_arrs[arr.n]; return i < a->len ? js_vpool[a->data + i] : js_undef; }
static void js_arr_set(struct jv arr, u32 i, struct jv v) {
    struct jarr *a = &js_arrs[arr.n];
    if (i > 100000) { js_fail("array index too large"); return; }
    while (a->len <= i) { js_arr_push(arr, js_undef); if (js_fatal) return; a = &js_arrs[arr.n]; }
    js_vpool[a->data + i] = v;
}
static u32 js_find_prop(struct jv obj, u32 key) {
    for (u32 p = js_objs[obj.n].first; p; p = js_props[p].next) if (js_props[p].key == key) return p;
    return 0;
}
static struct jv js_obj_get(struct jv obj, u32 key) { u32 p = js_find_prop(obj, key); return p ? js_props[p].v : js_undef; }
static void js_obj_set(struct jv obj, u32 key, struct jv v) {
    u32 p = js_find_prop(obj, key);
    if (p) { js_props[p].v = v; return; }
    if (js_ptop >= JS_PROPS) { js_fail("out of memory (properties)"); return; }
    if (!js_ptop) js_ptop = 1;                                       /* property 0 means "none" */
    p = js_ptop++;
    js_props[p].key = key; js_props[p].v = v; js_props[p].next = 0;
    struct jobj *o = &js_objs[obj.n];
    if (o->last) js_props[o->last].next = p; else o->first = p;
    o->last = p;
}
static void js_obj_del(struct jv obj, u32 key) {
    struct jobj *o = &js_objs[obj.n]; u32 prev = 0;
    for (u32 p = o->first; p; prev = p, p = js_props[p].next) if (js_props[p].key == key) {
        if (prev) js_props[prev].next = js_props[p].next; else o->first = js_props[p].next;
        if (o->last == p) o->last = prev;
        return;
    }
}
static u32 js_newscope(u32 parent) {
    if (js_stop >= JS_SCOPES) { js_fail("out of memory (scopes)"); return 0; }
    if (!js_stop) js_stop = 1;                                       /* scope 0 is the global scope */
    js_scopes[js_stop].parent = parent; js_scopes[js_stop].first = 0;
    return js_stop++;
}
static struct jvar *js_var_at(u32 scope, u32 idx) { return scope == 0 ? &js_gvars[idx] : &js_lvars[idx]; }
static struct jvar *js_find_var_in(u32 scope, u32 name) {
    for (u32 v = js_scopes[scope].first; v; v = js_var_at(scope, v)->next) if (js_var_at(scope, v)->name == name) return js_var_at(scope, v);
    return 0;
}
static struct jvar *js_lookup(u32 scope, u32 name) {
    for (;;) {
        struct jvar *v = js_find_var_in(scope, name);
        if (v) return v;
        if (scope == 0) return 0;
        scope = js_scopes[scope].parent;
    }
}
static void js_declare(u32 scope, u32 name, struct jv val) {
    struct jvar *v = js_find_var_in(scope, name);
    if (v) { v->v = val; return; }
    u32 *top = scope == 0 ? &js_gvtop : &js_lvtop, lim = scope == 0 ? JS_GVARS : JS_LVARS;
    if (!*top) *top = 1;
    if (*top >= lim) { js_fail("out of memory (variables)"); return; }
    u32 idx = (*top)++;
    struct jvar *nv = js_var_at(scope, idx);
    nv->name = name; nv->v = val; nv->next = js_scopes[scope].first; js_scopes[scope].first = idx;
}

/* =============================== parser ============================== */
enum { N_NONE, N_NUM, N_STR, N_IDENT, N_TRUE, N_FALSE, N_NULL, N_UNDEF, N_THIS, N_ARRAY, N_OBJECT, N_PROP, N_FUNC, N_MEMBER, N_INDEX, N_CALL, N_NEW,
       N_UNARY, N_UPDATE, N_BINARY, N_LOGICAL, N_ASSIGN, N_COND, N_SEQ, N_SPREAD,
       N_VAR, N_DECL, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_WHILE, N_DOWHILE, N_RETURN, N_BREAK, N_CONTINUE, N_THROW, N_TRY, N_SWITCH, N_CASE, N_EMPTY };
struct jn { u8 k; u32 op, a, b, c, d, next; };
static struct jn js_nodes[JS_NODES]; static u32 js_ntop;

#define PK1(a) ((u32)(unsigned char)(a))
#define PK2(a, b) (PK1(a) | PK1(b) << 8)
#define PK3(a, b, c) (PK2(a, b) | PK1(c) << 16)
#define PK4(a, b, c, d) (PK3(a, b, c) | PK1(d) << 24)
enum { TK_EOF, TK_NUM, TK_STR, TK_ID, TK_TPL, TK_P };
struct jlex { const char *s; int n, i; int tk; u32 op; js_s64 num; u32 str; int nl, line, tpl_s, tpl_e; };
static struct jlex L;
static int js_pdepth;

static u32 js_node(int k) {
    if (js_ntop >= JS_NODES) { js_fail("script too big"); return 0; }
    if (!js_ntop) js_ntop = 1;
    u32 i = js_ntop++; js_nodes[i].k = (u8)k; js_nodes[i].op = 0; js_nodes[i].a = js_nodes[i].b = js_nodes[i].c = js_nodes[i].d = js_nodes[i].next = 0;
    return i;
}
static void js_perr(const char *what) {
    char m[150]; int o = 0; const char *p = "SyntaxError: ";
    while (*p) m[o++] = *p++;
    for (int i = 0; what[i] && o < 100; i++) m[o++] = what[i];
    const char *l = " (line "; while (*l) m[o++] = *l++;
    char t[12]; int tl = 0, ln = L.line; if (!ln) t[tl++] = '0'; while (ln) { t[tl++] = (char)('0' + ln % 10); ln /= 10; }
    while (tl) m[o++] = t[--tl];
    m[o++] = ')'; m[o] = 0;
    js_fail(m);
}
static int js_hexv(int c) { return js_isdig(c) ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
/* read an escape sequence after the backslash at s[*i]; returns the character (bytes > 127 are replaced by '?') */
static int js_escape(const char *s, int n, int *i) {
    if (*i >= n) return 0;
    int c = (unsigned char)s[(*i)++];
    switch (c) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case '0': return 0; case 'b': return 8; case 'f': return 12; case 'v': return 11;
    case 'x': { int v = 0, k = 0; while (k < 2 && *i < n && js_hexv(s[*i]) >= 0) { v = v * 16 + js_hexv(s[(*i)++]); k++; } return v < 128 && v ? v : '?'; }
    case 'u': {
        int v = 0, k = 0;
        if (*i < n && s[*i] == '{') { (*i)++; while (*i < n && s[*i] != '}') { int h = js_hexv(s[*i]); if (h >= 0 && v < 0x110000) v = v * 16 + h; (*i)++; } if (*i < n) (*i)++; }
        else while (k < 4 && *i < n && js_hexv(s[*i]) >= 0) { v = v * 16 + js_hexv(s[(*i)++]); k++; }
        return v < 128 && v ? v : '?';
    }
    case '\n': return -1;                                            /* line continuation */
    default: return c;
    }
}
static void js_next(void) {
    L.nl = 0;
    for (;;) {                                                        /* whitespace and comments */
        while (L.i < L.n && js_isspace(L.s[L.i])) { if (L.s[L.i] == '\n') { L.nl = 1; L.line++; } L.i++; }
        if (L.i + 1 < L.n && L.s[L.i] == '/' && L.s[L.i + 1] == '/') { while (L.i < L.n && L.s[L.i] != '\n') L.i++; continue; }
        if (L.i + 1 < L.n && L.s[L.i] == '/' && L.s[L.i + 1] == '*') {
            L.i += 2; while (L.i + 1 < L.n && !(L.s[L.i] == '*' && L.s[L.i + 1] == '/')) { if (L.s[L.i] == '\n') { L.nl = 1; L.line++; } L.i++; }
            L.i += 2; continue;
        }
        if (L.i + 3 < L.n && L.s[L.i] == '<' && L.s[L.i + 1] == '!' && L.s[L.i + 2] == '-' && L.s[L.i + 3] == '-') { while (L.i < L.n && L.s[L.i] != '\n') L.i++; continue; }
        break;
    }
    if (L.i >= L.n) { L.tk = TK_EOF; return; }
    int c = (unsigned char)L.s[L.i];
    if (js_isdig(c) || (c == '.' && L.i + 1 < L.n && js_isdig(L.s[L.i + 1]))) {
        int used = 0; js_s64 v = js_parse_num(L.s + L.i, L.n - L.i, &used, 1);
        if (js_isnan(v) || used <= 0) { js_perr("bad number"); L.tk = TK_EOF; return; }
        L.i += used; L.tk = TK_NUM; L.num = v; return;
    }
    if (js_isid0(c)) {
        int st = L.i; while (L.i < L.n && js_isid((unsigned char)L.s[L.i])) L.i++;
        L.tk = TK_ID; L.str = js_intern(L.s + st, L.i - st); return;
    }
    if (c == '"' || c == '\'') {
        char buf[1024]; int o = 0; L.i++;
        while (L.i < L.n && L.s[L.i] != c) {
            if (L.s[L.i] == '\n') { js_perr("unterminated string"); L.tk = TK_EOF; return; }
            int ch = L.s[L.i++];
            if (ch == '\\') { ch = js_escape(L.s, L.n, &L.i); if (ch < 0) continue; }
            if (o < (int)sizeof buf - 1) buf[o++] = (char)ch;
        }
        if (L.i >= L.n) { js_perr("unterminated string"); L.tk = TK_EOF; return; }
        L.i++; L.tk = TK_STR; L.str = js_newstr(buf, o); return;
    }
    if (c == '`') {
        int depth = 0; L.i++; L.tpl_s = L.i;
        while (L.i < L.n) {
            char ch = L.s[L.i];
            if (ch == '\\') { L.i += 2; continue; }
            if (ch == '\n') L.line++;
            if (depth == 0 && ch == '`') break;
            if (ch == '$' && L.i + 1 < L.n && L.s[L.i + 1] == '{') { depth++; L.i += 2; continue; }
            if (depth > 0 && ch == '{') depth++;
            else if (depth > 0 && ch == '}') depth--;
            L.i++;
        }
        if (L.i >= L.n) { js_perr("unterminated template"); L.tk = TK_EOF; return; }
        L.tpl_e = L.i; L.i++; L.tk = TK_TPL; return;
    }
    static const u32 four[] = { PK4('>', '>', '>', '=') };
    static const u32 three[] = { PK3('=', '=', '='), PK3('!', '=', '='), PK3('>', '>', '>'), PK3('<', '<', '='), PK3('>', '>', '='), PK3('.', '.', '.'), PK3('*', '*', '=') };
    static const u32 two[] = { PK2('=', '>'), PK2('=', '='), PK2('!', '='), PK2('<', '='), PK2('>', '='), PK2('&', '&'), PK2('|', '|'), PK2('+', '+'), PK2('-', '-'),
        PK2('+', '='), PK2('-', '='), PK2('*', '='), PK2('/', '='), PK2('%', '='), PK2('&', '='), PK2('|', '='), PK2('^', '='), PK2('<', '<'), PK2('>', '>'),
        PK2('*', '*'), PK2('?', '?'), PK2('?', '.') };
    u32 w = 0; for (int k = 0; k < 4 && L.i + k < L.n; k++) w |= PK1(L.s[L.i + k]) << (8 * k);
    for (unsigned k = 0; k < sizeof four / sizeof four[0]; k++) if (w == four[k]) { L.i += 4; L.tk = TK_P; L.op = w; return; }
    for (unsigned k = 0; k < sizeof three / sizeof three[0]; k++) if ((w & 0xFFFFFF) == three[k]) { L.i += 3; L.tk = TK_P; L.op = w & 0xFFFFFF; return; }
    for (unsigned k = 0; k < sizeof two / sizeof two[0]; k++) if ((w & 0xFFFF) == two[k]) {
        if (two[k] == PK2('?', '.') && L.i + 2 < L.n && js_isdig(L.s[L.i + 2])) continue;      /* a ? .5 : 1 */
        L.i += 2; L.tk = TK_P; L.op = w & 0xFFFF; return;
    }
    L.i++; L.tk = TK_P; L.op = (u32)c;
}
static int js_is_p(u32 op) { return L.tk == TK_P && L.op == op; }
static int js_is_kw(const char *kw) { return L.tk == TK_ID && !js_cmp(js_heap + L.str, kw); }
static int js_eat_p(u32 op) { if (js_is_p(op)) { js_next(); return 1; } return 0; }
static int js_eat_kw(const char *kw) { if (js_is_kw(kw)) { js_next(); return 1; } return 0; }
static void js_expect_p(u32 op) {
    if (js_is_p(op)) { js_next(); return; }
    char m[40]; int o = 0; m[o++] = 'e'; m[o++] = 'x'; m[o++] = 'p'; m[o++] = 'e'; m[o++] = 'c'; m[o++] = 't'; m[o++] = 'e'; m[o++] = 'd'; m[o++] = ' '; m[o++] = '\'';
    for (int k = 0; k < 4 && ((op >> (8 * k)) & 255); k++) m[o++] = (char)((op >> (8 * k)) & 255);
    m[o++] = '\''; m[o] = 0;
    js_perr(m);
}
static void js_semi(void) {                                            /* a ; or automatic semicolon insertion */
    if (js_eat_p(';')) return;
    if (js_is_p('}') || L.tk == TK_EOF || L.nl) return;
    js_perr("expected ';'");
}
static u32 js_parse_assign(void);
static u32 js_parse_stmt(void);
static u32 js_parse_block_body(void);

static int js_is_reserved(const char *w) {
    static const char *const kw[] = { "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue", "new", "typeof",
        "true", "false", "null", "this", "try", "catch", "finally", "throw", "switch", "case", "default", "delete", "in", "of", "void", "class", "instanceof" };
    for (unsigned i = 0; i < sizeof kw / sizeof kw[0]; i++) if (!js_cmp(w, kw[i])) return 1;
    return 0;
}
/* is the '(' that is the current token the start of an arrow function's parameter list? */
static int js_arrow_ahead(void) {
    struct jlex save = L; int depth = 0, res = 0;
    for (;;) {
        if (L.tk == TK_EOF || js_fatal) break;
        if (js_is_p('(')) depth++;
        else if (js_is_p(')')) { depth--; if (depth == 0) { js_next(); res = js_is_p(PK2('=', '>')); break; } }
        js_next();
    }
    int ferr = js_fatal; L = save;
    if (!ferr) js_fatal = 0;
    return res;
}
static u32 js_parse_params(void) {                                     /* after '(' : returns the list of N_IDENT (b = default) */
    u32 head = 0, tail = 0;
    while (!js_is_p(')') && L.tk != TK_EOF && !js_fatal) {
        if (L.tk != TK_ID) { js_perr("bad parameter (destructuring is not supported)"); return 0; }
        u32 n = js_node(N_IDENT); js_nodes[n].a = L.str; js_next();
        if (js_eat_p('=')) js_nodes[n].b = js_parse_assign();
        if (tail) js_nodes[tail].next = n; else head = n;
        tail = n;
        if (!js_eat_p(',')) break;
    }
    js_expect_p(')');
    return head;
}
static u32 js_parse_func_body(u32 fn, int arrow_expr) {
    if (arrow_expr) { js_nodes[fn].b = js_parse_assign(); js_nodes[fn].op = 1; return fn; }
    js_expect_p('{');
    js_nodes[fn].b = js_parse_block_body();
    js_expect_p('}');
    return fn;
}
/* a template literal: the raw text between the backticks becomes a chain of string concatenations */
static u32 js_parse_template(int s, int e) {
    u32 cur = 0; int i = s;
    char buf[1024]; int o = 0;
    u32 first = js_node(N_STR); js_nodes[first].a = 0; cur = first;                 /* starts with "" so that + concatenates */
    while (i <= e && !js_fatal) {
        if (i == e || (L.s[i] == '$' && i + 1 < e && L.s[i + 1] == '{')) {
            if (o) { u32 sn = js_node(N_STR); js_nodes[sn].a = js_newstr(buf, o); o = 0; u32 b = js_node(N_BINARY); js_nodes[b].op = '+'; js_nodes[b].a = cur; js_nodes[b].b = sn; cur = b; }
            if (i == e) break;
            int depth = 1, j = i + 2;
            while (j < e && depth) {
                char ch = L.s[j];
                if (ch == '"' || ch == '\'' || ch == '`') { char q = ch; j++; while (j < e && L.s[j] != q) { if (L.s[j] == '\\') j++; j++; } }
                else if (ch == '{') depth++;
                else if (ch == '}') { depth--; if (!depth) break; }
                j++;
            }
            if (depth) { js_perr("unterminated ${"); return 0; }
            struct jlex save = L;
            L.s = save.s + i + 2; L.n = j - (i + 2); L.i = 0;
            js_next();
            u32 ex = js_parse_assign();
            if (L.tk != TK_EOF && !js_fatal) js_perr("bad ${} expression");
            int line = save.line; L = save; L.line = line;
            u32 b = js_node(N_BINARY); js_nodes[b].op = '+'; js_nodes[b].a = cur; js_nodes[b].b = ex; cur = b;
            i = j + 1; continue;
        }
        if (L.s[i] == '\\') { i++; int ch = js_escape(L.s, e, &i); if (ch >= 0 && o < (int)sizeof buf - 1) buf[o++] = (char)ch; continue; }
        if (o < (int)sizeof buf - 1) buf[o++] = L.s[i];
        i++;
    }
    return cur;
}
static u32 js_parse_args(u32 *count) {                                 /* after '(' */
    u32 head = 0, tail = 0; u32 n = 0;
    while (!js_is_p(')') && L.tk != TK_EOF && !js_fatal) {
        u32 a;
        if (js_eat_p(PK3('.', '.', '.'))) { a = js_node(N_SPREAD); js_nodes[a].a = js_parse_assign(); }
        else a = js_parse_assign();
        if (tail) js_nodes[tail].next = a; else head = a;
        tail = a; n++;
        if (!js_eat_p(',')) break;
    }
    js_expect_p(')');
    if (count) *count = n;
    return head;
}
static u32 js_parse_primary(void) {
    if (js_fatal) return 0;
    if (L.tk == TK_NUM) { u32 n = js_node(N_NUM); if (js_ctop >= JS_CONSTS) { js_fail("too many numbers"); return 0; } js_consts[js_ctop] = L.num; js_nodes[n].a = js_ctop++; js_next(); return n; }
    if (L.tk == TK_STR) { u32 n = js_node(N_STR); js_nodes[n].a = L.str; js_next(); return n; }
    if (L.tk == TK_TPL) { int s = L.tpl_s, e = L.tpl_e; u32 n = js_parse_template(s, e); js_next(); return n; }
    if (L.tk == TK_ID) {
        const char *w = js_heap + L.str;
        if (!js_cmp(w, "true")) { js_next(); return js_node(N_TRUE); }
        if (!js_cmp(w, "false")) { js_next(); return js_node(N_FALSE); }
        if (!js_cmp(w, "null")) { js_next(); return js_node(N_NULL); }
        if (!js_cmp(w, "undefined")) { js_next(); return js_node(N_UNDEF); }
        if (!js_cmp(w, "this")) { js_next(); return js_node(N_THIS); }
        if (!js_cmp(w, "function")) {
            js_next(); u32 fn = js_node(N_FUNC);
            if (js_eat_p('*')) { js_perr("generators are not supported"); return 0; }
            if (L.tk == TK_ID) { js_nodes[fn].c = L.str; js_next(); }
            js_expect_p('('); js_nodes[fn].a = js_parse_params();
            return js_parse_func_body(fn, 0);
        }
        if (!js_cmp(w, "class")) { js_perr("classes are not supported"); return 0; }
        if (!js_cmp(w, "async") || !js_cmp(w, "await") || !js_cmp(w, "yield")) { js_perr("async/await is not supported"); return 0; }
        if (js_is_reserved(w)) { js_perr("unexpected keyword"); return 0; }
        u32 id = L.str; js_next();
        if (js_is_p(PK2('=', '>'))) {                                  /* x => ... */
            js_next(); u32 fn = js_node(N_FUNC); u32 p = js_node(N_IDENT); js_nodes[p].a = id; js_nodes[fn].a = p;
            js_nodes[fn].d = 1;
            if (js_is_p('{')) return js_parse_func_body(fn, 0);
            return js_parse_func_body(fn, 1);
        }
        u32 n = js_node(N_IDENT); js_nodes[n].a = id; return n;
    }
    if (L.tk == TK_P) {
        if (L.op == '(') {
            if (js_arrow_ahead()) {
                js_next(); u32 fn = js_node(N_FUNC); js_nodes[fn].a = js_parse_params(); js_nodes[fn].d = 1;
                js_expect_p(PK2('=', '>'));
                if (js_is_p('{')) return js_parse_func_body(fn, 0);
                return js_parse_func_body(fn, 1);
            }
            js_next(); u32 e = js_parse_assign();
            while (js_eat_p(',')) { u32 s = js_node(N_SEQ); js_nodes[s].a = e; js_nodes[s].b = js_parse_assign(); e = s; }
            js_expect_p(')'); return e;
        }
        if (L.op == '[') {
            js_next(); u32 n = js_node(N_ARRAY), head = 0, tail = 0;
            while (!js_is_p(']') && L.tk != TK_EOF && !js_fatal) {
                u32 a;
                if (js_is_p(',')) a = js_node(N_UNDEF);
                else if (js_eat_p(PK3('.', '.', '.'))) { a = js_node(N_SPREAD); js_nodes[a].a = js_parse_assign(); }
                else a = js_parse_assign();
                if (tail) js_nodes[tail].next = a; else head = a;
                tail = a;
                if (!js_eat_p(',')) break;
            }
            js_expect_p(']'); js_nodes[n].a = head; return n;
        }
        if (L.op == '{') {
            js_next(); u32 n = js_node(N_OBJECT), head = 0, tail = 0;
            while (!js_is_p('}') && L.tk != TK_EOF && !js_fatal) {
                u32 pn = js_node(N_PROP);
                if (L.tk == TK_ID || L.tk == TK_STR) {
                    u32 key = L.tk == TK_STR ? js_intern(js_heap + L.str, js_len(js_heap + L.str)) : L.str; int was_id = L.tk == TK_ID; js_next();
                    js_nodes[pn].a = key;
                    if (js_eat_p(':')) js_nodes[pn].b = js_parse_assign();
                    else if (was_id && (js_is_p(',') || js_is_p('}'))) { u32 id = js_node(N_IDENT); js_nodes[id].a = key; js_nodes[pn].b = id; }      /* {x} shorthand */
                    else if (was_id && js_is_p('(')) {                      /* method shorthand */
                        u32 fn = js_node(N_FUNC); js_next(); js_nodes[fn].a = js_parse_params(); js_parse_func_body(fn, 0); js_nodes[pn].b = fn;
                    } else { js_perr("bad object literal"); return 0; }
                } else if (L.tk == TK_NUM) {
                    char t[24]; js_fmt_num(L.num, t, sizeof t); js_nodes[pn].a = js_intern_c(t); js_next(); js_expect_p(':'); js_nodes[pn].b = js_parse_assign();
                } else if (js_is_p('[')) {
                    js_perr("computed keys are not supported"); return 0;
                } else { js_perr("bad object literal"); return 0; }
                if (tail) js_nodes[tail].next = pn; else head = pn;
                tail = pn;
                if (!js_eat_p(',')) break;
            }
            js_expect_p('}'); js_nodes[n].a = head; return n;
        }
    }
    js_perr("unexpected token");
    return 0;
}
static u32 js_parse_postfix(void) {
    u32 e;
    if (js_is_kw("new")) {
        js_next(); u32 callee = js_parse_primary();
        for (;;) {                                                     /* member accesses bind tighter than new's call */
            if (js_eat_p('.')) { if (L.tk != TK_ID) { js_perr("expected a name"); return 0; } u32 m = js_node(N_MEMBER); js_nodes[m].a = callee; js_nodes[m].b = L.str; js_next(); callee = m; }
            else break;
        }
        u32 n = js_node(N_NEW); js_nodes[n].a = callee;
        if (js_eat_p('(')) js_nodes[n].b = js_parse_args(NULL);
        e = n;
    } else e = js_parse_primary();
    while (!js_fatal) {
        if (js_is_p('.') || js_is_p(PK2('?', '.'))) {
            int opt = L.op != '.'; js_next();
            if (opt && js_is_p('(')) { js_next(); u32 c = js_node(N_CALL); js_nodes[c].a = e; js_nodes[c].b = js_parse_args(NULL); js_nodes[c].op = 1; e = c; continue; }
            if (opt && js_is_p('[')) { js_next(); u32 m = js_node(N_INDEX); js_nodes[m].a = e; js_nodes[m].b = js_parse_assign(); js_nodes[m].op = 1; js_expect_p(']'); e = m; continue; }
            if (L.tk != TK_ID) { js_perr("expected a property name"); return 0; }
            u32 m = js_node(N_MEMBER); js_nodes[m].a = e; js_nodes[m].b = L.str; js_nodes[m].op = (u32)opt; js_next(); e = m;
        } else if (js_is_p('[')) {
            js_next(); u32 m = js_node(N_INDEX); js_nodes[m].a = e; js_nodes[m].b = js_parse_assign(); js_expect_p(']'); e = m;
        } else if (js_is_p('(')) {
            js_next(); u32 c = js_node(N_CALL); js_nodes[c].a = e; js_nodes[c].b = js_parse_args(NULL); e = c;
        } else if ((js_is_p(PK2('+', '+')) || js_is_p(PK2('-', '-'))) && !L.nl) {
            u32 u = js_node(N_UPDATE); js_nodes[u].op = L.op; js_nodes[u].a = e; js_nodes[u].c = 0; js_next(); e = u;
        } else break;
    }
    return e;
}
static u32 js_parse_unary(void) {
    if (js_fatal) return 0;
    if (++js_pdepth > 120) { js_perr("expression too deeply nested"); return 0; }
    u32 r;
    if (L.tk == TK_P && (L.op == '!' || L.op == '-' || L.op == '+' || L.op == '~')) {
        u32 op = L.op; js_next(); r = js_node(N_UNARY); js_nodes[r].op = op; js_nodes[r].a = js_parse_unary();
    } else if (L.tk == TK_P && (L.op == PK2('+', '+') || L.op == PK2('-', '-'))) {
        u32 op = L.op; js_next(); r = js_node(N_UPDATE); js_nodes[r].op = op; js_nodes[r].c = 1; js_nodes[r].a = js_parse_unary();
    } else if (js_is_kw("typeof") || js_is_kw("void") || js_is_kw("delete")) {
        u32 op = js_is_kw("typeof") ? 't' : js_is_kw("void") ? 'v' : 'd'; js_next();
        r = js_node(N_UNARY); js_nodes[r].op = op; js_nodes[r].a = js_parse_unary();
    } else r = js_parse_postfix();
    js_pdepth--;
    return r;
}
static int js_binprec(u32 *op) {                                       /* precedence of the current token as a binary operator, 0 if none */
    if (L.tk == TK_ID) { if (js_is_kw("in")) { *op = 'i'; return 8; } if (js_is_kw("instanceof")) { *op = 'I'; return 8; } return 0; }
    if (L.tk != TK_P) return 0;
    *op = L.op;
    switch (L.op) {
    case PK2('?', '?'): return 1;
    case PK2('|', '|'): return 2;
    case PK2('&', '&'): return 3;
    case '|': return 4;
    case '^': return 5;
    case '&': return 6;
    case PK2('=', '='): case PK2('!', '='): case PK3('=', '=', '='): case PK3('!', '=', '='): return 7;
    case '<': case '>': case PK2('<', '='): case PK2('>', '='): return 8;
    case PK2('<', '<'): case PK2('>', '>'): case PK3('>', '>', '>'): return 9;
    case '+': case '-': return 10;
    case '*': case '/': case '%': return 11;
    case PK2('*', '*'): return 12;
    }
    return 0;
}
static u32 js_parse_binary(int minp) {
    u32 left = js_parse_unary();
    for (;;) {
        u32 op; int p = js_binprec(&op);
        if (!p || p < minp || js_fatal) return left;
        js_next();
        u32 right = js_parse_binary(op == PK2('*', '*') ? p : p + 1);
        u32 n = js_node(op == PK2('&', '&') || op == PK2('|', '|') || op == PK2('?', '?') ? N_LOGICAL : N_BINARY);
        js_nodes[n].op = op; js_nodes[n].a = left; js_nodes[n].b = right; left = n;
    }
}
static u32 js_parse_assign(void) {
    if (js_fatal) return 0;
    if (++js_pdepth > 120) { js_perr("expression too deeply nested"); return 0; }
    u32 left = js_parse_binary(1);
    if (js_is_p('?') && !js_fatal) {
        js_next(); u32 c = js_node(N_COND); js_nodes[c].a = left; js_nodes[c].b = js_parse_assign(); js_expect_p(':'); js_nodes[c].c = js_parse_assign(); left = c;
    } else if (L.tk == TK_P && !js_fatal) {
        u32 op = L.op;
        if (op == '=' || op == PK2('+', '=') || op == PK2('-', '=') || op == PK2('*', '=') || op == PK2('/', '=') || op == PK2('%', '=') ||
            op == PK2('&', '=') || op == PK2('|', '=') || op == PK2('^', '=') || op == PK3('<', '<', '=') || op == PK3('>', '>', '=') || op == PK4('>', '>', '>', '=') || op == PK3('*', '*', '=')) {
            int k = js_nodes[left].k;
            if (k != N_IDENT && k != N_MEMBER && k != N_INDEX) { js_perr("invalid assignment target"); js_pdepth--; return 0; }
            js_next(); u32 a = js_node(N_ASSIGN); js_nodes[a].op = op; js_nodes[a].a = left; js_nodes[a].b = js_parse_assign(); left = a;
        }
    }
    js_pdepth--;
    return left;
}
static u32 js_parse_expr(void) {
    u32 e = js_parse_assign();
    while (js_is_p(',') && !js_fatal) { js_next(); u32 s = js_node(N_SEQ); js_nodes[s].a = e; js_nodes[s].b = js_parse_assign(); e = s; }
    return e;
}
static u32 js_parse_var(int in_for) {                                  /* var/let/const declarators */
    int is_var = js_is_kw("var");
    js_next();
    u32 v = js_node(N_VAR), head = 0, tail = 0;
    js_nodes[v].c = is_var ? 0 : 1;                                    /* c: 1 for let/const (block scoped loop variables) */
    for (;;) {
        if (L.tk != TK_ID) { js_perr("expected a variable name (destructuring is not supported)"); return 0; }
        u32 d = js_node(N_DECL); js_nodes[d].a = L.str; js_next();
        if (js_eat_p('=')) js_nodes[d].b = js_parse_assign();
        if (tail) js_nodes[tail].next = d; else head = d;
        tail = d;
        if (in_for && (js_is_kw("of") || js_is_kw("in"))) break;
        if (!js_eat_p(',')) break;
    }
    js_nodes[v].a = head;
    return v;
}
static u32 js_parse_block_body(void) {                                 /* statements up to '}' or EOF (not consumed) */
    u32 head = 0, tail = 0;
    while (!js_is_p('}') && L.tk != TK_EOF && !js_fatal) {
        u32 s = js_parse_stmt();
        if (!s) continue;
        if (tail) js_nodes[tail].next = s; else head = s;
        tail = s;
    }
    return head;
}
static u32 js_parse_stmt(void) {
    if (js_fatal) return 0;
    if (++js_pdepth > 120) { js_perr("statements nested too deeply"); return 0; }
    u32 r = 0;
    if (js_is_p(';')) { js_next(); r = js_node(N_EMPTY); }
    else if (js_is_p('{')) { js_next(); r = js_node(N_BLOCK); js_nodes[r].a = js_parse_block_body(); js_expect_p('}'); }
    else if (js_is_kw("var") || js_is_kw("let") || js_is_kw("const")) { r = js_parse_var(0); js_semi(); }
    else if (js_is_kw("function")) {
        js_next(); if (L.tk != TK_ID) { js_perr("function needs a name"); js_pdepth--; return 0; }
        u32 fn = js_node(N_FUNC); js_nodes[fn].c = L.str; js_next(); js_expect_p('('); js_nodes[fn].a = js_parse_params(); js_parse_func_body(fn, 0);
        r = js_node(N_VAR); u32 d = js_node(N_DECL); js_nodes[d].a = js_nodes[fn].c; js_nodes[d].b = fn; js_nodes[r].a = d; js_nodes[r].op = 1;     /* op 1: hoisted function declaration */
    }
    else if (js_is_kw("if")) {
        js_next(); r = js_node(N_IF); js_expect_p('('); js_nodes[r].a = js_parse_expr(); js_expect_p(')');
        js_nodes[r].b = js_parse_stmt();
        if (js_eat_kw("else")) js_nodes[r].c = js_parse_stmt();
    }
    else if (js_is_kw("while")) { js_next(); r = js_node(N_WHILE); js_expect_p('('); js_nodes[r].a = js_parse_expr(); js_expect_p(')'); js_nodes[r].b = js_parse_stmt(); }
    else if (js_is_kw("do")) {
        js_next(); r = js_node(N_DOWHILE); js_nodes[r].a = js_parse_stmt();
        if (!js_eat_kw("while")) { js_perr("expected while"); js_pdepth--; return 0; }
        js_expect_p('('); js_nodes[r].b = js_parse_expr(); js_expect_p(')'); js_eat_p(';');
    }
    else if (js_is_kw("for")) {
        js_next(); js_expect_p('(');
        u32 init = 0;
        if (js_is_p(';')) { }
        else if (js_is_kw("var") || js_is_kw("let") || js_is_kw("const")) init = js_parse_var(1);
        else init = js_parse_expr();
        if (!js_fatal && (js_is_kw("of") || js_is_kw("in"))) {
            r = js_node(N_FORIN); js_nodes[r].op = js_is_kw("of") ? 'o' : 'i'; js_next();
            js_nodes[r].a = init; js_nodes[r].b = js_parse_expr(); js_expect_p(')'); js_nodes[r].c = js_parse_stmt();
        } else {
            r = js_node(N_FOR); js_nodes[r].a = init; js_expect_p(';');
            if (!js_is_p(';')) js_nodes[r].b = js_parse_expr();
            js_expect_p(';');
            if (!js_is_p(')')) js_nodes[r].c = js_parse_expr();
            js_expect_p(')'); js_nodes[r].d = js_parse_stmt();
        }
    }
    else if (js_is_kw("return")) {
        js_next(); r = js_node(N_RETURN);
        if (!js_is_p(';') && !js_is_p('}') && L.tk != TK_EOF && !L.nl) js_nodes[r].a = js_parse_expr();
        js_semi();
    }
    else if (js_is_kw("break")) { js_next(); r = js_node(N_BREAK); js_semi(); }
    else if (js_is_kw("continue")) { js_next(); r = js_node(N_CONTINUE); js_semi(); }
    else if (js_is_kw("throw")) { js_next(); r = js_node(N_THROW); js_nodes[r].a = js_parse_expr(); js_semi(); }
    else if (js_is_kw("try")) {
        js_next(); r = js_node(N_TRY); js_expect_p('{'); js_nodes[r].a = js_parse_block_body(); js_expect_p('}');
        if (js_eat_kw("catch")) {
            if (js_eat_p('(')) { if (L.tk != TK_ID) { js_perr("expected a name"); js_pdepth--; return 0; } js_nodes[r].op = L.str; js_next(); js_expect_p(')'); }
            js_expect_p('{'); js_nodes[r].b = js_parse_block_body(); js_expect_p('}'); js_nodes[r].d |= 1;
        }
        if (js_eat_kw("finally")) { js_expect_p('{'); js_nodes[r].c = js_parse_block_body(); js_expect_p('}'); js_nodes[r].d |= 2; }
    }
    else if (js_is_kw("switch")) {
        js_next(); r = js_node(N_SWITCH); js_expect_p('('); js_nodes[r].a = js_parse_expr(); js_expect_p(')'); js_expect_p('{');
        u32 head = 0, tail = 0;
        while (!js_is_p('}') && L.tk != TK_EOF && !js_fatal) {
            u32 c = js_node(N_CASE);
            if (js_eat_kw("case")) { js_nodes[c].a = js_parse_expr(); js_nodes[c].op = 1; }
            else if (js_eat_kw("default")) js_nodes[c].op = 0;
            else { js_perr("expected case"); break; }
            js_expect_p(':');
            u32 bh = 0, bt = 0;
            while (!js_is_kw("case") && !js_is_kw("default") && !js_is_p('}') && L.tk != TK_EOF && !js_fatal) {
                u32 s = js_parse_stmt(); if (!s) continue;
                if (bt) js_nodes[bt].next = s; else bh = s;
                bt = s;
            }
            js_nodes[c].b = bh;
            if (tail) js_nodes[tail].next = c; else head = c;
            tail = c;
        }
        js_expect_p('}'); js_nodes[r].b = head;
    }
    else if (js_is_kw("class")) { js_perr("classes are not supported"); }
    else {
        r = js_node(N_EXPR); js_nodes[r].a = js_parse_expr(); js_semi();
    }
    js_pdepth--;
    return r;
}

/* ============================ conversions ============================ */
static u32 k_length, k_message, k_name, k_prototype;
static struct jv js_this; static u32 js_scope; static struct jv js_retval;
static struct jv js_throw_err(const char *kind, const char *msg) {
    struct jv e = js_new_obj();
    if (js_fatal) return js_undef;
    js_obj_set(e, k_name, js_str(kind)); js_obj_set(e, k_message, js_str(msg));
    js_exc = e; js_thrown = 1;
    return js_undef;
}
static struct jv js_s(const char *s) { return js_mk(JT_STR, js_intern_c(s)); }
static int js_truthy(struct jv v) {
    switch (v.t) {
    case JT_UNDEF: case JT_NULL: return 0;
    case JT_BOOL: return v.n != 0;
    case JT_NUM: return v.n != 0 && !js_isnan(v.n);
    case JT_STR: return js_heap[v.n] != 0;
    default: return 1;
    }
}
static struct jv js_tostr(struct jv v);
static js_s64 js_tonum(struct jv v) {
    switch (v.t) {
    case JT_NUM: return v.n;
    case JT_BOOL: return v.n ? JS_ONE : 0;
    case JT_NULL: return 0;
    case JT_STR: { const char *s = js_cs(v); return js_parse_num(s, js_len(s), NULL, 0) == JS_NAN && js_len(s) ? JS_NAN : (js_len(s) ? js_parse_num(s, js_len(s), NULL, 0) : 0); }
    case JT_ARR: { struct jarr *a = &js_arrs[v.n]; if (a->len == 0) return 0; if (a->len == 1) return js_tonum(js_vpool[a->data]); return JS_NAN; }
    default: return JS_NAN;
    }
}
static int js_arr_depth;
static struct jv js_join(struct jv arr, const char *sep) {
    struct jarr *a = &js_arrs[arr.n];
    if (js_arr_depth > 6) return js_s("");
    js_arr_depth++;
    u32 n = a->len; int total = 0, sl = js_len(sep);
    u32 *offs = NULL; (void)offs;
    /* build in the heap: first collect string values (each allocates), then concatenate */
    u32 start = js_htop; char *buf = NULL; (void)start; (void)buf;
    int cap = 256, o = 0; u32 mark = js_htop;
    char *tmp = js_heap + js_htop;                                   /* use the free heap top as scratch, then move */
    (void)tmp; (void)cap; (void)total;
    /* simple two-pass: pass 1 computes strings, pass 2 copies */
    struct jv *parts = &js_vpool[js_vtop]; u32 vsave = js_vtop;
    if (js_vp_alloc(n ? n : 1) == 0 && js_fatal) { js_arr_depth--; return js_undef; }
    parts = &js_vpool[vsave];
    for (u32 i = 0; i < n; i++) {
        struct jv e = js_vpool[a->data + i];
        parts[i] = (e.t == JT_UNDEF || e.t == JT_NULL) ? js_s("") : js_tostr(e);
        o += js_len(js_cs(parts[i])) + (i ? sl : 0);
    }
    u32 res = js_alloc((u32)o + 1);
    if (!res) { js_arr_depth--; return js_undef; }
    char *d = js_heap + res; int p = 0;
    for (u32 i = 0; i < n; i++) {
        if (i) { memcpy(d + p, sep, (size_t)sl); p += sl; }
        int l = js_len(js_cs(parts[i])); memcpy(d + p, js_cs(parts[i]), (size_t)l); p += l;
    }
    d[p] = 0; js_htop = res + (u32)p + 1;
    js_vtop = vsave; js_arr_depth--; (void)mark;
    return js_mk(JT_STR, p ? res : 0);
}
static struct jv js_tostr(struct jv v) {
    char b[40];
    switch (v.t) {
    case JT_UNDEF: return js_s("undefined");
    case JT_NULL: return js_s("null");
    case JT_BOOL: return js_s(v.n ? "true" : "false");
    case JT_NUM: js_fmt_num(v.n, b, sizeof b); return js_str(b);
    case JT_STR: return v;
    case JT_ARR: return js_join(v, ",");
    case JT_FUN: case JT_NAT: return js_s("function () { [code] }");
    case JT_ELEM: return js_s("[object HTMLElement]");
    default: return js_s("[object Object]");
    }
}
static struct jv js_concat(struct jv a, struct jv b) {
    const char *x = js_cs(a), *y = js_cs(b); int lx = js_len(x), ly = js_len(y);
    if (!ly) return a;
    if (!lx) return b;
    u32 o = js_alloc((u32)(lx + ly) + 1); if (!o) return js_undef;
    memcpy(js_heap + o, x, (size_t)lx); memcpy(js_heap + o + (u32)lx, y, (size_t)ly); js_heap[o + (u32)lx + (u32)ly] = 0;
    js_htop = o + (u32)lx + (u32)ly + 1;
    return js_mk(JT_STR, o);
}
static const char *js_typeof(struct jv v) {
    switch (v.t) {
    case JT_UNDEF: return "undefined"; case JT_BOOL: return "boolean"; case JT_NUM: return "number"; case JT_STR: return "string";
    case JT_FUN: case JT_NAT: return "function"; default: return "object";
    }
}
static int js_seq(struct jv a, struct jv b) {                         /* === */
    if (a.t != b.t) return 0;
    switch (a.t) {
    case JT_UNDEF: case JT_NULL: return 1;
    case JT_NUM: return a.n == b.n && !js_isnan(a.n);
    case JT_STR: return a.n == b.n || !js_cmp(js_cs(a), js_cs(b));
    default: return a.n == b.n;
    }
}
static int js_leq(struct jv a, struct jv b) {                         /* == */
    if (a.t == b.t) return js_seq(a, b);
    if ((a.t == JT_UNDEF || a.t == JT_NULL) && (b.t == JT_UNDEF || b.t == JT_NULL)) return 1;
    if (a.t == JT_UNDEF || a.t == JT_NULL || b.t == JT_UNDEF || b.t == JT_NULL) return 0;
    if ((a.t == JT_NUM || a.t == JT_STR || a.t == JT_BOOL) && (b.t == JT_NUM || b.t == JT_STR || b.t == JT_BOOL)) { js_s64 x = js_tonum(a), y = js_tonum(b); return x == y && !js_isnan(x); }
    return 0;
}
/* -1 less, 0 equal, 1 greater, 2 unordered (NaN) */
static int js_compare(struct jv a, struct jv b) {
    if (a.t == JT_STR && b.t == JT_STR) { int c = js_cmp(js_cs(a), js_cs(b)); return c < 0 ? -1 : c > 0 ? 1 : 0; }
    js_s64 x = js_tonum(a), y = js_tonum(b);
    if (js_isnan(x) || js_isnan(y)) return 2;
    return x < y ? -1 : x > y ? 1 : 0;
}
static int js_arr_index(struct jv key, u32 *out) {                    /* is key a non-negative integer number? */
    if (key.t != JT_NUM || key.n < 0 || (key.n & (JS_ONE - 1)) || (key.n >> 32) > 0x7FFFFF) return 0;
    *out = (u32)(key.n >> 32); return 1;
}
static u32 js_key_of(struct jv key) {                                 /* property key (interned) for any value */
    struct jv s = js_tostr(key);
    return js_intern_c(js_cs(s));
}

/* ============================ properties ============================= */
enum { NS_MATH = 1, NS_JSON, NS_CONSOLE, NS_OBJECT, NS_ARRAY, NS_NUMBER, NS_STRING, NS_DATE, NS_BOOLEAN, NS_ERROR, NS_PERF, NS_STORAGE };
static struct jv js_storage;        /* localStorage / sessionStorage: kept while the page is open */
enum { NAT_PARSEINT = 1, NAT_PARSEFLOAT, NAT_ISNAN, NAT_ALERT, NAT_SETTIMEOUT, NAT_SETINTERVAL, NAT_CLEARTIMER, NAT_PROMPT, NAT_CONFIRM, NAT_ISFINITE, NAT_RAF };
struct jtimer { struct jv fn; u32 due, period; int id; u8 on; };
static struct jtimer js_timers[JS_MAXTIMERS]; static int js_timer_id;

static struct jv js_getprop(struct jv o, u32 key) {
    const char *name = js_heap + key;
    switch (o.t) {
    case JT_OBJ: return js_obj_get(o, key);
    case JT_ARR: if (key == k_length) return js_num_i((int)js_arrs[o.n].len); return js_undef;
    case JT_STR: if (key == k_length) return js_num_i(js_len(js_cs(o))); return js_undef;
    case JT_NS:
        if (o.n == NS_MATH) {
            if (!js_cmp(name, "PI")) return js_mk(JT_NUM, 13493037704LL);             /* 3.14159265359 * 2^32 */
            if (!js_cmp(name, "E")) return js_mk(JT_NUM, 11674931555LL);
            if (!js_cmp(name, "SQRT2")) return js_mk(JT_NUM, 6074001000LL);
        } else if (o.n == NS_NUMBER) {
            if (!js_cmp(name, "MAX_SAFE_INTEGER") || !js_cmp(name, "MAX_VALUE")) return js_num_i(0x7FFFFFFF);
            if (!js_cmp(name, "MIN_SAFE_INTEGER") || !js_cmp(name, "MIN_VALUE")) return js_num_i(-0x7FFFFFFF);
            if (!js_cmp(name, "POSITIVE_INFINITY")) return js_mk(JT_NUM, JS_INF);
            if (!js_cmp(name, "NEGATIVE_INFINITY")) return js_mk(JT_NUM, -JS_INF);
            if (!js_cmp(name, "NaN")) return js_mk(JT_NUM, JS_NAN);
            if (!js_cmp(name, "EPSILON")) return js_mk(JT_NUM, 1);
        } else if (o.n == NS_STORAGE && key == k_length) { int c = 0; for (u32 p = js_objs[js_storage.n].first; p; p = js_props[p].next) c++; return js_num_i(c); }
        else if (o.n == NS_STORAGE) { u32 p = js_find_prop(js_storage, key); return p ? js_props[p].v : js_undef; }
        return js_undef;
    case JT_ELEM: case JT_DOC: case JT_STYLE: case JT_WIN: case JT_LOC: {
        struct jv r = js_undef;
        if (js_host && js_host->get && js_host->get(o, name, &r)) return r;
        if (o.t == JT_WIN) { struct jvar *v = js_lookup(0, key); if (v) return v->v; }
        return js_undef;
    }
    case JT_UNDEF: case JT_NULL: {
        char m[100]; int k = 0; const char *p = "Cannot read properties of ";
        while (*p) m[k++] = *p++;
        p = o.t == JT_UNDEF ? "undefined" : "null"; while (*p) m[k++] = *p++;
        p = " (reading '"; while (*p) m[k++] = *p++;
        for (int i = 0; name[i] && k < 90; i++) m[k++] = name[i];
        m[k++] = '\''; m[k++] = ')'; m[k] = 0;
        return js_throw_err("TypeError", m);
    }
    default: return js_undef;
    }
}
static void js_setprop(struct jv o, u32 key, struct jv v) {
    const char *name = js_heap + key;
    switch (o.t) {
    case JT_OBJ: js_obj_set(o, key, v); break;
    case JT_ARR:
        if (key == k_length) {
            js_s64 n = js_tonum(v); u32 nl = n < 0 || js_isnan(n) ? 0 : (u32)(n >> 32);
            if (nl > 100000) { js_throw_err("RangeError", "Invalid array length"); break; }
            struct jarr *a = &js_arrs[o.n];
            if (nl <= a->len) a->len = nl; else js_arr_set(o, nl - 1, js_undef);
        }
        break;
    case JT_ELEM: case JT_DOC: case JT_STYLE: case JT_WIN: case JT_LOC:
        if (js_host && js_host->set && js_host->set(o, name, v)) break;
        if (o.t == JT_WIN) js_declare(0, key, v);
        break;
    case JT_UNDEF: case JT_NULL: js_throw_err("TypeError", "Cannot set properties of undefined or null"); break;
    default: break;
    }
}
static struct jv js_eval(u32 n);
static int js_exec(u32 n);
static int js_exec_list(u32 head);
static struct jv js_callfn(struct jv f, struct jv thisv, struct jv *args, int nargs);

/* ======================= timers and host calls ======================= */
static int js_timer_add(struct jv fn, int ms, int repeat) {
    for (int i = 0; i < JS_MAXTIMERS; i++) if (!js_timers[i].on) {
        if (ms < 1) ms = 1;
        js_timers[i].fn = fn; js_timers[i].due = (js_host && js_host->now_ms ? js_host->now_ms() : 0) + (u32)ms; js_timers[i].period = repeat ? (u32)ms : 0;
        js_timers[i].id = ++js_timer_id; js_timers[i].on = 1;
        return js_timers[i].id;
    }
    return 0;
}
static void js_timer_clear(int id) { for (int i = 0; i < JS_MAXTIMERS; i++) if (js_timers[i].on && js_timers[i].id == id) js_timers[i].on = 0; }
static int js_timers_active(void) { for (int i = 0; i < JS_MAXTIMERS; i++) if (js_timers[i].on) return 1; return 0; }
static void js_say(const char *s, int alert) {
    if (alert) { if (js_host && js_host->alert) js_host->alert(s); }
    else if (js_host && js_host->console) js_host->console(s);
}
static struct jv js_native(int id, struct jv *a, int n) {
    struct jv u = js_undef;
    switch (id) {
    case NAT_PARSEINT: {
        if (n < 1) return js_mk(JT_NUM, JS_NAN);
        struct jv sv = js_tostr(a[0]); const char *s = js_cs(sv); int len = js_len(s), i = 0, radix = n > 1 ? js_toint(js_tonum(a[1])) : 10, neg = 0;
        while (i < len && js_isspace(s[i])) i++;
        if (i < len && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
        if ((radix == 0 || radix == 16) && i + 1 < len && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) { i += 2; radix = 16; }
        if (radix == 0) radix = 10;
        if (radix < 2 || radix > 36) return js_mk(JT_NUM, JS_NAN);
        js_s64 v = 0; int any = 0;
        while (i < len) { int c = s[i], d = js_isdig(c) ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'z' ? (c | 32) - 'a' + 10 : 99; if (d >= radix) break; if (v < 0x7FFFFFFF) v = v * radix + d; i++; any = 1; }
        if (!any) return js_mk(JT_NUM, JS_NAN);
        return js_num_i((int)(neg ? -v : v));
    }
    case NAT_PARSEFLOAT: { if (n < 1) return js_mk(JT_NUM, JS_NAN); struct jv sv = js_tostr(a[0]); js_s64 v = js_parse_num(js_cs(sv), js_len(js_cs(sv)), NULL, 1); return js_mk(JT_NUM, v); }
    case NAT_ISNAN: return js_bool(n < 1 || js_isnan(js_tonum(a[0])));
    case NAT_ISFINITE: { js_s64 v = n ? js_tonum(a[0]) : JS_NAN; return js_bool(!js_isnan(v) && v != JS_INF && v != -JS_INF); }
    case NAT_ALERT: { struct jv s = n ? js_tostr(a[0]) : js_s(""); js_say(js_cs(s), 1); return u; }
    case NAT_SETTIMEOUT: case NAT_SETINTERVAL: case NAT_RAF: {
        if (n < 1 || (a[0].t != JT_FUN && a[0].t != JT_NAT)) return js_num_i(0);
        int ms = id == NAT_RAF ? 33 : n > 1 ? js_toint(js_tonum(a[1])) : 0;
        return js_num_i(js_timer_add(a[0], ms, id == NAT_SETINTERVAL));
    }
    case NAT_CLEARTIMER: if (n) js_timer_clear(js_toint(js_tonum(a[0]))); return u;
    case NAT_PROMPT: return js_mk(JT_NULL, 0);
    case NAT_CONFIRM: return js_bool(1);
    }
    return u;
}

/* ========================= built-in methods ========================== */
static int js_argi(struct jv *a, int n, int i, int def) { return i < n && a[i].t != JT_UNDEF ? js_toint(js_tonum(a[i])) : def; }
static struct jv js_substr(struct jv s, int from, int to) {            /* [from, to) already clamped */
    const char *p = js_cs(s);
    if (to <= from) return js_s("");
    return js_mk(JT_STR, js_newstr(p + from, to - from));
}
static int js_clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int js_find(const char *h, int hl, const char *nd, int nl, int from) {
    for (int i = from < 0 ? 0 : from; i + nl <= hl; i++) if (!memcmp(h + i, nd, (size_t)nl)) return i;
    return -1;
}
static struct jv js_str_method(struct jv o, const char *m, struct jv *a, int n, int *ok) {
    const char *s = js_cs(o); int len = js_len(s); *ok = 1;
    if (!js_cmp(m, "charAt")) { int i = js_argi(a, n, 0, 0); return i >= 0 && i < len ? js_mk(JT_STR, js_newstr(s + i, 1)) : js_s(""); }
    if (!js_cmp(m, "charCodeAt") || !js_cmp(m, "codePointAt")) { int i = js_argi(a, n, 0, 0); return i >= 0 && i < len ? js_num_i((unsigned char)s[i]) : js_mk(JT_NUM, JS_NAN); }
    if (!js_cmp(m, "at")) { int i = js_argi(a, n, 0, 0); if (i < 0) i += len; return i >= 0 && i < len ? js_mk(JT_STR, js_newstr(s + i, 1)) : js_undef; }
    if (!js_cmp(m, "indexOf") || !js_cmp(m, "includes") || !js_cmp(m, "lastIndexOf")) {
        struct jv nd = n ? js_tostr(a[0]) : js_s("undefined"); const char *ns = js_cs(nd); int nl = js_len(ns), r = -1;
        if (!js_cmp(m, "lastIndexOf")) { for (int i = len - nl; i >= 0; i--) if (!memcmp(s + i, ns, (size_t)nl)) { r = i; break; } }
        else r = js_find(s, len, ns, nl, js_argi(a, n, 1, 0));
        if (m[0] == 'i' && m[1] == 'n' && m[2] == 'c') return js_bool(r >= 0);
        return js_num_i(r);
    }
    if (!js_cmp(m, "startsWith") || !js_cmp(m, "endsWith")) {
        struct jv nd = n ? js_tostr(a[0]) : js_s(""); const char *ns = js_cs(nd); int nl = js_len(ns);
        if (m[0] == 's') return js_bool(nl <= len && !memcmp(s, ns, (size_t)nl));
        return js_bool(nl <= len && !memcmp(s + len - nl, ns, (size_t)nl));
    }
    if (!js_cmp(m, "slice")) {
        int f = js_argi(a, n, 0, 0), t = js_argi(a, n, 1, len);
        if (f < 0) f += len;
        if (t < 0) t += len;
        return js_substr(o, js_clampi(f, 0, len), js_clampi(t, 0, len));
    }
    if (!js_cmp(m, "substring")) {
        int f = js_clampi(js_argi(a, n, 0, 0), 0, len), t = js_clampi(js_argi(a, n, 1, len), 0, len);
        if (f > t) { int x = f; f = t; t = x; }
        return js_substr(o, f, t);
    }
    if (!js_cmp(m, "substr")) {
        int f = js_argi(a, n, 0, 0); if (f < 0) f += len; f = js_clampi(f, 0, len);
        int l = js_argi(a, n, 1, len - f);
        return js_substr(o, f, js_clampi(f + l, f, len));
    }
    if (!js_cmp(m, "toUpperCase") || !js_cmp(m, "toLowerCase") || !js_cmp(m, "toLocaleUpperCase") || !js_cmp(m, "toLocaleLowerCase")) {
        int up = m[2 + (m[2] == 'L' ? 5 : 0)] == 'U' || !js_cmp(m, "toUpperCase") || !js_cmp(m, "toLocaleUpperCase");
        u32 r = js_alloc((u32)len + 1); if (!r) return js_undef;
        for (int i = 0; i < len; i++) { char c = s[i]; if (up && c >= 'a' && c <= 'z') c = (char)(c - 32); else if (!up && c >= 'A' && c <= 'Z') c = (char)(c + 32); js_heap[r + (u32)i] = c; }
        js_heap[r + (u32)len] = 0; js_htop = r + (u32)len + 1; return js_mk(JT_STR, len ? r : 0);
    }
    if (!js_cmp(m, "trim") || !js_cmp(m, "trimStart") || !js_cmp(m, "trimEnd")) {
        int f = 0, t = len;
        if (m[4] != 'E') while (f < t && js_isspace(s[f])) f++;
        if (m[4] != 'S') while (t > f && js_isspace(s[t - 1])) t--;
        return js_substr(o, f, t);
    }
    if (!js_cmp(m, "repeat")) {
        int c = js_argi(a, n, 0, 0); if (c < 0 || (long long)c * len > 100000) return js_throw_err("RangeError", "Invalid string length");
        u32 r = js_alloc((u32)(c * len) + 1); if (!r) return js_undef;
        for (int i = 0; i < c; i++) memcpy(js_heap + r + (u32)(i * len), s, (size_t)len);
        js_heap[r + (u32)(c * len)] = 0; js_htop = r + (u32)(c * len) + 1; return js_mk(JT_STR, (c && len) ? r : 0);
    }
    if (!js_cmp(m, "padStart") || !js_cmp(m, "padEnd")) {
        int tl = js_argi(a, n, 0, 0); struct jv pv = n > 1 ? js_tostr(a[1]) : js_s(" "); const char *pad = js_cs(pv); int pl = js_len(pad);
        if (tl <= len || !pl || tl > 100000) return o;
        u32 r = js_alloc((u32)tl + 1); if (!r) return js_undef;
        int fill = tl - len, at = m[3] == 'S' ? 0 : len;
        for (int i = 0; i < fill; i++) js_heap[r + (u32)(m[3] == 'S' ? i : len + i)] = pad[i % pl];
        memcpy(js_heap + r + (u32)(m[3] == 'S' ? fill : 0), s, (size_t)len); (void)at;
        js_heap[r + (u32)tl] = 0; js_htop = r + (u32)tl + 1; return js_mk(JT_STR, r);
    }
    if (!js_cmp(m, "concat")) { struct jv r = o; for (int i = 0; i < n; i++) r = js_concat(r, js_tostr(a[i])); return r; }
    if (!js_cmp(m, "toString") || !js_cmp(m, "valueOf")) return o;
    if (!js_cmp(m, "localeCompare")) { struct jv t = n ? js_tostr(a[0]) : js_s(""); int c = js_cmp(s, js_cs(t)); return js_num_i(c < 0 ? -1 : c > 0 ? 1 : 0); }
    if (!js_cmp(m, "split")) {
        struct jv r = js_new_arr(8); if (js_fatal) return js_undef;
        int lim = js_argi(a, n, 1, 100000);
        if (n < 1 || a[0].t == JT_UNDEF) { js_arr_push(r, o); return r; }
        struct jv sepv = js_tostr(a[0]); const char *sep = js_cs(sepv); int sl = js_len(sep);
        if (!sl) { for (int i = 0; i < len && i < lim; i++) js_arr_push(r, js_mk(JT_STR, js_newstr(s + i, 1))); return r; }
        int st = 0;
        for (;;) {
            int f = js_find(s, len, sep, sl, st);
            if ((int)js_arrs[r.n].len >= lim) break;
            if (f < 0) { js_arr_push(r, js_substr(o, st, len)); break; }
            js_arr_push(r, js_substr(o, st, f)); st = f + sl;
            if (js_fatal) break;
        }
        return r;
    }
    if (!js_cmp(m, "replace") || !js_cmp(m, "replaceAll")) {
        int all = m[7] == 'A';
        struct jv fv = n ? js_tostr(a[0]) : js_s("undefined"); const char *f = js_cs(fv); int fl = js_len(f);
        struct jv res = js_s(""); int st = 0;
        if (n < 2) return o;
        for (;;) {
            int at = fl ? js_find(s, len, f, fl, st) : (st == 0 ? 0 : -1);
            if (at < 0) break;
            res = js_concat(res, js_substr(o, st, at));
            struct jv rep;
            if (a[1].t == JT_FUN || a[1].t == JT_NAT) { struct jv arg = js_substr(o, at, at + fl); rep = js_tostr(js_callfn(a[1], js_undef, &arg, 1)); }
            else rep = js_tostr(a[1]);
            if (js_thrown || js_fatal) return js_undef;
            res = js_concat(res, rep); st = at + fl;
            if (!all) break;
            if (!fl) break;
        }
        return js_concat(res, js_substr(o, st, len));
    }
    *ok = 0; return js_undef;
}
static struct jv js_num_method(struct jv o, const char *m, struct jv *a, int n, int *ok) {
    *ok = 1;
    if (!js_cmp(m, "toFixed")) {
        int d = js_clampi(js_argi(a, n, 0, 0), 0, 8);
        if (js_isnan(o.n) || o.n == JS_INF || o.n == -JS_INF) return js_tostr(o);
        js_u64 u = o.n < 0 ? (js_u64)(-o.n) : (js_u64)o.n, pw = 1; for (int i = 0; i < d; i++) pw *= 10;
        js_u64 ip = u >> 32, fp = u & 0xFFFFFFFFu;
        js_u64 scaled = ip * pw + ((fp * pw + 0x80000000u) >> 32);
        char b[40]; int bo = 0; js_u64 whole = js_udiv(scaled, pw, 0), frac = scaled - whole * pw;
        if (o.n < 0 && scaled) b[bo++] = '-';
        char t[24]; int tl = 0; u32 w = (u32)whole; if (!w) t[tl++] = '0'; while (w) { t[tl++] = (char)('0' + w % 10); w /= 10; }
        while (tl) b[bo++] = t[--tl];
        if (d) { b[bo++] = '.'; for (int i = d - 1; i >= 0; i--) { js_u64 p10 = 1; for (int k = 0; k < i; k++) p10 *= 10; b[bo++] = (char)('0' + (u32)(js_udiv(frac, p10, 0) % 10)); } }
        b[bo] = 0; return js_str(b);
    }
    if (!js_cmp(m, "toString")) {
        int radix = js_argi(a, n, 0, 10);
        if (radix == 10 || radix < 2 || radix > 36 || (o.n & (JS_ONE - 1)) || js_isnan(o.n)) return js_tostr(o);
        int v = js_toint(o.n), neg = v < 0; u32 u = neg ? (u32)(-v) : (u32)v; char t[40], b[40]; int tl = 0, bo = 0;
        if (!u) t[tl++] = '0';
        while (u) { u32 d = u % (u32)radix; t[tl++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (u32)radix; }
        if (neg) b[bo++] = '-';
        while (tl) b[bo++] = t[--tl];
        b[bo] = 0; return js_str(b);
    }
    if (!js_cmp(m, "valueOf")) return o;
    if (!js_cmp(m, "toLocaleString")) return js_tostr(o);
    *ok = 0; return js_undef;
}
/* array callbacks: fn(element, index, array) */
static struct jv js_cb3(struct jv fn, struct jv el, u32 i, struct jv arr) { struct jv args[3]; args[0] = el; args[1] = js_num_i((int)i); args[2] = arr; return js_callfn(fn, js_undef, args, 3); }
static int js_is_fn(struct jv v) { return v.t == JT_FUN || v.t == JT_NAT; }
static void js_sort_arr(struct jv arr, struct jv cmp) {
    struct jarr *a = &js_arrs[arr.n]; u32 len = a->len;
    for (u32 i = 1; i < len && !js_thrown && !js_fatal; i++) {
        struct jv key = js_vpool[a->data + i]; u32 j = i;
        while (j > 0) {
            struct jv prev = js_vpool[a->data + j - 1]; int gt;
            if (key.t == JT_UNDEF) gt = 0;
            else if (prev.t == JT_UNDEF) gt = 1;
            else if (js_is_fn(cmp)) { struct jv ar[2]; ar[0] = prev; ar[1] = key; gt = js_tonum(js_callfn(cmp, js_undef, ar, 2)) > 0; if (js_thrown || js_fatal) return; }
            else gt = js_cmp(js_cs(js_tostr(prev)), js_cs(js_tostr(key))) > 0;
            if (!gt) break;
            js_vpool[a->data + j] = prev; j--;
        }
        js_vpool[a->data + j] = key;
    }
}
static struct jv js_arr_method(struct jv o, const char *m, struct jv *a, int n, int *ok) {
    struct jarr *ar = &js_arrs[o.n]; *ok = 1;
    if (!js_cmp(m, "push")) { for (int i = 0; i < n; i++) js_arr_push(o, a[i]); return js_num_i((int)js_arrs[o.n].len); }
    if (!js_cmp(m, "pop")) { if (!ar->len) return js_undef; return js_vpool[ar->data + --ar->len]; }
    if (!js_cmp(m, "shift")) {
        if (!ar->len) return js_undef;
        struct jv r = js_vpool[ar->data]; memmove(&js_vpool[ar->data], &js_vpool[ar->data + 1], (ar->len - 1) * sizeof(struct jv)); ar->len--; return r;
    }
    if (!js_cmp(m, "unshift")) {
        for (int i = n - 1; i >= 0; i--) { js_arr_push(o, js_undef); ar = &js_arrs[o.n]; if (js_fatal) return js_undef; memmove(&js_vpool[ar->data + 1], &js_vpool[ar->data], (ar->len - 1) * sizeof(struct jv)); js_vpool[ar->data] = a[i]; }
        return js_num_i((int)ar->len);
    }
    if (!js_cmp(m, "join")) { struct jv sv = n && a[0].t != JT_UNDEF ? js_tostr(a[0]) : js_s(","); return js_join(o, js_cs(sv)); }
    if (!js_cmp(m, "toString")) return js_join(o, ",");
    if (!js_cmp(m, "indexOf") || !js_cmp(m, "includes") || !js_cmp(m, "lastIndexOf")) {
        int r = -1; struct jv x = n ? a[0] : js_undef;
        if (m[0] == 'l') { for (int i = (int)ar->len - 1; i >= 0; i--) if (js_seq(js_vpool[ar->data + (u32)i], x)) { r = i; break; } }
        else for (u32 i = (u32)js_clampi(js_argi(a, n, 1, 0), 0, (int)ar->len); i < ar->len; i++) if (js_seq(js_vpool[ar->data + i], x)) { r = (int)i; break; }
        if (m[0] == 'i' && m[2] == 'c') return js_bool(r >= 0);
        return js_num_i(r);
    }
    if (!js_cmp(m, "slice")) {
        int len = (int)ar->len, f = js_argi(a, n, 0, 0), t = js_argi(a, n, 1, len);
        if (f < 0) f += len;
        if (t < 0) t += len;
        f = js_clampi(f, 0, len); t = js_clampi(t, 0, len);
        struct jv r = js_new_arr((u32)(t > f ? t - f : 0) + 1); if (js_fatal) return js_undef;
        for (int i = f; i < t; i++) js_arr_push(r, js_vpool[js_arrs[o.n].data + (u32)i]);
        return r;
    }
    if (!js_cmp(m, "splice")) {
        int len = (int)ar->len, f = js_argi(a, n, 0, 0); if (f < 0) f += len; f = js_clampi(f, 0, len);
        int dc = n < 2 ? len - f : js_clampi(js_argi(a, n, 1, 0), 0, len - f), ins = n > 2 ? n - 2 : 0;
        struct jv removed = js_new_arr((u32)dc + 1), rest = js_new_arr((u32)(len - f - dc) + 1); if (js_fatal) return js_undef;
        for (int i = 0; i < dc; i++) js_arr_push(removed, js_vpool[js_arrs[o.n].data + (u32)(f + i)]);
        for (int i = f + dc; i < len; i++) js_arr_push(rest, js_vpool[js_arrs[o.n].data + (u32)i]);
        js_arrs[o.n].len = (u32)f;
        for (int i = 0; i < ins; i++) js_arr_push(o, a[2 + i]);
        for (u32 i = 0; i < js_arrs[rest.n].len; i++) js_arr_push(o, js_vpool[js_arrs[rest.n].data + i]);
        return removed;
    }
    if (!js_cmp(m, "concat")) {
        struct jv r = js_new_arr(ar->len + 4); if (js_fatal) return js_undef;
        for (u32 i = 0; i < js_arrs[o.n].len; i++) js_arr_push(r, js_vpool[js_arrs[o.n].data + i]);
        for (int k = 0; k < n; k++) {
            if (a[k].t == JT_ARR) for (u32 i = 0; i < js_arrs[a[k].n].len; i++) js_arr_push(r, js_vpool[js_arrs[a[k].n].data + i]);
            else js_arr_push(r, a[k]);
        }
        return r;
    }
    if (!js_cmp(m, "reverse")) {
        for (u32 i = 0, j = ar->len; i + 1 < j; i++, j--) { struct jv t = js_vpool[ar->data + i]; js_vpool[ar->data + i] = js_vpool[ar->data + j - 1]; js_vpool[ar->data + j - 1] = t; }
        return o;
    }
    if (!js_cmp(m, "sort")) { js_sort_arr(o, n ? a[0] : js_undef); return o; }
    if (!js_cmp(m, "fill")) { for (u32 i = 0; i < ar->len; i++) js_vpool[ar->data + i] = n ? a[0] : js_undef; return o; }
    if (!js_cmp(m, "forEach") || !js_cmp(m, "map") || !js_cmp(m, "filter") || !js_cmp(m, "find") || !js_cmp(m, "findIndex") ||
        !js_cmp(m, "some") || !js_cmp(m, "every") || !js_cmp(m, "reduce") || !js_cmp(m, "findLast")) {
        if (n < 1 || !js_is_fn(a[0])) return js_throw_err("TypeError", "callback is not a function");
        int is_map = m[0] == 'm', is_filter = m[0] == 'f' && m[1] == 'i' && m[2] == 'l', is_find = m[0] == 'f' && m[1] == 'i' && m[2] == 'n' && m[4] != 'I';
        int is_findidx = !js_cmp(m, "findIndex"), is_some = m[0] == 's', is_every = m[0] == 'e', is_reduce = m[0] == 'r';
        struct jv res = is_map || is_filter ? js_new_arr(ar->len + 1) : js_undef; if (js_fatal) return js_undef;
        struct jv acc = js_undef; u32 start = 0;
        if (is_reduce) {
            if (n > 1) acc = a[1]; else { if (!ar->len) return js_throw_err("TypeError", "Reduce of empty array with no initial value"); acc = js_vpool[ar->data]; start = 1; }
        }
        for (u32 i = start; i < js_arrs[o.n].len; i++) {
            struct jv el = js_vpool[js_arrs[o.n].data + i], r;
            if (is_reduce) { struct jv ar4[4]; ar4[0] = acc; ar4[1] = el; ar4[2] = js_num_i((int)i); ar4[3] = o; acc = js_callfn(a[0], js_undef, ar4, 4); if (js_thrown || js_fatal) return js_undef; continue; }
            r = js_cb3(a[0], el, i, o);
            if (js_thrown || js_fatal) return js_undef;
            if (is_map) js_arr_push(res, r);
            else if (is_filter) { if (js_truthy(r)) js_arr_push(res, el); }
            else if (is_find) { if (js_truthy(r)) return el; }
            else if (is_findidx) { if (js_truthy(r)) return js_num_i((int)i); }
            else if (is_some) { if (js_truthy(r)) return js_bool(1); }
            else if (is_every) { if (!js_truthy(r)) return js_bool(0); }
        }
        if (is_map || is_filter) return res;
        if (is_findidx) return js_num_i(-1);
        if (is_some) return js_bool(0);
        if (is_every) return js_bool(1);
        if (is_reduce) return acc;
        return js_undef;
    }
    *ok = 0; return js_undef;
}
/* ---- JSON.stringify ---- */
static char js_sb[65536]; static int js_sbn, js_sb_over;
static void js_sb_put(const char *s, int n) { if (js_sbn + n >= (int)sizeof js_sb) { js_sb_over = 1; return; } memcpy(js_sb + js_sbn, s, (size_t)n); js_sbn += n; }
static void js_sb_str(const char *s) {
    js_sb_put("\"", 1);
    for (; *s; s++) {
        char c = *s;
        if (c == '"') js_sb_put("\\\"", 2); else if (c == '\\') js_sb_put("\\\\", 2); else if (c == '\n') js_sb_put("\\n", 2);
        else if (c == '\t') js_sb_put("\\t", 2); else if (c == '\r') js_sb_put("\\r", 2); else js_sb_put(&c, 1);
    }
    js_sb_put("\"", 1);
}
static void js_json(struct jv v, int depth, int indent, int level) {
    char b[40];
    if (depth > 10) { js_sb_put("null", 4); return; }
    switch (v.t) {
    case JT_NULL: case JT_UNDEF: case JT_FUN: case JT_NAT: js_sb_put("null", 4); return;
    case JT_BOOL: js_sb_put(v.n ? "true" : "false", v.n ? 4 : 5); return;
    case JT_NUM: if (js_isnan(v.n) || v.n == JS_INF || v.n == -JS_INF) js_sb_put("null", 4); else { js_fmt_num(v.n, b, sizeof b); js_sb_put(b, js_len(b)); } return;
    case JT_STR: js_sb_str(js_cs(v)); return;
    case JT_ARR: {
        u32 len = js_arrs[v.n].len; js_sb_put("[", 1);
        for (u32 i = 0; i < len; i++) {
            if (i) js_sb_put(",", 1);
            if (indent) { js_sb_put("\n", 1); for (int k = 0; k < (level + 1) * indent; k++) js_sb_put(" ", 1); }
            js_json(js_vpool[js_arrs[v.n].data + i], depth + 1, indent, level + 1);
        }
        if (indent && len) { js_sb_put("\n", 1); for (int k = 0; k < level * indent; k++) js_sb_put(" ", 1); }
        js_sb_put("]", 1); return;
    }
    case JT_OBJ: {
        int first = 1; js_sb_put("{", 1);
        for (u32 p = js_objs[v.n].first; p; p = js_props[p].next) {
            if (js_props[p].v.t == JT_UNDEF || js_is_fn(js_props[p].v)) continue;
            if (!first) js_sb_put(",", 1);
            first = 0;
            if (indent) { js_sb_put("\n", 1); for (int k = 0; k < (level + 1) * indent; k++) js_sb_put(" ", 1); }
            js_sb_str(js_heap + js_props[p].key); js_sb_put(indent ? ": " : ":", indent ? 2 : 1);
            js_json(js_props[p].v, depth + 1, indent, level + 1);
        }
        if (indent && !first) { js_sb_put("\n", 1); for (int k = 0; k < level * indent; k++) js_sb_put(" ", 1); }
        js_sb_put("}", 1); return;
    }
    default: js_sb_put("{}", 2); return;
    }
}
static js_s64 js_trig(js_s64 x, int cosine) {
    if (js_isnan(x) || x == JS_INF || x == -JS_INF) return JS_NAN;
    const js_s64 PI = 13493037704LL, TWO_PI = 26986075409LL, HALF_PI = 6746518852LL;
    x = js_nmod(x, TWO_PI); if (x < 0) x += TWO_PI;
    if (cosine) x += HALF_PI;
    if (x >= TWO_PI) x -= TWO_PI;
    int neg = 0;
    if (x > PI) { x -= PI; neg = 1; }                                   /* sin(x) = -sin(x - pi) */
    if (x > HALF_PI) x = PI - x;                                        /* now 0..pi/2 */
    js_s64 x2 = js_nmul(x, x), term = x, sum = x;
    for (int k = 1; k <= 7; k++) {                                      /* Taylor: x - x^3/3! + x^5/5! ... */
        term = js_ndiv(js_nmul(term, x2), (js_s64)((2 * k) * (2 * k + 1)) << 32);
        sum += (k & 1) ? -term : term;
    }
    return neg ? -sum : sum;
}
static struct jv js_ns_call(int ns, const char *m, struct jv *a, int n, int *ok) {
    *ok = 1;
    js_s64 x = n ? js_tonum(a[0]) : JS_NAN;
    if (ns == NS_MATH) {
        if (!js_cmp(m, "floor")) return js_mk(JT_NUM, js_floor_n(x));
        if (!js_cmp(m, "ceil")) return js_mk(JT_NUM, js_isnan(x) ? x : -js_floor_n(-x));
        if (!js_cmp(m, "round")) return js_mk(JT_NUM, js_isnan(x) ? x : js_floor_n(x + (JS_ONE >> 1)));
        if (!js_cmp(m, "trunc")) return js_mk(JT_NUM, js_isnan(x) ? x : x < 0 ? -js_floor_n(-x) : js_floor_n(x));
        if (!js_cmp(m, "abs")) return js_mk(JT_NUM, js_isnan(x) ? x : x < 0 ? -x : x);
        if (!js_cmp(m, "sign")) return js_mk(JT_NUM, js_isnan(x) ? x : x > 0 ? JS_ONE : x < 0 ? -JS_ONE : 0);
        if (!js_cmp(m, "sqrt")) return js_mk(JT_NUM, js_sqrt_n(x));
        if (!js_cmp(m, "sin")) return js_mk(JT_NUM, js_trig(x, 0));
        if (!js_cmp(m, "cos")) return js_mk(JT_NUM, js_trig(x, 1));
        if (!js_cmp(m, "tan")) return js_mk(JT_NUM, js_ndiv(js_trig(x, 0), js_trig(x, 1)));
        if (!js_cmp(m, "random")) { u32 r = js_host && js_host->rnd ? js_host->rnd() : 12345; return js_mk(JT_NUM, (js_s64)(r & 0x7FFFFFFFu) << 1); }
        if (!js_cmp(m, "min") || !js_cmp(m, "max")) {
            js_s64 r = m[1] == 'i' ? JS_INF : -JS_INF;
            for (int i = 0; i < n; i++) { js_s64 v = js_tonum(a[i]); if (js_isnan(v)) return js_mk(JT_NUM, JS_NAN); if (m[1] == 'i' ? v < r : v > r) r = v; }
            return js_mk(JT_NUM, r);
        }
        if (!js_cmp(m, "pow")) {
            js_s64 e = n > 1 ? js_tonum(a[1]) : JS_NAN;
            if (js_isnan(x) || js_isnan(e)) return js_mk(JT_NUM, JS_NAN);
            if (e == (JS_ONE >> 1)) return js_mk(JT_NUM, js_sqrt_n(x));
            if (e & (JS_ONE - 1)) return js_mk(JT_NUM, JS_NAN);
            int k = js_toint(e); int neg = k < 0; if (neg) k = -k; if (k > 64) k = 64;
            js_s64 r = JS_ONE; for (int i = 0; i < k; i++) r = js_nmul(r, x);
            return js_mk(JT_NUM, neg ? js_ndiv(JS_ONE, r) : r);
        }
        if (!js_cmp(m, "hypot")) { js_s64 y = n > 1 ? js_tonum(a[1]) : 0; return js_mk(JT_NUM, js_sqrt_n(js_nadd(js_nmul(x, x), js_nmul(y, y)))); }
    } else if (ns == NS_JSON) {
        if (!js_cmp(m, "stringify")) {
            if (n < 1 || a[0].t == JT_UNDEF || js_is_fn(a[0])) return js_undef;
            js_sbn = 0; js_sb_over = 0; js_json(a[0], 0, n > 2 && a[2].t == JT_NUM ? js_clampi(js_toint(a[2].n), 0, 8) : 0, 0);
            if (js_sb_over) return js_throw_err("RangeError", "JSON too large");
            return js_mk(JT_STR, js_newstr(js_sb, js_sbn));
        }
    } else if (ns == NS_CONSOLE) {
        char buf[600]; int o = 0;
        for (int i = 0; i < n && o < 560; i++) {
            struct jv s = a[i].t == JT_OBJ ? (js_sbn = 0, js_sb_over = 0, js_json(a[i], 0, 0, 0), js_mk(JT_STR, js_newstr(js_sb, js_sbn))) : js_tostr(a[i]);
            const char *p = js_cs(s); if (i) buf[o++] = ' ';
            while (*p && o < 590) buf[o++] = *p++;
        }
        buf[o] = 0;
        if (!js_cmp(m, "clear")) return js_undef;
        js_say(buf, 0); return js_undef;
    } else if (ns == NS_OBJECT) {
        if (n && a[0].t == JT_OBJ && (!js_cmp(m, "keys") || !js_cmp(m, "values") || !js_cmp(m, "entries"))) {
            struct jv r = js_new_arr(8); if (js_fatal) return js_undef;
            for (u32 p = js_objs[a[0].n].first; p; p = js_props[p].next) {
                if (m[0] == 'k') js_arr_push(r, js_mk(JT_STR, js_props[p].key));
                else if (m[0] == 'v') js_arr_push(r, js_props[p].v);
                else { struct jv pr = js_new_arr(2); js_arr_push(pr, js_mk(JT_STR, js_props[p].key)); js_arr_push(pr, js_props[p].v); js_arr_push(r, pr); }
            }
            return r;
        }
        if (n && a[0].t == JT_ARR && !js_cmp(m, "keys")) { struct jv r = js_new_arr(8); for (u32 i = 0; i < js_arrs[a[0].n].len; i++) { char b[16]; js_fmt_num((js_s64)i << 32, b, sizeof b); js_arr_push(r, js_str(b)); } return r; }
        if (!js_cmp(m, "assign") && n && a[0].t == JT_OBJ) {
            for (int i = 1; i < n; i++) if (a[i].t == JT_OBJ) for (u32 p = js_objs[a[i].n].first; p; p = js_props[p].next) js_obj_set(a[0], js_props[p].key, js_props[p].v);
            return a[0];
        }
        if (!js_cmp(m, "freeze") || !js_cmp(m, "seal")) return n ? a[0] : js_undef;
        if (!js_cmp(m, "keys") || !js_cmp(m, "values") || !js_cmp(m, "entries")) return js_new_arr(1);
    } else if (ns == NS_ARRAY) {
        if (!js_cmp(m, "isArray")) return js_bool(n && a[0].t == JT_ARR);
        if (!js_cmp(m, "of")) { struct jv r = js_new_arr((u32)n); for (int i = 0; i < n; i++) js_arr_push(r, a[i]); return r; }
        if (!js_cmp(m, "from")) {
            struct jv r = js_new_arr(8); if (js_fatal) return js_undef;
            if (n && a[0].t == JT_STR) { const char *s = js_cs(a[0]); for (int i = 0; s[i]; i++) js_arr_push(r, js_mk(JT_STR, js_newstr(s + i, 1))); }
            else if (n && a[0].t == JT_ARR) for (u32 i = 0; i < js_arrs[a[0].n].len; i++) js_arr_push(r, js_vpool[js_arrs[a[0].n].data + i]);
            if (n > 1 && js_is_fn(a[1])) for (u32 i = 0; i < js_arrs[r.n].len; i++) { struct jv v = js_cb3(a[1], js_vpool[js_arrs[r.n].data + i], i, r); if (js_thrown) return js_undef; js_vpool[js_arrs[r.n].data + i] = v; }
            return r;
        }
    } else if (ns == NS_NUMBER) {
        if (!js_cmp(m, "isInteger")) return js_bool(n && a[0].t == JT_NUM && !js_isnan(x) && !(x & (JS_ONE - 1)));
        if (!js_cmp(m, "isNaN")) return js_bool(n && a[0].t == JT_NUM && js_isnan(x));
        if (!js_cmp(m, "isFinite")) return js_bool(n && a[0].t == JT_NUM && !js_isnan(x) && x != JS_INF && x != -JS_INF);
        if (!js_cmp(m, "parseFloat")) return js_native(NAT_PARSEFLOAT, a, n);
        if (!js_cmp(m, "parseInt")) return js_native(NAT_PARSEINT, a, n);
    } else if (ns == NS_STRING) {
        if (!js_cmp(m, "fromCharCode")) { char b[64]; int o = 0; for (int i = 0; i < n && o < 62; i++) { int c = js_toint(js_tonum(a[i])); b[o++] = (char)(c > 0 && c < 128 ? c : '?'); } b[o] = 0; return js_str(b); }
    } else if (ns == NS_STORAGE) {
        if (!js_cmp(m, "getItem") && n) { u32 p = js_find_prop(js_storage, js_key_of(a[0])); return p ? js_props[p].v : js_mk(JT_NULL, 0); }
        if (!js_cmp(m, "setItem") && n > 1) { js_obj_set(js_storage, js_key_of(a[0]), js_tostr(a[1])); return js_undef; }
        if (!js_cmp(m, "removeItem") && n) { js_obj_del(js_storage, js_key_of(a[0])); return js_undef; }
        if (!js_cmp(m, "clear")) { js_objs[js_storage.n].first = js_objs[js_storage.n].last = 0; return js_undef; }
        if (!js_cmp(m, "key") && n) { int k = js_toint(js_tonum(a[0])); for (u32 p = js_objs[js_storage.n].first; p; p = js_props[p].next) if (k-- == 0) return js_mk(JT_STR, js_props[p].key); return js_mk(JT_NULL, 0); }
    } else if (ns == NS_DATE || ns == NS_PERF) {
        if (!js_cmp(m, "now")) return js_num_i(js_host && js_host->now_ms ? (int)js_host->now_ms() : 0);
    }
    *ok = 0; return js_undef;
}
/* call a built-in method on any value; returns 1 if there is such a method */
static int js_method(struct jv o, const char *m, struct jv *a, int n, struct jv *res) {
    int ok = 0;
    switch (o.t) {
    case JT_STR: *res = js_str_method(o, m, a, n, &ok); break;
    case JT_NUM: *res = js_num_method(o, m, a, n, &ok); break;
    case JT_ARR: *res = js_arr_method(o, m, a, n, &ok); break;
    case JT_NS: *res = js_ns_call((int)o.n, m, a, n, &ok); break;
    case JT_BOOL: if (!js_cmp(m, "toString")) { *res = js_tostr(o); ok = 1; } break;
    case JT_OBJ:
        if (!js_cmp(m, "hasOwnProperty") && n) { *res = js_bool(js_find_prop(o, js_key_of(a[0])) != 0); ok = 1; }
        else if (!js_cmp(m, "toString")) { *res = js_obj_get(o, k_message).t != JT_UNDEF ? js_tostr(js_obj_get(o, k_message)) : js_s("[object Object]"); ok = 1; }
        else if (!js_cmp(m, "preventDefault")) { js_prevent = 1; *res = js_undef; ok = 1; }
        else if (!js_cmp(m, "stopPropagation") || !js_cmp(m, "stopImmediatePropagation")) { *res = js_undef; ok = 1; }
        break;
    case JT_FUN: case JT_NAT:
        if (!js_cmp(m, "call")) { *res = js_callfn(o, n ? a[0] : js_undef, n > 1 ? a + 1 : a, n > 1 ? n - 1 : 0); ok = 1; }
        else if (!js_cmp(m, "apply")) {
            struct jv args[16]; int cnt = 0;
            if (n > 1 && a[1].t == JT_ARR) for (u32 i = 0; i < js_arrs[a[1].n].len && cnt < 16; i++) args[cnt++] = js_vpool[js_arrs[a[1].n].data + i];
            *res = js_callfn(o, n ? a[0] : js_undef, args, cnt); ok = 1;
        }
        break;
    case JT_ELEM: case JT_DOC: case JT_STYLE: case JT_WIN: case JT_LOC:
        if (js_host && js_host->call && js_host->call(o, m, a, n, res)) ok = 1;
        else if (o.t == JT_WIN) {                                      /* window.setTimeout(...) etc. */
            struct jvar *v = js_lookup(0, js_intern_c(m));
            if (v && js_is_fn(v->v)) { *res = js_callfn(v->v, o, a, n); ok = 1; }
        }
        break;
    default: break;
    }
    return ok;
}

/* ============================== evaluator ============================ */
static u32 k_dt;
static int js_date_method(struct jv o, const char *m, struct jv *res) {
    int f[7] = { 2025, 1, 1, 0, 0, 0, 3 };
    if (js_host && js_host->date) js_host->date(f);
    if (!js_cmp(m, "getFullYear")) *res = js_num_i(f[0]);
    else if (!js_cmp(m, "getMonth")) *res = js_num_i(f[1] - 1);
    else if (!js_cmp(m, "getDate")) *res = js_num_i(f[2]);
    else if (!js_cmp(m, "getHours")) *res = js_num_i(f[3]);
    else if (!js_cmp(m, "getMinutes")) *res = js_num_i(f[4]);
    else if (!js_cmp(m, "getSeconds")) *res = js_num_i(f[5]);
    else if (!js_cmp(m, "getDay")) *res = js_num_i(f[6]);
    else if (!js_cmp(m, "getTime") || !js_cmp(m, "valueOf")) *res = js_obj_get(o, k_dt);
    else if (!js_cmp(m, "toLocaleDateString") || !js_cmp(m, "toDateString")) { char b[24]; int n = 0; n += 0; (void)n; b[0] = 0;
        char t[12]; js_fmt_num(js_num_i(f[1]).n, t, sizeof t); int ol = 0; for (int i = 0; t[i]; i++) b[ol++] = t[i]; b[ol++] = '/';
        js_fmt_num(js_num_i(f[2]).n, t, sizeof t); for (int i = 0; t[i]; i++) b[ol++] = t[i]; b[ol++] = '/';
        js_fmt_num(js_num_i(f[0]).n, t, sizeof t); for (int i = 0; t[i]; i++) b[ol++] = t[i]; b[ol] = 0; *res = js_str(b); }
    else if (!js_cmp(m, "toLocaleTimeString") || !js_cmp(m, "toTimeString") || !js_cmp(m, "toString") || !js_cmp(m, "toISOString")) {
        char b[16]; b[0] = (char)('0' + f[3] / 10); b[1] = (char)('0' + f[3] % 10); b[2] = ':'; b[3] = (char)('0' + f[4] / 10); b[4] = (char)('0' + f[4] % 10);
        b[5] = ':'; b[6] = (char)('0' + f[5] / 10); b[7] = (char)('0' + f[5] % 10); b[8] = 0; *res = js_str(b); }
    else return 0;
    return 1;
}

#define JCHK() do { if (js_thrown || js_fatal) return js_undef; } while (0)
static struct jv js_binop(u32 op, struct jv a, struct jv b) {
    switch (op) {
    case '+':
        if (a.t == JT_STR || b.t == JT_STR || a.t >= JT_ARR || b.t >= JT_ARR) return js_concat(js_tostr(a), js_tostr(b));
        return js_mk(JT_NUM, js_nadd(js_tonum(a), js_tonum(b)));
    case '-': return js_mk(JT_NUM, js_nsub(js_tonum(a), js_tonum(b)));
    case '*': return js_mk(JT_NUM, js_nmul(js_tonum(a), js_tonum(b)));
    case '/': return js_mk(JT_NUM, js_ndiv(js_tonum(a), js_tonum(b)));
    case '%': return js_mk(JT_NUM, js_nmod(js_tonum(a), js_tonum(b)));
    case PK2('*', '*'): { struct jv args[2]; args[0] = a; args[1] = b; int ok; return js_ns_call(NS_MATH, "pow", args, 2, &ok); }
    case PK2('=', '='): return js_bool(js_leq(a, b));
    case PK2('!', '='): return js_bool(!js_leq(a, b));
    case PK3('=', '=', '='): return js_bool(js_seq(a, b));
    case PK3('!', '=', '='): return js_bool(!js_seq(a, b));
    case '<': { int c = js_compare(a, b); return js_bool(c == -1); }
    case '>': { int c = js_compare(a, b); return js_bool(c == 1); }
    case PK2('<', '='): { int c = js_compare(a, b); return js_bool(c == -1 || c == 0); }
    case PK2('>', '='): { int c = js_compare(a, b); return js_bool(c == 1 || c == 0); }
    case '&': return js_num_i(js_toint(js_tonum(a)) & js_toint(js_tonum(b)));
    case '|': return js_num_i(js_toint(js_tonum(a)) | js_toint(js_tonum(b)));
    case '^': return js_num_i(js_toint(js_tonum(a)) ^ js_toint(js_tonum(b)));
    case PK2('<', '<'): return js_num_i((int)((u32)js_toint(js_tonum(a)) << (js_toint(js_tonum(b)) & 31)));
    case PK2('>', '>'): return js_num_i(js_toint(js_tonum(a)) >> (js_toint(js_tonum(b)) & 31));
    case PK3('>', '>', '>'): return js_num_i((int)((u32)js_toint(js_tonum(a)) >> (js_toint(js_tonum(b)) & 31)));
    case 'i': {
        if (b.t == JT_OBJ) return js_bool(js_find_prop(b, js_key_of(a)) != 0);
        if (b.t == JT_ARR) { u32 i; return js_bool(js_arr_index(a, &i) && i < js_arrs[b.n].len); }
        return js_throw_err("TypeError", "Cannot use 'in' operator on this value");
    }
    case 'I': return js_bool(0);
    }
    return js_undef;
}
static u32 js_compound_base(u32 op) {                                  /* += -> '+' etc. */
    switch (op) {
    case PK2('+', '='): return '+'; case PK2('-', '='): return '-'; case PK2('*', '='): return '*'; case PK2('/', '='): return '/'; case PK2('%', '='): return '%';
    case PK2('&', '='): return '&'; case PK2('|', '='): return '|'; case PK2('^', '='): return '^';
    case PK3('<', '<', '='): return PK2('<', '<'); case PK3('>', '>', '='): return PK2('>', '>'); case PK4('>', '>', '>', '='): return PK3('>', '>', '>');
    case PK3('*', '*', '='): return PK2('*', '*');
    }
    return 0;
}
#define JCHKV() do { if (js_thrown || js_fatal) return; } while (0)
static void js_assign_to(u32 target, struct jv v) {
    struct jn *t = &js_nodes[target];
    if (t->k == N_IDENT) {
        struct jvar *var = js_lookup(js_scope, t->a);
        if (var) var->v = v; else js_declare(0, t->a, v);
    } else if (t->k == N_MEMBER) {
        struct jv o = js_eval(t->a); JCHKV();
        js_setprop(o, js_nodes[target].b, v);
    } else if (t->k == N_INDEX) {
        struct jv o = js_eval(t->a); JCHKV();
        struct jv key = js_eval(js_nodes[target].b); JCHKV();
        u32 i;
        if (o.t == JT_ARR && js_arr_index(key, &i)) js_arr_set(o, i, v);
        else js_setprop(o, js_key_of(key), v);
    }
}
static int js_eval_args(u32 head, struct jv *args, int max) {
    int n = 0;
    for (u32 a = head; a && !js_thrown && !js_fatal; a = js_nodes[a].next) {
        if (js_nodes[a].k == N_SPREAD) {
            struct jv sv = js_eval(js_nodes[a].a); if (js_thrown || js_fatal) return n;
            if (sv.t == JT_ARR) for (u32 i = 0; i < js_arrs[sv.n].len; i++) { if (n >= max) { js_throw_err("RangeError", "too many arguments"); return n; } args[n++] = js_vpool[js_arrs[sv.n].data + i]; }
            else if (sv.t == JT_STR) { const char *s = js_cs(sv); for (int i = 0; s[i]; i++) { if (n >= max) break; args[n++] = js_mk(JT_STR, js_newstr(s + i, 1)); } }
        } else {
            if (n >= max) { js_throw_err("RangeError", "too many arguments"); return n; }
            args[n++] = js_eval(a);
        }
    }
    return n;
}
static struct jv js_make_error(const char *kind, struct jv msg) {
    struct jv e = js_new_obj(); if (js_fatal) return js_undef;
    js_obj_set(e, k_name, js_str(kind)); js_obj_set(e, k_message, msg);
    return e;
}
static struct jv js_construct_ns(int ns, struct jv *a, int n, int is_new) {
    switch (ns) {
    case NS_STRING: return n ? js_tostr(a[0]) : js_s("");
    case NS_NUMBER: return js_mk(JT_NUM, n ? js_tonum(a[0]) : 0);
    case NS_BOOLEAN: return js_bool(n && js_truthy(a[0]));
    case NS_OBJECT: return js_new_obj();
    case NS_ARRAY: {
        struct jv r = js_new_arr(8); if (js_fatal) return js_undef;
        if (n == 1 && a[0].t == JT_NUM) { int c = js_toint(a[0].n); if (c < 0 || c > 100000) return js_throw_err("RangeError", "Invalid array length"); if (c) js_arr_set(r, (u32)c - 1, js_undef); }
        else for (int i = 0; i < n; i++) js_arr_push(r, a[i]);
        return r;
    }
    case NS_ERROR: return js_make_error("Error", n ? js_tostr(a[0]) : js_s(""));
    case NS_DATE: {
        struct jv o = js_new_obj(); if (js_fatal) return js_undef;
        js_obj_set(o, k_dt, js_num_i(js_host && js_host->now_ms ? (int)js_host->now_ms() : 0));
        (void)is_new; return o;
    }
    }
    return js_undef;
}
static struct jv js_callfn(struct jv f, struct jv thisv, struct jv *args, int nargs) {
    if (f.t == JT_NAT) return js_native((int)f.n, args, nargs);
    if (f.t == JT_NS) return js_construct_ns((int)f.n, args, nargs, 0);
    if (f.t != JT_FUN) return js_throw_err("TypeError", "value is not a function");
    if (js_depth >= JS_MAXDEPTH) return js_throw_err("RangeError", "Maximum call stack size exceeded");
    struct jfunc *fn = &js_funcs[f.n]; u32 node = fn->node;
    u32 sc_top = js_stop, lv_top = js_lvtop, clos = js_ftop, old_scope = js_scope; struct jv old_this = js_this;
    u32 sc = js_newscope(fn->env); if (js_fatal) return js_undef;
    js_depth++; js_scope = sc; js_this = fn->arrow ? fn->this_v : thisv;
    int i = 0;
    for (u32 p = js_nodes[node].a; p && !js_fatal; p = js_nodes[p].next, i++) {
        struct jv v = i < nargs ? args[i] : js_undef;
        if (v.t == JT_UNDEF && js_nodes[p].b) { v = js_eval(js_nodes[p].b); if (js_thrown || js_fatal) break; }
        js_declare(sc, js_nodes[p].a, v);
    }
    struct jv result = js_undef;
    if (!js_thrown && !js_fatal) {
        if (js_nodes[node].op == 1) result = js_eval(js_nodes[node].b);
        else { int rc = js_exec_list(js_nodes[node].b); if (rc == 3) result = js_retval; }
    }
    js_depth--; js_scope = old_scope; js_this = old_this;
    if (js_ftop == clos) { js_stop = sc_top; js_lvtop = lv_top; }        /* no closure was created: nothing can refer to this frame */
    if (js_thrown) return js_undef;
    return result;
}
static struct jv js_eval(u32 n) {
    if (!n) return js_undef;
    if (++js_steps > JS_MAXSTEPS) { js_fail("the script ran too long and was stopped"); return js_undef; }
    if (js_fatal || js_thrown) return js_undef;
    struct jn *nd = &js_nodes[n];
    switch (nd->k) {
    case N_NUM: return js_mk(JT_NUM, js_consts[nd->a]);
    case N_STR: return js_mk(JT_STR, nd->a);
    case N_TRUE: return js_bool(1);
    case N_FALSE: return js_bool(0);
    case N_NULL: return js_mk(JT_NULL, 0);
    case N_UNDEF: return js_undef;
    case N_THIS: return js_this;
    case N_IDENT: {
        struct jvar *v = js_lookup(js_scope, nd->a);
        if (v) return v->v;
        char m[80]; int o = 0; const char *p = js_heap + nd->a; while (*p && o < 60) m[o++] = *p++;
        const char *t = " is not defined"; while (*t) m[o++] = *t++;
        m[o] = 0; return js_throw_err("ReferenceError", m);
    }
    case N_ARRAY: {
        struct jv r = js_new_arr(8); if (js_fatal) return js_undef;
        for (u32 e = nd->a; e; e = js_nodes[e].next) {
            if (js_nodes[e].k == N_SPREAD) {
                struct jv sv = js_eval(js_nodes[e].a); JCHK();
                if (sv.t == JT_ARR) for (u32 i = 0; i < js_arrs[sv.n].len; i++) js_arr_push(r, js_vpool[js_arrs[sv.n].data + i]);
                else if (sv.t == JT_STR) { const char *s = js_cs(sv); for (int i = 0; s[i]; i++) js_arr_push(r, js_mk(JT_STR, js_newstr(s + i, 1))); }
            } else { struct jv v = js_eval(e); JCHK(); js_arr_push(r, v); }
            if (js_fatal) return js_undef;
        }
        return r;
    }
    case N_OBJECT: {
        struct jv o = js_new_obj(); if (js_fatal) return js_undef;
        for (u32 p = nd->a; p; p = js_nodes[p].next) { struct jv v = js_eval(js_nodes[p].b); JCHK(); js_obj_set(o, js_nodes[p].a, v); }
        return o;
    }
    case N_FUNC: {
        if (js_ftop >= JS_FUNCS) { js_fail("out of memory (functions)"); return js_undef; }
        struct jfunc *f = &js_funcs[js_ftop]; f->node = n; f->env = js_scope; f->this_v = js_this; f->arrow = (u8)nd->d;
        return js_mk(JT_FUN, js_ftop++);
    }
    case N_MEMBER: {
        struct jv o = js_eval(nd->a); JCHK();
        if (nd->op && (o.t == JT_UNDEF || o.t == JT_NULL)) return js_undef;
        return js_getprop(o, js_nodes[n].b);
    }
    case N_INDEX: {
        struct jv o = js_eval(nd->a); JCHK();
        if (nd->op && (o.t == JT_UNDEF || o.t == JT_NULL)) return js_undef;
        struct jv key = js_eval(js_nodes[n].b); JCHK();
        u32 i;
        if (js_arr_index(key, &i)) {
            if (o.t == JT_ARR) return js_arr_get(o, i);
            if (o.t == JT_STR) { const char *s = js_cs(o); return (int)i < js_len(s) ? js_mk(JT_STR, js_newstr(s + i, 1)) : js_undef; }
        }
        return js_getprop(o, js_key_of(key));
    }
    case N_CALL: {
        struct jv args[16]; int na; u32 callee = nd->a; u32 argn = nd->b; int optional = (int)nd->op;
        if (js_nodes[callee].k == N_MEMBER || js_nodes[callee].k == N_INDEX) {
            struct jv o = js_eval(js_nodes[callee].a); JCHK();
            if (js_nodes[callee].op && (o.t == JT_UNDEF || o.t == JT_NULL)) return js_undef;
            u32 key; struct jv index_key = js_undef;
            if (js_nodes[callee].k == N_MEMBER) key = js_nodes[callee].b;
            else { index_key = js_eval(js_nodes[callee].b); JCHK(); key = js_key_of(index_key); }
            if (o.t == JT_UNDEF || o.t == JT_NULL) return js_getprop(o, key);        /* throws the TypeError */
            struct jv fv = js_undef; u32 ai;
            if (o.t == JT_OBJ) fv = js_obj_get(o, key);
            else if (o.t == JT_ARR && js_nodes[callee].k == N_INDEX && js_arr_index(index_key, &ai)) fv = js_arr_get(o, ai);
            else if (o.t == JT_ELEM || o.t == JT_DOC || o.t == JT_WIN || o.t == JT_LOC || o.t == JT_STYLE) fv = js_undef;
            na = js_eval_args(argn, args, 16); JCHK();
            if (js_is_fn(fv)) return js_callfn(fv, o, args, na);
            struct jv res = js_undef;
            if (o.t == JT_OBJ && js_obj_get(o, k_dt).t == JT_NUM && js_date_method(o, js_heap + key, &res)) return res;
            if (js_method(o, js_heap + key, args, na, &res)) { if (js_thrown || js_fatal) return js_undef; return res; }
            if (optional) return js_undef;
            char m[100]; int k = 0; const char *p = js_heap + key; while (*p && k < 70) m[k++] = *p++;
            const char *t = " is not a function"; while (*t) m[k++] = *t++;
            m[k] = 0; return js_throw_err("TypeError", m);
        }
        struct jv f = js_eval(callee); JCHK();
        if (optional && (f.t == JT_UNDEF || f.t == JT_NULL)) return js_undef;
        na = js_eval_args(argn, args, 16); JCHK();
        if (f.t != JT_FUN && f.t != JT_NAT && f.t != JT_NS) {
            char m[100]; int k = 0; if (js_nodes[callee].k == N_IDENT) { const char *p = js_heap + js_nodes[callee].a; while (*p && k < 70) m[k++] = *p++; } else { const char *p = "expression"; while (*p) m[k++] = *p++; }
            const char *t = " is not a function"; while (*t) m[k++] = *t++;
            m[k] = 0; return js_throw_err("TypeError", m);
        }
        return js_callfn(f, js_undef, args, na);
    }
    case N_NEW: {
        struct jv args[16]; struct jv f = js_eval(nd->a); JCHK();
        int na = js_eval_args(js_nodes[n].b, args, 16); JCHK();
        if (f.t == JT_NS) return js_construct_ns((int)f.n, args, na, 1);
        if (f.t != JT_FUN) return js_throw_err("TypeError", "value is not a constructor");
        struct jv o = js_new_obj(); if (js_fatal) return js_undef;
        struct jv r = js_callfn(f, o, args, na); JCHK();
        return r.t == JT_OBJ || r.t == JT_ARR ? r : o;
    }
    case N_UNARY: {
        u32 op = nd->op;
        if (op == 't' && js_nodes[nd->a].k == N_IDENT && !js_lookup(js_scope, js_nodes[nd->a].a)) return js_s("undefined");
        if (op == 'd') {
            u32 t = nd->a;
            if (js_nodes[t].k == N_MEMBER || js_nodes[t].k == N_INDEX) {
                struct jv o = js_eval(js_nodes[t].a); JCHK();
                u32 key = js_nodes[t].k == N_MEMBER ? js_nodes[t].b : js_key_of(js_eval(js_nodes[t].b));
                if (o.t == JT_OBJ) js_obj_del(o, key);
            }
            return js_bool(1);
        }
        struct jv v = js_eval(nd->a); JCHK();
        switch (op) {
        case '!': return js_bool(!js_truthy(v));
        case '-': return js_mk(JT_NUM, js_isnan(js_tonum(v)) ? JS_NAN : -js_tonum(v));
        case '+': return js_mk(JT_NUM, js_tonum(v));
        case '~': return js_num_i(~js_toint(js_tonum(v)));
        case 't': return js_s(js_typeof(v));
        case 'v': return js_undef;
        }
        return js_undef;
    }
    case N_UPDATE: {
        struct jv old = js_eval(nd->a); JCHK();
        js_s64 o = js_tonum(old), nv = js_nadd(o, nd->op == PK2('+', '+') ? JS_ONE : -JS_ONE);
        js_assign_to(js_nodes[n].a, js_mk(JT_NUM, nv)); JCHK();
        return js_mk(JT_NUM, js_nodes[n].c ? nv : o);
    }
    case N_BINARY: {
        struct jv a = js_eval(nd->a); JCHK();
        struct jv b = js_eval(js_nodes[n].b); JCHK();
        struct jv r = js_binop(js_nodes[n].op, a, b);
        return r;
    }
    case N_LOGICAL: {
        struct jv a = js_eval(nd->a); JCHK();
        u32 op = js_nodes[n].op;
        if (op == PK2('&', '&')) return js_truthy(a) ? js_eval(js_nodes[n].b) : a;
        if (op == PK2('|', '|')) return js_truthy(a) ? a : js_eval(js_nodes[n].b);
        return a.t == JT_UNDEF || a.t == JT_NULL ? js_eval(js_nodes[n].b) : a;
    }
    case N_ASSIGN: {
        u32 op = nd->op, target = nd->a, rhs = nd->b; struct jv v;
        if (op == '=') { v = js_eval(rhs); JCHK(); }
        else { struct jv cur = js_eval(target); JCHK(); struct jv r = js_eval(rhs); JCHK(); v = js_binop(js_compound_base(op), cur, r); JCHK(); }
        js_assign_to(target, v); JCHK();
        return v;
    }
    case N_COND: { struct jv c = js_eval(nd->a); JCHK(); return js_eval(js_truthy(c) ? js_nodes[n].b : js_nodes[n].c); }
    case N_SEQ: { js_eval(nd->a); JCHK(); return js_eval(js_nodes[n].b); }
    }
    return js_undef;
}

/* ============================== statements =========================== */
/* exec returns 0 normal, 1 break, 2 continue, 3 return (value in js_retval); errors set js_thrown / js_fatal */
static int js_exec_list(u32 head) {
    for (u32 s = head; s; s = js_nodes[s].next)                       /* hoist function declarations */
        if (js_nodes[s].k == N_VAR && js_nodes[s].op == 1) { u32 d = js_nodes[s].a; struct jv f = js_eval(js_nodes[d].b); if (js_thrown || js_fatal) return 0; js_declare(js_scope, js_nodes[d].a, f); }
    for (u32 s = head; s; s = js_nodes[s].next) {
        int rc = js_exec(s);
        if (rc || js_thrown || js_fatal) return rc;
    }
    return 0;
}
/* when a closure captured the loop variables, later iterations must get their own copy of the scope */
static void js_rescope(u32 outer) {
    u32 old = js_scope, ns = js_newscope(outer); if (js_fatal) return;
    for (u32 v = js_scopes[old].first; v; v = js_var_at(old, v)->next) js_declare(ns, js_var_at(old, v)->name, js_var_at(old, v)->v);
    js_scope = ns;
}
static int js_exec(u32 n) {
    if (!n) return 0;
    if (++js_steps > JS_MAXSTEPS) { js_fail("the script ran too long and was stopped"); return 0; }
    if (js_fatal || js_thrown) return 0;
    struct jn *nd = &js_nodes[n];
    switch (nd->k) {
    case N_EMPTY: return 0;
    case N_EXPR: js_eval(nd->a); return 0;
    case N_VAR:
        if (nd->op == 1) return 0;
        for (u32 d = nd->a; d; d = js_nodes[d].next) {
            struct jv v = js_undef;
            if (js_nodes[d].b) { v = js_eval(js_nodes[d].b); if (js_thrown || js_fatal) return 0; }
            js_declare(js_scope, js_nodes[d].a, v);
        }
        return 0;
    case N_BLOCK: return js_exec_list(nd->a);
    case N_IF: {
        struct jv c = js_eval(nd->a); if (js_thrown || js_fatal) return 0;
        if (js_truthy(c)) return js_exec(js_nodes[n].b);
        return js_exec(js_nodes[n].c);
    }
    case N_WHILE:
        for (;;) {
            struct jv c = js_eval(js_nodes[n].a); if (js_thrown || js_fatal || !js_truthy(c)) return 0;
            int rc = js_exec(js_nodes[n].b);
            if (js_thrown || js_fatal) return 0;
            if (rc == 1) return 0;
            if (rc == 3) return 3;
        }
    case N_DOWHILE:
        for (;;) {
            int rc = js_exec(js_nodes[n].a);
            if (js_thrown || js_fatal) return 0;
            if (rc == 1) return 0;
            if (rc == 3) return 3;
            struct jv c = js_eval(js_nodes[n].b); if (js_thrown || js_fatal || !js_truthy(c)) return 0;
        }
    case N_FOR: {
        u32 outer = js_scope, init = nd->a; int letloop = init && js_nodes[init].k == N_VAR && js_nodes[init].c;
        if (letloop) { u32 ls = js_newscope(outer); if (js_fatal) return 0; js_scope = ls; }
        if (init) { if (js_nodes[init].k == N_VAR) js_exec(init); else js_eval(init); }
        int rc = 0;
        while (!js_thrown && !js_fatal) {
            if (js_nodes[n].b) { struct jv c = js_eval(js_nodes[n].b); if (js_thrown || js_fatal || !js_truthy(c)) break; }
            u32 clos = js_ftop;
            rc = js_exec(js_nodes[n].d);
            if (js_thrown || js_fatal) break;
            if (rc == 1) { rc = 0; break; }
            if (rc == 3) break;
            rc = 0;
            if (letloop && js_ftop != clos) js_rescope(outer);
            if (js_nodes[n].c) js_eval(js_nodes[n].c);
        }
        js_scope = outer;
        return rc;
    }
    case N_FORIN: {
        u32 outer = js_scope, target = nd->a; int isof = nd->op == 'o'; int rc = 0;
        struct jv it = js_eval(js_nodes[n].b); if (js_thrown || js_fatal) return 0;
        u32 name = 0, ls = outer;
        if (js_nodes[target].k == N_VAR) { name = js_nodes[js_nodes[target].a].a; if (js_nodes[target].c) { ls = js_newscope(outer); if (js_fatal) return 0; js_scope = ls; } else ls = outer; }
        /* snapshot the things to iterate */
        struct jv items = js_new_arr(8); if (js_fatal) { js_scope = outer; return 0; }
        if (isof) {
            if (it.t == JT_ARR) for (u32 i = 0; i < js_arrs[it.n].len; i++) js_arr_push(items, js_vpool[js_arrs[it.n].data + i]);
            else if (it.t == JT_STR) { const char *s = js_cs(it); for (int i = 0; s[i]; i++) js_arr_push(items, js_mk(JT_STR, js_newstr(s + i, 1))); }
            else { js_throw_err("TypeError", "value is not iterable"); js_scope = outer; return 0; }
        } else {
            if (it.t == JT_OBJ) for (u32 p = js_objs[it.n].first; p; p = js_props[p].next) js_arr_push(items, js_mk(JT_STR, js_props[p].key));
            else if (it.t == JT_ARR) for (u32 i = 0; i < js_arrs[it.n].len; i++) { char b[16]; js_fmt_num((js_s64)i << 32, b, sizeof b); js_arr_push(items, js_str(b)); }
        }
        for (u32 i = 0; i < js_arrs[items.n].len && !js_thrown && !js_fatal; i++) {
            struct jv v = js_vpool[js_arrs[items.n].data + i]; u32 clos = js_ftop;
            if (name) js_declare(js_scope, name, v); else js_assign_to(target, v);
            rc = js_exec(js_nodes[n].c);
            if (js_thrown || js_fatal) break;
            if (rc == 1) { rc = 0; break; }
            if (rc == 3) break;
            rc = 0;
            if (name && js_scope != outer && js_ftop != clos) js_rescope(outer);
        }
        js_scope = outer;
        return rc;
    }
    case N_RETURN: { struct jv v = nd->a ? js_eval(nd->a) : js_undef; if (js_thrown || js_fatal) return 0; js_retval = v; return 3; }
    case N_BREAK: return 1;
    case N_CONTINUE: return 2;
    case N_THROW: { struct jv v = js_eval(nd->a); if (js_fatal || js_thrown) return 0; js_exc = v; js_thrown = 1; return 0; }
    case N_TRY: {
        int rc = js_exec_list(nd->a), has_catch = nd->d & 1, has_fin = nd->d & 2;
        if (js_thrown && has_catch && !js_fatal) {
            struct jv e = js_exc; js_thrown = 0;
            u32 outer = js_scope;
            if (js_nodes[n].op) { u32 cs = js_newscope(outer); if (js_fatal) return 0; js_declare(cs, js_nodes[n].op, e); js_scope = cs; }
            rc = js_exec_list(js_nodes[n].b);
            js_scope = outer;
        }
        if (has_fin && !js_fatal) {
            int th = js_thrown; struct jv exc = js_exc, rv = js_retval; js_thrown = 0;
            int rc2 = js_exec_list(js_nodes[n].c);
            if (js_thrown || js_fatal || rc2) return rc2;
            js_thrown = th; js_exc = exc; js_retval = rv;
        }
        return rc;
    }
    case N_SWITCH: {
        struct jv d = js_eval(nd->a); if (js_thrown || js_fatal) return 0;
        u32 hit = 0;
        for (u32 c = js_nodes[n].b; c && !hit; c = js_nodes[c].next) if (js_nodes[c].op) { struct jv t = js_eval(js_nodes[c].a); if (js_thrown || js_fatal) return 0; if (js_seq(d, t)) hit = c; }
        if (!hit) for (u32 c = js_nodes[n].b; c; c = js_nodes[c].next) if (!js_nodes[c].op) { hit = c; break; }
        for (u32 c = hit; c; c = js_nodes[c].next) {
            int rc = js_exec_list(js_nodes[c].b);
            if (js_thrown || js_fatal) return 0;
            if (rc == 1) return 0;
            if (rc) return rc;
        }
        return 0;
    }
    default: js_eval(n); return 0;
    }
}

/* ================================ public API ========================= */
static void js_define(const char *name, struct jv v) { js_declare(0, js_intern_c(name), v); }
static void js_reset(void) {                                           /* a fresh page: forget everything */
    memset(js_itab, 0, sizeof js_itab);
    js_htop = 1; js_heap[0] = 0; js_vtop = 1; js_ptop = 1; js_otop = 1; js_atop = 1; js_ftop = 1; js_stop = 1; js_lvtop = 1; js_gvtop = 1; js_ctop = 0; js_ntop = 1;
    memset(js_timers, 0, sizeof js_timers);
    js_fatal = 0; js_thrown = 0; js_steps = 0; js_depth = 0; js_errmsg[0] = 0; js_arr_depth = 0; js_pdepth = 0;
    js_scopes[0].parent = 0; js_scopes[0].first = 0;
    js_this = js_undef; js_scope = 0; js_retval = js_undef;
    k_length = js_intern_c("length"); k_message = js_intern_c("message"); k_name = js_intern_c("name"); k_prototype = js_intern_c("prototype"); k_dt = js_intern_c("_dt");
    js_define("NaN", js_mk(JT_NUM, JS_NAN)); js_define("Infinity", js_mk(JT_NUM, JS_INF));
    js_define("Math", js_mk(JT_NS, NS_MATH)); js_define("JSON", js_mk(JT_NS, NS_JSON)); js_define("console", js_mk(JT_NS, NS_CONSOLE));
    js_define("Object", js_mk(JT_NS, NS_OBJECT)); js_define("Array", js_mk(JT_NS, NS_ARRAY)); js_define("Number", js_mk(JT_NS, NS_NUMBER));
    js_define("String", js_mk(JT_NS, NS_STRING)); js_define("Date", js_mk(JT_NS, NS_DATE)); js_define("Boolean", js_mk(JT_NS, NS_BOOLEAN));
    js_define("Error", js_mk(JT_NS, NS_ERROR)); js_define("TypeError", js_mk(JT_NS, NS_ERROR)); js_define("RangeError", js_mk(JT_NS, NS_ERROR));
    js_define("performance", js_mk(JT_NS, NS_PERF));
    js_storage = js_new_obj(); js_define("localStorage", js_mk(JT_NS, NS_STORAGE)); js_define("sessionStorage", js_mk(JT_NS, NS_STORAGE));
    js_define("parseInt", js_mk(JT_NAT, NAT_PARSEINT)); js_define("parseFloat", js_mk(JT_NAT, NAT_PARSEFLOAT)); js_define("isNaN", js_mk(JT_NAT, NAT_ISNAN));
    js_define("isFinite", js_mk(JT_NAT, NAT_ISFINITE)); js_define("alert", js_mk(JT_NAT, NAT_ALERT)); js_define("setTimeout", js_mk(JT_NAT, NAT_SETTIMEOUT));
    js_define("setInterval", js_mk(JT_NAT, NAT_SETINTERVAL)); js_define("clearTimeout", js_mk(JT_NAT, NAT_CLEARTIMER)); js_define("clearInterval", js_mk(JT_NAT, NAT_CLEARTIMER));
    js_define("prompt", js_mk(JT_NAT, NAT_PROMPT)); js_define("confirm", js_mk(JT_NAT, NAT_CONFIRM)); js_define("requestAnimationFrame", js_mk(JT_NAT, NAT_RAF));
    js_define("window", js_mk(JT_WIN, 0)); js_define("globalThis", js_mk(JT_WIN, 0)); js_define("self", js_mk(JT_WIN, 0));
    js_define("document", js_mk(JT_DOC, 0)); js_define("location", js_mk(JT_LOC, 0));
}
static void js_init(struct jshost *h) { js_host = h; js_reset(); }
/* the text of an uncaught exception */
static void js_uncaught(void) {
    char m[160]; int o = 0; const char *p = "Uncaught ";
    while (*p) m[o++] = *p++;
    if (js_exc.t == JT_OBJ && js_obj_get(js_exc, k_message).t != JT_UNDEF) {
        struct jv nm = js_obj_get(js_exc, k_name); p = nm.t == JT_STR ? js_cs(nm) : "Error";
        while (*p && o < 40) m[o++] = *p++;
        m[o++] = ':'; m[o++] = ' ';
        p = js_cs(js_tostr(js_obj_get(js_exc, k_message)));
    } else p = js_cs(js_tostr(js_exc));
    while (*p && o < 150) m[o++] = *p++;
    m[o] = 0; js_thrown = 0; js_fail(m);
}
/* run a script in the global scope; 0 = fine, else js_errmsg tells what happened (the page keeps working) */
static int js_run(const char *src, int len) {
    js_fatal = 0; js_thrown = 0; js_steps = 0; js_depth = 0; js_pdepth = 0; js_scope = 0; js_this = js_mk(JT_WIN, 0);
    memset(&L, 0, sizeof L); L.s = src; L.n = len; L.line = 1;
    u32 mark_nodes = js_ntop; (void)mark_nodes;
    js_next();
    u32 head = js_parse_block_body();
    if (!js_fatal && L.tk != TK_EOF) js_perr("unexpected '}'");
    if (js_fatal) return 1;
    js_exec_list(head);
    if (js_thrown) js_uncaught();
    return js_fatal ? 2 : 0;
}
/* call a function value (an event handler or callback); 0 = fine */
static int js_call_handler(struct jv fn, struct jv thisv, struct jv *args, int n) {
    js_fatal = 0; js_thrown = 0; js_steps = 0; js_depth = 0; js_scope = 0;
    js_prevent = 0;
    struct jv r = js_callfn(fn, thisv, args, n);
    if (r.t == JT_BOOL && !r.n && !js_thrown) js_prevent = 1;
    if (js_thrown) js_uncaught();
    return js_fatal ? 2 : 0;
}
/* run the code of an inline handler such as onclick="count++" */
static int js_run_handler_code(const char *src, int len, struct jv thisv) {
    js_fatal = 0; js_thrown = 0; js_steps = 0; js_depth = 0; js_pdepth = 0; js_scope = 0; js_this = thisv;
    memset(&L, 0, sizeof L); L.s = src; L.n = len; L.line = 1;
    js_next();
    u32 head = js_parse_block_body();
    if (!js_fatal && L.tk != TK_EOF) js_perr("unexpected '}'");
    if (js_fatal) return 1;
    js_prevent = 0;
    int rc = js_exec_list(head);
    if (rc == 3 && js_retval.t == JT_BOOL && !js_retval.n) js_prevent = 1;
    if (js_thrown) js_uncaught();
    return js_fatal ? 2 : 0;
}
/* run the timers that are due; returns how many ran */
static int js_poll(void) {
    int ran = 0; u32 now = js_host && js_host->now_ms ? js_host->now_ms() : 0;
    for (int guard = 0; guard < 64; guard++) {
        int best = -1;                                                  /* the earliest timer that is due */
        for (int i = 0; i < JS_MAXTIMERS; i++)
            if (js_timers[i].on && (int)(now - js_timers[i].due) >= 0 && (best < 0 || (int)(js_timers[i].due - js_timers[best].due) < 0)) best = i;
        if (best < 0) break;
        struct jv fn = js_timers[best].fn;
        if (js_timers[best].period) js_timers[best].due += js_timers[best].period; else js_timers[best].on = 0;
        if (js_timers[best].on && (int)(now - js_timers[best].due) >= 0) js_timers[best].due = now + js_timers[best].period;     /* do not burst after a long pause */
        int rc = js_call_handler(fn, js_mk(JT_WIN, 0), NULL, 0);
        if (rc) { js_timers[best].on = 0; if (js_host && js_host->console) js_host->console(js_errmsg); }
        ran++;
    }
    return ran;
}
#endif
