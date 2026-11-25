/* fakenet.h - a fake network for tests: DHCP + DNS + gateway/ARP + ICMP + a small HTTP server with its
 * own TCP (and optional loss).  Include after net.h.  Use fake_send()/fake_recv() as the NIC. */
#ifndef FAKENET_H
#define FAKENET_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define IP(a, b, c, d) ((u32)(a) << 24 | (b) << 16 | (c) << 8 | (d))
#define GW IP(10, 0, 2, 2)
#define DNSIP IP(10, 0, 2, 3)
#define LOCAL IP(10, 0, 2, 100)         /* on-link web server */
#define REMOTE IP(93, 184, 216, 34)     /* off-link web server (via the gateway) */
#define DEAD IP(10, 0, 2, 200)          /* nobody answers */
#define CLOSED_PORT 81
/* ---------------- the fake wire ---------------- */
struct qf { u8 d[1600]; int n; };
static struct qf q[256]; static int q_h, q_t;
static int fake_recv(u8 *b, int max) {
    if (q_h == q_t) return 0;
    struct qf *f = &q[q_h++ & 255]; int n = f->n < max ? f->n : max; memcpy(b, f->d, (size_t)n); return n;
}
static int drop_nth_data = 0, data_count = 0, drop_all_data = 0;   /* loss injection (server -> client data) */
static int srv_tx_frames, srv_retrans;
static void wire_push(const u8 *f, int n) { struct qf *s = &q[q_t++ & 255]; memcpy(s->d, f, (size_t)n); s->n = n; }
static void mac_of(u8 *m, u32 ip) { m[0] = 0x52; m[1] = 0x55; nwr32(m + 2, ip); }
static u8 cmac[6];
static u32 cli_ip = IP(10, 0, 2, 15);

static void push_ip(u32 src, const u8 *dstmac, u32 dst, u8 proto, const u8 *pl, int pln) {
    u8 f[1600]; memcpy(f, dstmac, 6); mac_of(f + 6, src == 0 ? GW : (src == IP(10, 0, 2, 2) || src == DNSIP ? src : (src == LOCAL ? src : GW)));
    nwr16(f + 12, 0x0800);
    u8 *p = f + 14; p[0] = 0x45; p[1] = 0; nwr16(p + 2, (u16)(20 + pln)); nwr16(p + 4, 7); nwr16(p + 6, 0); p[8] = 64; p[9] = proto;
    nwr16(p + 10, 0); nwr32(p + 12, src); nwr32(p + 16, dst); nwr16(p + 10, ncfin(ncsum(0, p, 20)));
    memcpy(p + 20, pl, (size_t)pln);
    wire_push(f, 34 + pln);
}
static void push_udp(u32 src, u16 sp, const u8 *dmac, u32 dst, u16 dp, const u8 *pl, int pln) {
    u8 u[1500]; nwr16(u, sp); nwr16(u + 2, dp); nwr16(u + 4, (u16)(8 + pln)); nwr16(u + 6, 0); memcpy(u + 8, pl, (size_t)pln);
    push_ip(src, dmac, dst, 17, u, 8 + pln);
}

/* ---- DHCP ---- */
static void fake_dhcp(const u8 *b, int n) {
    if (n < 244) return;
    int type = 0;
    for (int i = 240; i < n && b[i] != 255;) { if (b[i] == 53) type = b[i + 2]; i += 2 + b[i + 1]; }
    if (type != 1 && type != 3) return;
    u8 r[400]; memset(r, 0, sizeof r);
    r[0] = 2; r[1] = 1; r[2] = 6; memcpy(r + 4, b + 4, 4); nwr32(r + 16, cli_ip); memcpy(r + 28, b + 28, 6); nwr32(r + 236, 0x63825363u);
    u8 *o = r + 240;
    *o++ = 53; *o++ = 1; *o++ = type == 1 ? 2 : 5;
    *o++ = 54; *o++ = 4; nwr32(o, GW); o += 4;
    *o++ = 1; *o++ = 4; nwr32(o, 0xFFFFFF00u); o += 4;
    *o++ = 3; *o++ = 4; nwr32(o, GW); o += 4;
    *o++ = 6; *o++ = 8; nwr32(o, DNSIP); nwr32(o + 4, GW); o += 8;
    *o++ = 51; *o++ = 4; nwr32(o, 86400); o += 4;
    *o++ = 255;
    push_udp(GW, 67, bcast_mac, 0xFFFFFFFFu, 68, r, 300);
}
/* ---- DNS ---- */
static int dns_queries;
static void dns_server(const u8 *b, int n, u16 cport) {
    dns_queries++;
    if (n < 17) return;
    char name[100]; int o = 12, l = 0;
    while (b[o]) { int c = b[o++]; if (l) name[l++] = '.'; memcpy(name + l, b + o, (size_t)c); l += c; o += c; }
    name[l] = 0; o++;
    u32 ans = 0, canon = 0;
    if (!strcmp(name, "example.test")) ans = REMOTE;
    else if (!strcmp(name, "www.example.test")) { ans = REMOTE; canon = 1; }
    else if (!strcmp(name, "local.test")) ans = LOCAL;
    else if (!strcmp(name, "refuse.test")) ans = LOCAL;
    else if (!strcmp(name, "dead.test")) ans = DEAD;
    u8 r[512]; memset(r, 0, sizeof r);
    memcpy(r, b, 2); nwr16(r + 2, ans ? 0x8180 : 0x8183); nwr16(r + 4, 1); nwr16(r + 6, ans ? (canon ? 2 : 1) : 0);
    int qlen = o + 4 - 12; memcpy(r + 12, b + 12, (size_t)qlen);
    int w = 12 + qlen;
    if (canon) {                         /* CNAME www -> example.test, name compressed to the question */
        nwr16(r + w, 0xC00C); nwr16(r + w + 2, 5); nwr16(r + w + 4, 1); nwr32(r + w + 6, 60);
        u8 cn[] = { 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 4, 't', 'e', 's', 't', 0 };
        nwr16(r + w + 10, sizeof cn); memcpy(r + w + 12, cn, sizeof cn); w += 12 + (int)sizeof cn;
    }
    if (ans) { nwr16(r + w, 0xC00C); nwr16(r + w + 2, 1); nwr16(r + w + 4, 1); nwr32(r + w + 6, 60); nwr16(r + w + 10, 4); nwr32(r + w + 12, ans); w += 16; }
    u8 m[6]; memcpy(m, cmac, 6);
    push_udp(DNSIP, 53, m, cli_ip, cport, r, w);
}

/* ---- HTTP + TCP server ---- */
static u8 *page; static int page_n;
/* extra files the fake web server can serve (registered by the tests); a path may be registered again to replace it */
static struct { char path[160]; u8 *d; int n; } fake_files[24]; static int fake_nfiles;
static void fake_add_file(const char *path, const u8 *d, int n) {
    int i; for (i = 0; i < fake_nfiles; i++) if (!strcmp(fake_files[i].path, path)) break;
    if (i == fake_nfiles) { assert(fake_nfiles < 24); fake_nfiles++; snprintf(fake_files[i].path, sizeof fake_files[i].path, "%s", path); }
    else free(fake_files[i].d);
    fake_files[i].d = malloc((size_t)n + 1); memcpy(fake_files[i].d, d, (size_t)n); fake_files[i].n = n;
}
static u8 pat(int i) { return (u8)('A' + (i * 7 + i / 13) % 26); }
static void build_page(const char *path) {
    free(page); page = malloc(300000); page_n = 0;
    char *p = (char *)page;
#define ADD(...) (page_n += sprintf(p + page_n, __VA_ARGS__))
    int fi = -1; for (int k = 0; k < fake_nfiles; k++) if (!strcmp(fake_files[k].path, path)) fi = k;
    if (fi >= 0) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %d\r\n\r\n", fake_files[fi].n);
        memcpy(page + page_n, fake_files[fi].d, (size_t)fake_files[fi].n); page_n += fake_files[fi].n;
    } else if (!strcmp(path, "/")) {
        const char *body = "<html><title>Hi</title><body>Hello from the fake server</body></html>";
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(body), body);
    } else if (!strcmp(path, "/chunked")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\n");
        ADD("5\r\nHello\r\n"); ADD("6;ext=1\r\n, chun\r\n"); ADD("1A\r\nked world, in 3 chunks ok.\r\n"); ADD("0\r\n\r\n");
    } else if (!strcmp(path, "/redir")) {
        ADD("HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n");
    } else if (!strcmp(path, "/redir2")) {
        ADD("HTTP/1.1 301 Moved\r\nLocation: http://www.example.test/final\r\nContent-Length: 5\r\n\r\nmoved");
    } else if (!strcmp(path, "/loop")) {
        ADD("HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n");
    } else if (!strcmp(path, "/tls")) {
        ADD("HTTP/1.1 301 Moved\r\nLocation: https://secure.test/\r\nContent-Length: 0\r\n\r\n");
    } else if (!strcmp(path, "/page")) {
        const char *b =
            "<!DOCTYPE html><html><head><title>Test &amp; Demo Page</title><style>p { color: red }</style>"
            "<script>var secret = '<b>hidden</b>'; if (1 < 2) { document.write('no'); }</script></head><body>"
            "<!-- a comment <b>not shown</b> -->"
            "<h1>Big Heading</h1><p>This is a paragraph with <b>bold</b> text, an <a href=\"/final\">absolute link</a>, "
            "a <a href='sub/rel.html'>relative link</a> and a very long run of words so that the line has to wrap around the "
            "right edge of the window at least once or twice, which is what we want to see.</p>"
            "<h2>List</h2><ul><li>First item</li><li>Second &lt;item&gt; &quot;quoted&quot; &#65;&#x42;</li><li><a href=\"../up.html\">Up link</a></li></ul>"
            "<ol><li>One</li><li>Two</li></ol><hr><pre>  preformatted   text\n    keeps    spaces</pre>"
            "<table><tr><th>Name</th><th>Value</th></tr><tr><td>alpha</td><td>1</td></tr></table>"
            "<p>Image: <img src=\"x.png\" alt=\"a picture\"> caf\xc3\xa9 \xe2\x80\x9cquotes\xe2\x80\x9d dash\xe2\x80\x94" "end</p>"
            "<p><a href=\"mailto:a@b.c\">mail</a> <a href=\"#top\">top</a> <a href=\"http://example.test/final\">cross-host</a></p>"
            "</body></html>";
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(b), b);
    } else if (!strcmp(path, "/app.bin")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: 3000\r\n\r\n");
        for (int i = 0; i < 3000; i++) page[page_n++] = pat(i);
    } else if (!strcmp(path, "/plain")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 40\r\n\r\nline one\nline two\n\n    indented three\n");
    } else if (!strcmp(path, "/final")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 18\r\n\r\n<b>final page</b>\n");
    } else if (!strcmp(path, "/big")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: 100000\r\n\r\n");
        for (int i = 0; i < 100000; i++) page[page_n++] = pat(i);
    } else if (!strcmp(path, "/huge")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 250000\r\n\r\n");
        for (int i = 0; i < 250000; i++) page[page_n++] = pat(i);
    } else if (!strcmp(path, "/close")) {
        ADD("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nno length, ends when the server closes");
    } else if (!strcmp(path, "/nodata")) {
        ADD("HTTP/1.1 204 No Content\r\n\r\n");
    } else if (!strcmp(path, "/split")) {
        ADD("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd");
    } else {
        ADD("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: 20\r\n\r\n<h1>Not Found</h1>\n\n");
    }
}
struct srv { int active; u32 sip, cip; u16 cport, sport; u32 last_prog; u32 iss, snd_una, snd_nxt, rcv_nxt; u16 wnd; int fin_sent, got_req, sent_seen_ack; int dup; char req[2048]; int reqn; } sv;
static int srv_syns;
static char fake_gw_host[100]; static int fake_gw_tls, fake_gw_down, fake_gw_hits;   /* gateway emulation: what the client asked for */
static void srv_seg(u8 flags, u32 seq, const u8 *d, int dl, int mss) {
    u8 t[1500]; int hl = 20 + (mss ? 4 : 0);
    nwr16(t, sv.sport); nwr16(t + 2, sv.cport); nwr32(t + 4, seq); nwr32(t + 8, (flags & 0x10) ? sv.rcv_nxt : 0);
    t[12] = (u8)(hl << 2); t[13] = flags; nwr16(t + 14, 32768); nwr16(t + 16, 0); nwr16(t + 18, 0);
    if (mss) { t[20] = 2; t[21] = 4; nwr16(t + 22, 1460); }
    if (dl) memcpy(t + hl, d, (size_t)dl);
    u32 sum = (sv.sip >> 16) + (sv.sip & 0xFFFF) + (sv.cip >> 16) + (sv.cip & 0xFFFF) + 6 + (u32)(hl + dl);
    nwr16(t + 16, ncfin(ncsum(sum, t, hl + dl)));
    srv_tx_frames++;
    if (dl > 0) {
        data_count++;
        if (drop_all_data) return;
        if (drop_nth_data && data_count == drop_nth_data) return;
    }
    push_ip(sv.sip, cmac, sv.cip, 6, t, hl + dl);
}
static void srv_pump(void) {
    if (!sv.active || !sv.got_req) return;
    u32 base = sv.iss + 1;
    while ((i32)(sv.snd_nxt - base) < page_n && (u32)(sv.snd_nxt - sv.snd_una) < sv.wnd) {
        int off = (int)(sv.snd_nxt - base), n = page_n - off;
        if (n > 1460) n = 1460;
        if ((u32)n > sv.wnd - (sv.snd_nxt - sv.snd_una)) n = (int)(sv.wnd - (sv.snd_nxt - sv.snd_una));
        srv_seg(0x18, sv.snd_nxt, page + off, n, 0);
        sv.snd_nxt += (u32)n;
    }
    if ((i32)(sv.snd_nxt - base) >= page_n && !sv.fin_sent && sv.snd_una == sv.snd_nxt) {
        sv.fin_sent = 1; srv_seg(0x11, sv.snd_nxt, 0, 0, 0); sv.snd_nxt++;
    }
}
static void srv_tick(void) {            /* the server's retransmission timer */
    if (!sv.active || !sv.got_req || sv.snd_una == sv.snd_nxt || net_ms() - sv.last_prog < 300) return;
    sv.last_prog = net_ms(); srv_retrans++;
    u32 base = sv.iss + 1; int off = (int)(sv.snd_una - base);
    if (off < page_n) { int k = page_n - off; if (k > 1460) k = 1460; srv_seg(0x18, sv.snd_una, page + off, k, 0); }
    else srv_seg(0x11, sv.snd_nxt - 1, 0, 0, 0);
}
static int srv_server_stalls;       /* when set the server accepts but never answers */
static void tcp_server(u32 dst, const u8 *t, int n) {
    u16 sp = nrd16(t), dp = nrd16(t + 2); u8 fl = t[13]; int hl = (t[12] >> 4) * 4; u32 seq = nrd32(t + 4), ack = nrd32(t + 8);
    const u8 *d = t + hl; int dl = n - hl;
    u32 sum = (cli_ip >> 16) + (cli_ip & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + 6 + (u32)n;
    assert(ncfin(ncsum(sum, t, n)) == 0);                 /* the client's TCP checksum must be right */
    if (fl & 2) {
        srv_syns++;
        sv.sip = dst; sv.cip = cli_ip; sv.cport = sp; sv.sport = dp;
        if (dp == CLOSED_PORT || (dp == 8080 && fake_gw_down)) { sv.rcv_nxt = seq + 1; sv.active = 0; srv_seg(0x14, 0, 0, 0, 0); return; }
        if (dst == DEAD) return;
        memset(sv.req, 0, sizeof sv.req); sv.reqn = 0; sv.fin_sent = 0; sv.got_req = 0; sv.dup = 0;
        sv.active = 1; sv.iss = 1000 + (u32)srv_syns * 7777; sv.snd_una = sv.iss; sv.snd_nxt = sv.iss + 1; sv.rcv_nxt = seq + 1; sv.wnd = 1;
        srv_seg(0x12, sv.iss, 0, 0, 1460); return;
    }
    if (!sv.active || sp != sv.cport || dp != sv.sport) return;
    if (fl & 0x10) {
        sv.wnd = nrd16(t + 14);
        if ((i32)(ack - sv.snd_una) > 0) { sv.snd_una = ack; sv.dup = 0; sv.last_prog = net_ms(); }
        else if (ack == sv.snd_una && dl == 0 && !(fl & 1) && sv.got_req && sv.snd_nxt != sv.snd_una) {      /* duplicate ack -> retransmit */
            if (sv.dup++ == 0) { srv_retrans++; int off = (int)(sv.snd_una - (sv.iss + 1)); int k = page_n - off; if (k > 1460) k = 1460; if (k > 0) srv_seg(0x18, sv.snd_una, page + off, k, 0); }
        }
    }
    if (dl > 0 && seq == sv.rcv_nxt) {
        sv.rcv_nxt += (u32)dl;
        if (sv.reqn + dl < (int)sizeof sv.req) { memcpy(sv.req + sv.reqn, d, (size_t)dl); sv.reqn += dl; }
        srv_seg(0x10, sv.snd_nxt, 0, 0, 0);
        if (!sv.got_req && strstr(sv.req, "\r\n\r\n")) {
            char path[200] = "/"; sscanf(sv.req, "GET %199s", path);
            fake_gw_host[0] = 0;
            if (!strncmp(path, "/https/", 7) || !strncmp(path, "/http/", 6)) {          /* gateway request: /https/HOST/PATH */
                int tls = path[5] == 's'; const char *h = path + (tls ? 7 : 6); const char *sl = strchr(h, '/');
                size_t hl = sl ? (size_t)(sl - h) : strlen(h);
                memcpy(fake_gw_host, h, hl); fake_gw_host[hl] = 0; fake_gw_tls = tls; fake_gw_hits++;
                char rest[200]; snprintf(rest, sizeof rest, "%s", sl ? sl : "/"); snprintf(path, sizeof path, "%s", rest);
            }
            assert(strstr(sv.req, "Host: ")); assert(strstr(sv.req, "HTTP/1.1"));
            build_page(path);
            if (!srv_server_stalls) sv.got_req = 1;
        }
    }
    if (fl & 1) { sv.rcv_nxt++; srv_seg(0x10, sv.snd_nxt, 0, 0, 0); }
    srv_pump();
}
static int icmp_seen;
static int fake_send(const u8 *f, int n) {
    if (n < 14) return -1;
    memcpy(cmac, f + 6, 6);
    u16 type = nrd16(f + 12);
    if (type == 0x0806) {
        const u8 *a = f + 14;
        if (nrd16(a + 6) == 1) {
            u32 tpa = nrd32(a + 24);
            if (tpa == GW || tpa == DNSIP || tpa == LOCAL) {
                u8 r[60]; memset(r, 0, sizeof r); memcpy(r, a + 8, 6); mac_of(r + 6, tpa); nwr16(r + 12, 0x0806);
                u8 *p = r + 14; nwr16(p, 1); nwr16(p + 2, 0x0800); p[4] = 6; p[5] = 4; nwr16(p + 6, 2);
                mac_of(p + 8, tpa); nwr32(p + 14, tpa); memcpy(p + 18, a + 8, 6); memcpy(p + 24, a + 14, 4);
                wire_push(r, 60);
            }
        }
        return n;
    }
    if (type != 0x0800) return n;
    const u8 *ip = f + 14; u32 dst = nrd32(ip + 16); int ihl = (ip[0] & 15) * 4; const u8 *l4 = ip + ihl; int l4n = nrd16(ip + 2) - ihl;
    assert(ncfin(ncsum(0, ip, ihl)) == 0);                  /* IP header checksum */
    /* frames for off-link hosts must be addressed to the gateway, on-link ones to the host itself */
    u8 want[6]; mac_of(want, (dst == DNSIP || dst == LOCAL || dst == 0xFFFFFFFFu || dst == GW) ? dst : GW);
    if (dst != 0xFFFFFFFFu) assert(!memcmp(f, want, 6));
    if (ip[9] == 17) {
        u16 dp = nrd16(l4 + 2), sp = nrd16(l4);
        if (dp == 67) fake_dhcp(l4 + 8, l4n - 8);
        else if (dp == 53 && dst == DNSIP) dns_server(l4 + 8, l4n - 8, sp);
    } else if (ip[9] == 1 && l4[0] == 8 && dst != DEAD) {
        icmp_seen++;
        u8 r[100]; memcpy(r, l4, (size_t)l4n); r[0] = 0; nwr16(r + 2, 0); nwr16(r + 2, ncfin(ncsum(0, r, l4n)));
        u8 m[6]; memcpy(m, cmac, 6); push_ip(dst, m, cli_ip, 1, r, l4n);
    } else if (ip[9] == 6) tcp_server(dst, l4, l4n);
    return n;
}

#endif
