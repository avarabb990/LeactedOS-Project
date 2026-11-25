/* net.h - a small TCP/IP stack for LeactedOS (included by kernel.c).
 *
 * Needs from the includer:  u8 u16 u32 i32, memcpy/memset/memmove/memcmp, and
 *   static int  nic_send(const u8 *frame, int len);
 *   static int  nic_recv(u8 *buf, int max);      (0 when nothing is waiting)
 *   static u32  net_ms(void);                    (milliseconds, wraps)
 *   static u32  net_rand(void);
 *
 * Everything is polled: call net_poll() often.  Layers: Ethernet/ARP, IPv4,
 * ICMP echo, UDP, DHCP, DNS (A records), TCP (client side) and an HTTP/1.1 GET
 * client that works as a state machine (http_start() then keep polling).
 * IPv4 addresses are host-order u32 (a<<24 | b<<16 | c<<8 | d).
 */
#ifndef NET_H
#define NET_H

static u8 net_mac[6];
static u32 net_ip, net_mask, net_gw, net_dns;
static int net_have_nic;
static u32 net_stat_rx, net_stat_tx, net_stat_drop;

static u16 nrd16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 nrd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void nwr16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void nwr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 ncsum(u32 s, const u8 *p, int n) {
    for (; n > 1; n -= 2, p += 2) s += (u32)(p[0] << 8 | p[1]);
    if (n) s += (u32)p[0] << 8;
    return s;
}
static u16 ncfin(u32 s) { while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return (u16)~s; }

/* ---- tiny string helpers (so net.h does not depend on the kernel's) ---- */
static int n_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static char n_lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int n_starts(const char *s, const char *pre) {            /* case-insensitive prefix */
    while (*pre) { if (n_lc(*s) != n_lc(*pre)) return 0; s++; pre++; }
    return 1;
}
static void n_copy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
/* "a.b.c.d" -> 1 and *out, else 0 */
static int net_parse_ip(const char *s, u32 *out) {
    u32 v = 0; int parts = 0;
    for (;;) {
        if (*s < '0' || *s > '9') return 0;
        u32 n = 0; int digs = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (u32)(*s - '0'); s++; if (++digs > 3) return 0; }
        if (n > 255) return 0;
        v = v << 8 | n; parts++;
        if (parts == 4) break;
        if (*s != '.') return 0;
        s++;
    }
    if (*s) return 0;
    *out = v;
    return 1;
}
static void net_fmt_ip(char *out, u32 ip) {          /* out: at least 16 bytes */
    int o = 0;
    for (int k = 3; k >= 0; k--) {
        u32 b = (ip >> (k * 8)) & 255;
        if (b >= 100) out[o++] = (char)('0' + b / 100);
        if (b >= 10) out[o++] = (char)('0' + b / 10 % 10);
        out[o++] = (char)('0' + b % 10);
        if (k) out[o++] = '.';
    }
    out[o] = 0;
}

/* ------------------------------------------------------------------ */
/* Ethernet + ARP                                                      */
/* ------------------------------------------------------------------ */
static u8 net_rxb[1600], net_txb[1600];
static const u8 bcast_mac[6] = { 255, 255, 255, 255, 255, 255 };
struct arpe { u32 ip; u8 mac[6]; u8 ok; };
static struct arpe arpc[8];
static int arp_next;
static u8 pend[1600];                 /* one IP packet waiting for an ARP answer */
static int pend_len;
static u32 pend_nh;

static void eth_send(const u8 *dst, u16 type, int plen) {     /* payload already at net_txb + 14 */
    memcpy(net_txb, dst, 6); memcpy(net_txb + 6, net_mac, 6); nwr16(net_txb + 12, type);
    int n = 14 + plen;
    if (n < 60) { memset(net_txb + n, 0, (size_t)(60 - n)); n = 60; }
    nic_send(net_txb, n);
    net_stat_tx++;
}
static const u8 *arp_find(u32 ip) {
    for (int i = 0; i < 8; i++) if (arpc[i].ok && arpc[i].ip == ip) return arpc[i].mac;
    return 0;
}
static void arp_learn(u32 ip, const u8 *mac) {
    int slot = -1;
    for (int i = 0; i < 8; i++) if (arpc[i].ok && arpc[i].ip == ip) slot = i;
    if (slot < 0) { slot = arp_next; arp_next = (arp_next + 1) & 7; }
    arpc[slot].ip = ip; memcpy(arpc[slot].mac, mac, 6); arpc[slot].ok = 1;
    if (pend_len && pend_nh == ip) {                     /* send what was waiting */
        memcpy(net_txb + 14, pend, (size_t)pend_len);
        int n = pend_len; pend_len = 0;
        eth_send(mac, 0x0800, n);
    }
}
static void arp_packet(u16 op, const u8 *dmac, const u8 *tha, u32 tpa) {
    u8 *p = net_txb + 14;
    nwr16(p, 1); nwr16(p + 2, 0x0800); p[4] = 6; p[5] = 4; nwr16(p + 6, op);
    memcpy(p + 8, net_mac, 6); nwr32(p + 14, net_ip);
    memcpy(p + 18, tha, 6); nwr32(p + 24, tpa);
    eth_send(dmac, 0x0806, 28);
}
static void arp_request(u32 ip) { static const u8 z[6] = { 0 }; arp_packet(1, bcast_mac, z, ip); }
static void arp_rx(const u8 *p, int n) {
    if (n < 28 || nrd16(p) != 1 || nrd16(p + 2) != 0x0800 || p[4] != 6 || p[5] != 4) return;
    u16 op = nrd16(p + 6); u32 spa = nrd32(p + 14), tpa = nrd32(p + 24);
    if (!net_ip || tpa != net_ip) return;
    arp_learn(spa, p + 8);
    if (op == 1) arp_packet(2, p + 8, p + 8, spa);
}

/* ------------------------------------------------------------------ */
/* IPv4                                                                */
/* ------------------------------------------------------------------ */
static u16 ip_id;
/* payload is already at net_txb + 34; returns 1 if sent, 0 if queued behind an ARP lookup, -1 dropped */
static int ip_out(u8 proto, u32 dst, int plen) {
    u8 *p = net_txb + 14;
    p[0] = 0x45; p[1] = 0; nwr16(p + 2, (u16)(20 + plen)); nwr16(p + 4, ++ip_id); nwr16(p + 6, 0x4000);
    p[8] = 64; p[9] = proto; nwr16(p + 10, 0); nwr32(p + 12, net_ip); nwr32(p + 16, dst);
    nwr16(p + 10, ncfin(ncsum(0, p, 20)));
    if (dst == 0xFFFFFFFFu || (net_ip && net_mask && dst == (net_ip | ~net_mask))) { eth_send(bcast_mac, 0x0800, 20 + plen); return 1; }
    u32 nh = dst;
    if (net_ip && ((dst ^ net_ip) & net_mask)) nh = net_gw;
    if (!nh) return -1;
    const u8 *mac = arp_find(nh);
    if (!mac) {
        memcpy(pend, p, (size_t)(20 + plen)); pend_len = 20 + plen; pend_nh = nh;
        arp_request(nh);
        return 0;
    }
    eth_send(mac, 0x0800, 20 + plen);
    return 1;
}

/* ---- ICMP echo ---- */
static u16 ping_id = 0x4C41, ping_seq;
static u32 ping_t0;
static int ping_got;                   /* sequence number of the last reply, -1 none */
static u32 ping_rtt, ping_from;
static void icmp_rx(u32 src, const u8 *p, int n) {
    if (n < 8) return;
    if (ncfin(ncsum(0, p, n))) return;
    if (p[0] == 8 && n <= 1400) {                         /* echo request: answer it */
        u8 *q = net_txb + 34; memcpy(q, p, (size_t)n); q[0] = 0; nwr16(q + 2, 0);
        nwr16(q + 2, ncfin(ncsum(0, q, n)));
        ip_out(1, src, n);
    } else if (p[0] == 0 && nrd16(p + 4) == ping_id) {
        ping_got = nrd16(p + 6); ping_rtt = net_ms() - ping_t0; ping_from = src;
    }
}
static void ping_send(u32 dst, u16 seq) {
    u8 *q = net_txb + 34;
    q[0] = 8; q[1] = 0; nwr16(q + 2, 0); nwr16(q + 4, ping_id); nwr16(q + 6, seq);
    for (int i = 0; i < 32; i++) q[8 + i] = (u8)('a' + i % 23);
    nwr16(q + 2, ncfin(ncsum(0, q, 40)));
    ping_seq = seq; ping_got = -1; ping_t0 = net_ms();
    ip_out(1, dst, 40);
}

/* ---- UDP ---- */
static int udp_out(u32 dst, u16 sport, u16 dport, int plen) {   /* payload at net_txb + 42 */
    u8 *u = net_txb + 34;
    nwr16(u, sport); nwr16(u + 2, dport); nwr16(u + 4, (u16)(8 + plen)); nwr16(u + 6, 0);
    return ip_out(17, dst, 8 + plen);
}

/* ---- DHCP ---- */
static int dhcp_state;                 /* 0 idle, 1 discover sent, 2 request sent, 3 bound, 4 failed */
static u32 dhcp_xid, dhcp_t, dhcp_offer, dhcp_server;
static int dhcp_tries;
static void dhcp_send(int type) {
    u8 *p = net_txb + 42;
    memset(p, 0, 300);
    p[0] = 1; p[1] = 1; p[2] = 6; nwr32(p + 4, dhcp_xid); nwr16(p + 10, 0x8000); memcpy(p + 28, net_mac, 6);
    nwr32(p + 236, 0x63825363u);
    u8 *o = p + 240;
    *o++ = 53; *o++ = 1; *o++ = (u8)type;
    if (type == 3) { *o++ = 50; *o++ = 4; nwr32(o, dhcp_offer); o += 4; *o++ = 54; *o++ = 4; nwr32(o, dhcp_server); o += 4; }
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    udp_out(0xFFFFFFFFu, 68, 67, 300);
}
static void dhcp_start(void) {
    net_ip = net_mask = net_gw = net_dns = 0;
    for (int i = 0; i < 8; i++) arpc[i].ok = 0;
    pend_len = 0;
    dhcp_xid = net_rand(); dhcp_state = 1; dhcp_tries = 1; dhcp_t = net_ms();
    dhcp_send(1);
}
static void dhcp_rx(const u8 *p, int n) {
    if (n < 244 || p[0] != 2 || nrd32(p + 4) != dhcp_xid || nrd32(p + 236) != 0x63825363u) return;
    if (memcmp(p + 28, net_mac, 6)) return;
    int type = 0; u32 mask = 0, gw = 0, dns = 0, server = 0;
    for (int i = 240; i < n;) {
        u8 c = p[i];
        if (c == 255) break;
        if (c == 0) { i++; continue; }
        if (i + 1 >= n) break;
        int l = p[i + 1];
        if (i + 2 + l > n) break;
        const u8 *v = p + i + 2;
        if (c == 53 && l >= 1) type = v[0];
        else if (c == 1 && l >= 4) mask = nrd32(v);
        else if (c == 3 && l >= 4) gw = nrd32(v);
        else if (c == 6 && l >= 4) dns = nrd32(v);
        else if (c == 54 && l >= 4) server = nrd32(v);
        i += 2 + l;
    }
    u32 yi = nrd32(p + 16);
    if (type == 2 && dhcp_state == 1) {
        dhcp_offer = yi; dhcp_server = server; dhcp_state = 2; dhcp_tries = 1; dhcp_t = net_ms();
        dhcp_send(3);
    } else if (type == 5 && dhcp_state == 2) {
        net_ip = yi; net_mask = mask ? mask : 0xFFFFFF00u; net_gw = gw; net_dns = dns ? dns : gw;
        dhcp_state = 3;
    } else if (type == 6) dhcp_state = 4;
}
static void dhcp_tick(void) {
    if (dhcp_state != 1 && dhcp_state != 2) return;
    if (net_ms() - dhcp_t < 2000) return;
    if (++dhcp_tries > 5) { dhcp_state = 4; return; }
    dhcp_t = net_ms();
    dhcp_send(dhcp_state == 1 ? 1 : 3);
}

/* ---- DNS (one lookup at a time) ---- */
static int dns_state;                  /* 0 idle, 1 asking, 2 answer in dns_ip, 3 failed */
static int dns_nx;                     /* failed because the name does not exist */
static char dns_name[96];
static u16 dns_id, dns_port = 40053;
static u32 dns_ip, dns_t;
static int dns_tries;
struct dnsc_e { char name[64]; u32 ip; };
static struct dnsc_e dnsc[8];
static int dnsc_n;
static void dns_send(void) {
    u8 *p = net_txb + 42;
    nwr16(p, dns_id); nwr16(p + 2, 0x0100); nwr16(p + 4, 1); nwr16(p + 6, 0); nwr16(p + 8, 0); nwr16(p + 10, 0);
    int o = 12;
    for (const char *s = dns_name; *s;) {
        const char *e = s; while (*e && *e != '.') e++;
        int l = (int)(e - s);
        if (l > 0 && l < 64) { p[o++] = (u8)l; memcpy(p + o, s, (size_t)l); o += l; }
        s = *e ? e + 1 : e;
    }
    p[o++] = 0; nwr16(p + o, 1); nwr16(p + o + 2, 1); o += 4;
    udp_out(net_dns, dns_port, 53, o);
}
static int dns_start(const char *name) {
    dns_nx = 0;
    if (net_parse_ip(name, &dns_ip)) { dns_state = 2; return 2; }
    for (int i = 0; i < dnsc_n; i++) {
        const char *a = dnsc[i].name, *b = name;
        while (*a && n_lc(*a) == n_lc(*b)) { a++; b++; }
        if (!*a && !*b) { dns_ip = dnsc[i].ip; dns_state = 2; return 2; }
    }
    if (!net_dns || !net_ip) { dns_state = 3; return 3; }
    n_copy(dns_name, name, sizeof dns_name);
    dns_id = (u16)net_rand(); dns_port = (u16)(40000 + (net_rand() & 0x3FF));
    dns_state = 1; dns_tries = 1; dns_t = net_ms();
    dns_send();
    return 1;
}
static int dns_skip_name(const u8 *p, int n, int o) {
    while (o < n) {
        u8 c = p[o];
        if (c == 0) return o + 1;
        if ((c & 0xC0) == 0xC0) return o + 2;
        o += c + 1;
    }
    return -1;
}
static void dns_rx(const u8 *p, int n) {
    if (dns_state != 1 || n < 12 || nrd16(p) != dns_id || !(p[2] & 0x80)) return;
    int rc = p[3] & 15;
    if (rc == 3) { dns_nx = 1; dns_state = 3; return; }
    if (rc) { dns_state = 3; return; }
    int qd = nrd16(p + 4), an = nrd16(p + 6), o = 12;
    for (int i = 0; i < qd; i++) { o = dns_skip_name(p, n, o); if (o < 0) return; o += 4; }
    for (int i = 0; i < an; i++) {
        o = dns_skip_name(p, n, o);
        if (o < 0 || o + 10 > n) break;
        int type = nrd16(p + o), rl = nrd16(p + o + 8);
        o += 10;
        if (o + rl > n) break;
        if (type == 1 && rl == 4) {
            dns_ip = nrd32(p + o); dns_state = 2;
            struct dnsc_e *e = &dnsc[dnsc_n < 8 ? dnsc_n++ : (int)(dns_ip & 7)];
            n_copy(e->name, dns_name, sizeof e->name); e->ip = dns_ip;
            return;
        }
        o += rl;
    }
    dns_state = 3;
}
static void dns_tick(void) {
    if (dns_state != 1 || net_ms() - dns_t < 1500) return;
    if (++dns_tries > 4) { dns_state = 3; return; }
    dns_t = net_ms();
    dns_send();
}

/* ------------------------------------------------------------------ */
/* TCP (client side)                                                   */
/* ------------------------------------------------------------------ */
#define NT 4
#define TCP_RXB 16384
#define TCP_TXB 4096
#define TCP_WIN_CAP 5840
enum { T_CLOSED, T_SYN_SENT, T_ESTAB, T_FIN_WAIT1, T_FIN_WAIT2, T_CLOSING, T_CLOSE_WAIT, T_LAST_ACK };
struct tcb {
    u8 used, st, fin_pending, fin_sent, fin_rcvd, retries; u8 err;      /* err: 1 refused, 2 reset, 3 timed out */
    u32 rip; u16 rport, lport, mss, peer_wnd;
    u32 snd_una, snd_nxt, rcv_nxt, rto, t_last;
    u32 rx_h, rx_len, tx_len;
};
static struct tcb tcps[NT];
static u8 tcp_rx[NT][TCP_RXB], tcp_tx[NT][TCP_TXB];
static u16 tcp_port_next = 49200;

static u16 tcp_rxwin(const struct tcb *s) {
    u32 f = TCP_RXB - s->rx_len;
    return (u16)(f > TCP_WIN_CAP ? TCP_WIN_CAP : f);
}
static void tcp_seg(const struct tcb *s, u8 flags, u32 seq, const u8 *data, int dlen, int with_mss) {
    u8 *p = net_txb + 34; int hl = 20 + (with_mss ? 4 : 0);
    nwr16(p, s->lport); nwr16(p + 2, s->rport); nwr32(p + 4, seq); nwr32(p + 8, (flags & 0x10) ? s->rcv_nxt : 0);
    p[12] = (u8)(hl << 2); p[13] = flags; nwr16(p + 14, tcp_rxwin(s)); nwr16(p + 16, 0); nwr16(p + 18, 0);
    if (with_mss) { p[20] = 2; p[21] = 4; nwr16(p + 22, 1460); }
    if (dlen) memcpy(p + hl, data, (size_t)dlen);
    u32 sum = (net_ip >> 16) + (net_ip & 0xFFFF) + (s->rip >> 16) + (s->rip & 0xFFFF) + 6 + (u32)(hl + dlen);
    nwr16(p + 16, ncfin(ncsum(sum, p, hl + dlen)));
    ip_out(6, s->rip, hl + dlen);
}
static void tcp_flush(struct tcb *s) {
    if (s->st != T_ESTAB && s->st != T_CLOSE_WAIT) return;
    int idx = (int)(s - tcps);
    u32 sent = (s->snd_nxt - s->snd_una) - s->fin_sent;
    while (sent < s->tx_len && sent < s->peer_wnd) {
        u32 n = s->tx_len - sent;
        if (n > s->mss) n = s->mss;
        if (n > s->peer_wnd - sent) n = s->peer_wnd - sent;
        if (s->snd_nxt == s->snd_una) s->t_last = net_ms();
        tcp_seg(s, 0x18, s->snd_nxt, tcp_tx[idx] + sent, (int)n, 0);
        s->snd_nxt += n; sent += n;
    }
    if (s->fin_pending && !s->fin_sent && sent == s->tx_len) {
        if (s->snd_nxt == s->snd_una) s->t_last = net_ms();
        tcp_seg(s, 0x11, s->snd_nxt, 0, 0, 0);
        s->snd_nxt++; s->fin_sent = 1;
        s->st = (s->st == T_CLOSE_WAIT) ? T_LAST_ACK : T_FIN_WAIT1;
    }
}
static struct tcb *tcp_find(u32 rip, u16 rport, u16 lport) {
    for (int i = 0; i < NT; i++)
        if (tcps[i].used && tcps[i].rip == rip && tcps[i].rport == rport && tcps[i].lport == lport) return &tcps[i];
    return 0;
}
static void tcp_rx_pkt(u32 src, const u8 *p, int len) {
    if (len < 20) return;
    int hl = (p[12] >> 4) * 4;
    if (hl < 20 || hl > len) return;
    u32 sum = (src >> 16) + (src & 0xFFFF) + (net_ip >> 16) + (net_ip & 0xFFFF) + 6 + (u32)len;
    if (ncfin(ncsum(sum, p, len))) { net_stat_drop++; return; }
    u16 sport = nrd16(p), dport = nrd16(p + 2), win = nrd16(p + 14);
    u32 seq = nrd32(p + 4), ack = nrd32(p + 8);
    u8 fl = p[13];
    const u8 *d = p + hl; int dl = len - hl;
    struct tcb *s = tcp_find(src, sport, dport);
    if (!s) {                                              /* nobody home: refuse politely */
        if (fl & 4) return;
        struct tcb t; memset(&t, 0, sizeof t);
        t.lport = dport; t.rport = sport; t.rip = src;
        if (fl & 0x10) tcp_seg(&t, 0x04, ack, 0, 0, 0);
        else { t.rcv_nxt = seq + (u32)dl + ((fl & 3) ? 1u : 0u); tcp_seg(&t, 0x14, 0, 0, 0, 0); }
        return;
    }
    int idx = (int)(s - tcps);
    if (s->st == T_SYN_SENT) {
        if (fl & 4) { if (fl & 0x10) { s->err = 1; s->st = T_CLOSED; } return; }
        if ((fl & 0x12) != 0x12 || ack != s->snd_nxt) return;
        s->rcv_nxt = seq + 1; s->snd_una = ack; s->peer_wnd = win; s->mss = 536; s->retries = 0; s->rto = 1000;
        for (int i = 20; i + 1 < hl;) {                     /* options: look for MSS */
            u8 k = p[i];
            if (k == 0) break;
            if (k == 1) { i++; continue; }
            int ol = p[i + 1];
            if (ol < 2 || i + ol > hl) break;
            if (k == 2 && ol == 4) { u16 m = nrd16(p + i + 2); s->mss = m > 1460 ? 1460 : (m < 64 ? 536 : m); }
            i += ol;
        }
        s->st = T_ESTAB;
        tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0);
        tcp_flush(s);
        return;
    }
    if (fl & 4) { s->err = 2; s->st = T_CLOSED; return; }
    if (fl & 2) { tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0); return; }          /* repeated SYN-ACK */
    if (fl & 0x10) {
        i32 adv = (i32)(ack - s->snd_una), infl = (i32)(s->snd_nxt - s->snd_una);
        if (adv > 0 && adv <= infl) {
            u32 n = (u32)adv; int fin_acked = 0;
            if (s->fin_sent && ack == s->snd_nxt) { n--; fin_acked = 1; }
            if (n > s->tx_len) n = s->tx_len;
            if (n) { memmove(tcp_tx[idx], tcp_tx[idx] + n, s->tx_len - n); s->tx_len -= n; }
            s->snd_una = ack; s->retries = 0; s->rto = 1000; s->t_last = net_ms();
            if (fin_acked) {
                if (s->st == T_FIN_WAIT1) s->st = T_FIN_WAIT2;
                else if (s->st == T_CLOSING || s->st == T_LAST_ACK) s->st = T_CLOSED;
            }
        }
        s->peer_wnd = win;
    }
    int fin = (fl & 1) != 0, ackit = 0;
    if (dl > 0 || fin) {
        i32 off = (i32)(s->rcv_nxt - seq);
        if (off < 0) { tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0); return; }       /* a gap: ask again */
        if (off > dl || (off == dl && !fin)) { tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0); return; }
        d += off; dl -= off;
        u32 room = TCP_RXB - s->rx_len; int n = dl;
        if ((u32)n > room) n = (int)room;
        for (int i = 0; i < n; i++) tcp_rx[idx][(s->rx_h + s->rx_len + (u32)i) % TCP_RXB] = d[i];
        s->rx_len += (u32)n; s->rcv_nxt += (u32)n;
        if (fin && n == dl && !s->fin_rcvd && s->st >= T_ESTAB) {
            s->rcv_nxt++; s->fin_rcvd = 1;
            if (s->st == T_ESTAB) s->st = T_CLOSE_WAIT;
            else if (s->st == T_FIN_WAIT1) s->st = T_CLOSING;
            else if (s->st == T_FIN_WAIT2) s->st = T_CLOSED;
        }
        ackit = 1;
    }
    if (ackit) tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0);
    tcp_flush(s);
}
static void tcp_timer(void) {
    u32 now = net_ms();
    for (int i = 0; i < NT; i++) {
        struct tcb *s = &tcps[i];
        if (!s->used || s->st == T_CLOSED || s->st == T_FIN_WAIT2) continue;
        if (s->snd_nxt == s->snd_una) continue;
        if (now - s->t_last < s->rto) continue;
        if (++s->retries > 6) { s->err = 3; s->st = T_CLOSED; continue; }
        s->rto = s->rto * 2 > 8000 ? 8000 : s->rto * 2;
        s->t_last = now;
        if (s->st == T_SYN_SENT) tcp_seg(s, 0x02, s->snd_una, 0, 0, 1);
        else if (s->tx_len) tcp_seg(s, 0x18, s->snd_una, tcp_tx[i], (int)(s->tx_len < s->mss ? s->tx_len : s->mss), 0);
        else if (s->fin_sent) tcp_seg(s, 0x11, s->snd_nxt - 1, 0, 0, 0);
    }
}
static int tcp_open(u32 ip, u16 port) {
    for (int i = 0; i < NT; i++) {
        struct tcb *s = &tcps[i];
        if (s->used) continue;
        memset(s, 0, sizeof *s);
        s->used = 1; s->rip = ip; s->rport = port;
        s->lport = tcp_port_next++; if (tcp_port_next > 60000) tcp_port_next = 49200;
        s->snd_una = net_rand(); s->snd_nxt = s->snd_una + 1;
        s->st = T_SYN_SENT; s->rto = 1000; s->t_last = net_ms(); s->mss = 536;
        tcp_seg(s, 0x02, s->snd_una, 0, 0, 1);
        return i;
    }
    return -1;
}
static int tcp_write(int i, const u8 *data, int n) {
    struct tcb *s = &tcps[i];
    if (!s->used || (s->st != T_ESTAB && s->st != T_SYN_SENT && s->st != T_CLOSE_WAIT) || s->fin_pending) return -1;
    int room = (int)(TCP_TXB - s->tx_len);
    if (n > room) n = room;
    memcpy(tcp_tx[i] + s->tx_len, data, (size_t)n); s->tx_len += (u32)n;
    tcp_flush(s);
    return n;
}
static int tcp_read(int i, u8 *buf, int max) {
    struct tcb *s = &tcps[i];
    if (!s->used) return 0;
    u16 before = tcp_rxwin(s);
    int n = (int)(s->rx_len < (u32)max ? s->rx_len : (u32)max);
    for (int k = 0; k < n; k++) buf[k] = tcp_rx[i][(s->rx_h + (u32)k) % TCP_RXB];
    s->rx_h = (s->rx_h + (u32)n) % TCP_RXB; s->rx_len -= (u32)n;
    if (n && before < 1460 && tcp_rxwin(s) >= 1460 && s->st >= T_ESTAB && s->st != T_CLOSED) tcp_seg(s, 0x10, s->snd_nxt, 0, 0, 0);
    return n;
}
static int tcp_connected(int i) { return tcps[i].used && tcps[i].st >= T_ESTAB && tcps[i].st != T_CLOSED; }
static int tcp_eof(int i) { return tcps[i].rx_len == 0 && (tcps[i].fin_rcvd || tcps[i].st == T_CLOSED); }
static void tcp_close(int i) { struct tcb *s = &tcps[i]; if (s->used && !s->fin_pending) { s->fin_pending = 1; tcp_flush(s); } }
static void tcp_free(int i) {
    struct tcb *s = &tcps[i];
    if (!s->used) return;
    if (s->st != T_CLOSED && !(s->fin_sent && s->fin_rcvd)) tcp_seg(s, 0x14, s->snd_nxt, 0, 0, 0);
    s->used = 0; s->st = T_CLOSED;
}

/* ------------------------------------------------------------------ */
/* Receive path + polling                                              */
/* ------------------------------------------------------------------ */
static void ip_rx(const u8 *p, int n) {
    if (n < 20 || (p[0] >> 4) != 4) return;
    int ihl = (p[0] & 15) * 4, tl = nrd16(p + 2);
    if (ihl < 20 || tl < ihl || tl > n) return;
    if (ncfin(ncsum(0, p, ihl))) return;
    if (nrd16(p + 6) & 0x3FFF) { net_stat_drop++; return; }        /* fragments are not supported */
    u32 src = nrd32(p + 12), dst = nrd32(p + 16);
    int mine = (net_ip && dst == net_ip) || dst == 0xFFFFFFFFu || (net_ip && net_mask && dst == (net_ip | ~net_mask));
    const u8 *q = p + ihl; int ql = tl - ihl;
    if (!net_ip) mine = (p[9] == 17);                               /* before DHCP only UDP replies */
    if (!mine) return;
    if (p[9] == 1) icmp_rx(src, q, ql);
    else if (p[9] == 6) tcp_rx_pkt(src, q, ql);
    else if (p[9] == 17 && ql >= 8) {
        u16 dp = nrd16(q + 2), ul = nrd16(q + 4);
        if (ul < 8 || ul > ql) return;
        if (dp == 68) dhcp_rx(q + 8, ul - 8);
        else if (dp == dns_port) dns_rx(q + 8, ul - 8);
    }
}
static void net_rx(const u8 *f, int n) {
    if (n < 14) return;
    if (memcmp(f, net_mac, 6) && memcmp(f, bcast_mac, 6)) return;
    net_stat_rx++;
    u16 t = nrd16(f + 12);
    if (t == 0x0806) arp_rx(f + 14, n - 14);
    else if (t == 0x0800) ip_rx(f + 14, n - 14);
}

/* ------------------------------------------------------------------ */
/* HTTP client: http_start(url), then poll until http_st is H_DONE/H_ERR */
/* ------------------------------------------------------------------ */
#define HTTP_MAX 196608
enum { H_IDLE, H_DHCP, H_DNS, H_CONN, H_HEAD, H_BODY, H_DONE, H_ERR };
static int http_st;
static char http_url[320], http_host[100], http_path[220], http_msg[72], http_ctype[40], http_loc[320];
static u16 http_port;
static int http_code, http_sock = -1, http_trunc, http_redirs;
static u8 http_body[HTTP_MAX + 1];
static u32 http_len, http_t, http_rcvd;
static i32 http_clen;
static int http_chunked, http_cs, http_cext, http_nodata;
static u32 http_csize;
static u8 hhead[4096];
static int hhl;
static int http_tcp_up;                /* set by the includer: 0 means no NIC */

static void http_fail(const char *m) {
    n_copy(http_msg, m, sizeof http_msg);
    if (http_sock >= 0) { tcp_free(http_sock); http_sock = -1; }
    http_st = H_ERR;
}
static int url_split(const char *u, char *host, int hmax, u16 *port, char *path, int pmax, int *https) {
    int tls = 0;
    if (n_starts(u, "http://")) u += 7;
    else if (n_starts(u, "https://")) { u += 8; tls = 1; }
    else { for (const char *c = u; *c && *c != '/'; c++) if (c[0] == ':' && c[1] == '/') return -1; }
    int h = 0;
    while (*u && *u != '/' && *u != ':' && *u != '?' && *u != '#') { if (h < hmax - 1) host[h++] = *u; u++; }
    host[h] = 0;
    if (!h) return -1;
    if (https) *https = tls;
    u32 pt = tls ? 443 : 80;
    if (*u == ':') { u++; pt = 0; while (*u >= '0' && *u <= '9') { pt = pt * 10 + (u32)(*u - '0'); if (pt > 65535) return -1; u++; } if (!pt) return -1; }
    *port = (u16)pt;
    int o = 0;
    if (*u == '?') path[o++] = '/';
    while (*u && *u != '#' && o < pmax - 1) path[o++] = *u++;
    if (!o) path[o++] = '/';
    path[o] = 0;
    return 0;
}
/* https:// pages go through a helper on the host PC (tools/leact-gateway.py): the request becomes
 * http://GATEWAY:PORT/https/HOST/PATH.  Turned off by default here; the kernel turns it on for QEMU. */
static int http_proxy_on;
static char http_proxy_host[24] = "10.0.2.2";
static u16 http_proxy_port = 8080;
static int http_via_proxy;
static void http_begin(void) {                              /* (re)start with http_url */
    http_len = 0; http_trunc = 0; http_code = 0; http_clen = -1; http_chunked = 0; http_cs = 0; http_cext = 0; http_csize = 0;
    http_ctype[0] = 0; http_loc[0] = 0; hhl = 0; http_rcvd = 0; http_nodata = 0;
    int tls = 0;
    http_via_proxy = 0;
    if (url_split(http_url, http_host, sizeof http_host, &http_port, http_path, sizeof http_path, &tls) < 0) { http_fail("Cannot understand that address"); return; }
    if (tls) {
        if (!http_proxy_on) { http_fail("HTTPS needs the gateway: run tools/leact-gateway.py on your PC (see: proxy)"); return; }
        char np[220]; int o = 0;
        const char *pre = "/https/";
        while (*pre) np[o++] = *pre++;
        for (const char *c = http_host; *c && o < 150; c++) np[o++] = *c;
        if (http_port != 443) { np[o++] = ':'; char t[8]; int l = 0; u32 v = http_port; while (v) { t[l++] = (char)('0' + v % 10); v /= 10; } while (l) np[o++] = t[--l]; }
        for (const char *c = http_path; *c && o < (int)sizeof np - 1; c++) np[o++] = *c;
        np[o] = 0;
        n_copy(http_path, np, sizeof http_path);
        n_copy(http_host, http_proxy_host, sizeof http_host);
        http_port = http_proxy_port;
        http_via_proxy = 1;
    }
    http_t = net_ms();
    if (!http_tcp_up) { http_fail("No network card found"); return; }
    n_copy(http_msg, "Starting network...", sizeof http_msg);
    http_st = H_DHCP;
}
static void http_start(const char *url) {
    if (http_sock >= 0) { tcp_free(http_sock); http_sock = -1; }
    http_redirs = 0;
    if (!n_starts(url, "http://") && !n_starts(url, "https://")) {
        int k = 0; const char *pre = "http://";
        while (pre[k]) { http_url[k] = pre[k]; k++; }
        n_copy(http_url + k, url, (int)sizeof http_url - k);
    } else n_copy(http_url, url, sizeof http_url);
    http_begin();
}
static void http_cancel(void) {
    if (http_sock >= 0) { tcp_free(http_sock); http_sock = -1; }
    if (http_st != H_DONE && http_st != H_ERR) { n_copy(http_msg, "Stopped", sizeof http_msg); http_st = H_ERR; }
}
static int http_active(void) { return http_st >= H_DHCP && http_st <= H_BODY; }
/* absolute URL from a base and a link (handles /abs, rel, ../, //host, ?q) */
static void url_join(const char *base, const char *rel, char *out, int max) {
    if (n_starts(rel, "http://") || n_starts(rel, "https://")) { n_copy(out, rel, max); return; }
    char host[100], path[220]; u16 port; int tls = 0;
    if (url_split(base, host, sizeof host, &port, path, sizeof path, &tls) < 0) { n_copy(out, rel, max); return; }
    char pre[130]; int o = 0;
    for (const char *s = tls ? "https://" : "http://"; *s; s++) pre[o++] = *s;
    for (const char *s = host; *s && o < 110; s++) pre[o++] = *s;
    if (port != (tls ? 443 : 80)) { pre[o++] = ':'; char t[8]; int l = 0; u32 v = port; while (v) { t[l++] = (char)('0' + v % 10); v /= 10; } while (l) pre[o++] = t[--l]; }
    pre[o] = 0;
    char tmp[420];
    if (rel[0] == '/' && rel[1] == '/') { int k = 0; for (const char *s = tls ? "https:" : "http:"; *s; s++) tmp[k++] = *s; n_copy(tmp + k, rel, (int)sizeof tmp - k); n_copy(out, tmp, max); return; }
    int k = 0;
    for (const char *s = pre; *s; s++) tmp[k++] = *s;
    if (rel[0] == '/') { n_copy(tmp + k, rel, (int)sizeof tmp - k); }
    else {
        int pl = n_len(path), cut = pl;
        while (cut > 0 && path[cut - 1] != '/') cut--;
        if (rel[0] == '?') { for (int i = 0; path[i] && path[i] != '?'; i++) tmp[k++] = path[i]; n_copy(tmp + k, rel, (int)sizeof tmp - k); }
        else {
            for (int i = 0; i < cut; i++) tmp[k++] = path[i];
            n_copy(tmp + k, rel, (int)sizeof tmp - k);
        }
    }
    /* squeeze ./ and ../ out of the path part */
    char res[420]; int r = 0, i = (int)n_len(pre);
    for (int j = 0; j < i; j++) res[r++] = tmp[j];
    int pstart = r;
    while (tmp[i]) {
        if (tmp[i] == '#') break;
        if (tmp[i] == '/' && tmp[i + 1] == '.' && tmp[i + 2] == '/') { i += 2; continue; }
        if (tmp[i] == '/' && tmp[i + 1] == '.' && (tmp[i + 2] == 0 || tmp[i + 2] == '?')) { i += 2; res[r++] = '/'; continue; }
        if (tmp[i] == '/' && tmp[i + 1] == '.' && tmp[i + 2] == '.' && (tmp[i + 3] == '/' || tmp[i + 3] == 0)) {
            while (r > pstart && res[r - 1] != '/') r--;
            if (r > pstart) r--;
            i += 3; if (!tmp[i]) res[r++] = '/';
            continue;
        }
        if (r < (int)sizeof res - 1) res[r++] = tmp[i];
        i++;
    }
    res[r] = 0;
    n_copy(out, res, max);
}
static int http_ieq_prefix(const char *line, const char *name) { return n_starts(line, name); }
static void http_store(const u8 *d, u32 n) {
    u32 room = HTTP_MAX - http_len;
    if (n > room) { n = room; http_trunc = 1; }
    memcpy(http_body + http_len, d, n); http_len += n;
}
static void http_finish(void) {
    if (http_sock >= 0) { tcp_close(http_sock); tcp_free(http_sock); http_sock = -1; }
    n_copy(http_msg, "Done", sizeof http_msg);
    http_st = H_DONE;
}
/* feed body bytes; returns 1 when the body is complete */
static int http_feed(const u8 *d, int n) {
    if (http_nodata) return 1;
    if (!http_chunked) {
        u32 take = (u32)n;
        if (http_clen >= 0 && http_rcvd + take > (u32)http_clen) take = (u32)http_clen - http_rcvd;
        http_store(d, take); http_rcvd += take;
        return http_clen >= 0 && http_rcvd >= (u32)http_clen;
    }
    for (int i = 0; i < n;) {
        u8 c = d[i];
        if (http_cs == 0) {                                   /* chunk-size line */
            i++;
            if (c == '\n') { if (http_csize == 0) return 1; http_cs = 1; }
            else if (!http_cext && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                http_csize = http_csize * 16 + (u32)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
            else if (c != '\r') http_cext = 1;
        } else if (http_cs == 1) {                            /* chunk data */
            u32 take = (u32)(n - i);
            if (take > http_csize) take = http_csize;
            http_store(d + i, take); http_rcvd += take; i += (int)take; http_csize -= take;
            if (!http_csize) http_cs = 2;
        } else {                                              /* CRLF after the data */
            i++;
            if (c == '\n') { http_cs = 0; http_cext = 0; http_csize = 0; }
        }
    }
    return 0;
}
static void http_parse_head(void) {
    hhead[hhl] = 0;
    const char *h = (const char *)hhead;
    http_code = 0;
    if (n_starts(h, "HTTP/")) { const char *c = h; while (*c && *c != ' ') c++; while (*c == ' ') c++; while (*c >= '0' && *c <= '9') { http_code = http_code * 10 + (*c - '0'); c++; } }
    if (!http_code) { http_fail("Not an HTTP server"); return; }
    for (const char *l = h; *l;) {
        const char *e = l; while (*e && *e != '\n') e++;
        if (http_ieq_prefix(l, "content-length:")) { l += 15; while (*l == ' ') l++; i32 v = 0; while (*l >= '0' && *l <= '9') { v = v * 10 + (*l - '0'); l++; } http_clen = v; }
        else if (http_ieq_prefix(l, "transfer-encoding:")) { const char *c = l + 18; while (c < e) { if (n_starts(c, "chunked")) http_chunked = 1; c++; } }
        else if (http_ieq_prefix(l, "content-type:")) {
            l += 13; while (*l == ' ') l++;
            int o = 0; while (l < e && *l != ';' && *l != '\r' && o < (int)sizeof http_ctype - 1) http_ctype[o++] = n_lc(*l++);
            http_ctype[o] = 0;
        } else if (http_ieq_prefix(l, "location:")) {
            l += 9; while (*l == ' ') l++;
            int o = 0; while (l < e && *l != '\r' && o < (int)sizeof http_loc - 1) http_loc[o++] = *l++;
            http_loc[o] = 0;
        }
        l = *e ? e + 1 : e;
    }
    if (http_code == 204 || http_code == 304 || (http_code >= 100 && http_code < 200)) http_nodata = 1;
    if ((http_code == 301 || http_code == 302 || http_code == 303 || http_code == 307 || http_code == 308) && http_loc[0]) {
        if (++http_redirs > 6) { http_fail("Too many redirects"); return; }
        char nu[320]; url_join(http_url, http_loc, nu, sizeof nu);
        if (http_sock >= 0) { tcp_free(http_sock); http_sock = -1; }
        n_copy(http_url, nu, sizeof http_url);
        http_begin();
        return;
    }
    http_st = H_BODY;
}
static void http_tick(void) {
    u32 now = net_ms();
    if (!http_active()) return;
    if (now - http_t > 20000) { http_fail("The server did not answer in time"); return; }
    switch (http_st) {
    case H_DHCP:
        if (net_ip) { n_copy(http_msg, "Looking up host...", sizeof http_msg); http_st = H_DNS; dns_start(http_host); http_t = now; }
        else if (dhcp_state == 0 || dhcp_state == 4) { if (dhcp_state == 4 && now - http_t > 1000) { http_fail("No network address (DHCP failed)"); return; } if (dhcp_state == 0) dhcp_start(); }
        break;
    case H_DNS:
        if (dns_state == 2) {
            http_sock = tcp_open(dns_ip, http_port);
            if (http_sock < 0) { http_fail("Out of network connections"); return; }
            n_copy(http_msg, "Connecting...", sizeof http_msg); http_st = H_CONN; http_t = now;
        } else if (dns_state == 3) { http_fail(dns_nx ? "Host not found" : "Name lookup failed"); return; }
        else if (dns_state == 0) dns_start(http_host);
        break;
    case H_CONN: {
        struct tcb *s = &tcps[http_sock];
        if (tcp_connected(http_sock)) {
            char req[420]; int o = 0;
            const char *parts[] = { "GET ", http_path, " HTTP/1.1\r\nHost: ", http_host, "\r\nUser-Agent: LeactedOS/7\r\nAccept: */*\r\nConnection: close\r\n\r\n" };
            for (int k = 0; k < 5; k++) for (const char *c = parts[k]; *c && o < (int)sizeof req - 1; c++) req[o++] = *c;
            if (tcp_write(http_sock, (const u8 *)req, o) != o) { http_fail("Request too long"); return; }
            n_copy(http_msg, "Waiting for the server...", sizeof http_msg); http_st = H_HEAD; http_t = now;
        } else if (s->st == T_CLOSED) {
            if (http_via_proxy) http_fail("Gateway not reachable: run tools/leact-gateway.py on your PC");
            else http_fail(s->err == 1 ? "Connection refused" : s->err == 3 ? "Could not connect (timed out)" : "Connection failed");
        }
        break;
    }
    case H_HEAD: case H_BODY: {
        u8 buf[1024]; int n, got = 0;
        while (http_active() && (n = tcp_read(http_sock, buf, sizeof buf)) > 0) {
            got = 1;
            int off = 0;
            while (off < n && http_st == H_HEAD) {
                if (hhl >= (int)sizeof hhead - 1) { http_fail("Server header too large"); return; }
                hhead[hhl++] = buf[off++];
                if (hhl >= 4 && hhead[hhl - 1] == '\n' && hhead[hhl - 2] == '\r' && hhead[hhl - 3] == '\n' && hhead[hhl - 4] == '\r') http_parse_head();
            }
            if (http_st == H_BODY) {
                if (http_nodata || http_feed(buf + off, n - off)) { http_finish(); return; }
                n_copy(http_msg, "Loading...", sizeof http_msg);
            }
            /* a redirect restarted the request (H_DHCP..): drop whatever else the old connection sent */
            if (http_st != H_HEAD && http_st != H_BODY) break;
        }
        if (got) http_t = now;
        if (!http_active()) return;
        if (http_st == H_BODY && (http_nodata || (http_clen == 0 && !http_chunked))) { http_finish(); return; }
        if (http_st == H_HEAD || http_st == H_BODY) {
            if (tcp_eof(http_sock)) {
                if (http_st == H_HEAD) http_fail(tcps[http_sock].err == 2 ? "Connection reset by the server" : "The server closed the connection");
                else http_finish();
            }
        }
        break;
    }
    }
}

static void net_poll(void) {
    if (!net_have_nic) return;
    int n, guard = 0;
    while (guard++ < 48 && (n = nic_recv(net_rxb, sizeof net_rxb)) > 0) net_rx(net_rxb, n);
    dhcp_tick(); dns_tick(); tcp_timer(); http_tick();
}
#endif
