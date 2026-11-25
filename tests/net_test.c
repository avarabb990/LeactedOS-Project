/* net_test.c - runs the real net.h stack (ASan, 64-bit) against a fake network.
 * The fake wire is a DHCP server, DNS server, gateway/ARP responder, ICMP echo
 * responder and a small HTTP server with its own TCP, with optional packet loss.
 *   gcc -m64 -O1 -g -fsanitize=address,undefined -I. -o tests/net_test tests/net_test.c && ./tests/net_test
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32;

static u32 now_ms;
static u32 rng = 12345;
static int nic_send(const u8 *f, int n);
static int nic_recv(u8 *b, int max);
static u32 net_ms(void) { return now_ms; }
static u32 net_rand(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
#include "net.h"


#include "tests/fakenet.h"
static int nic_send(const u8 *f, int n) { return fake_send(f, n); }
static int nic_recv(u8 *b, int max) { return fake_recv(b, max); }

/* ---------------- test helpers ---------------- */
static void run_ms(int ms) { for (int t = 0; t < ms; t += 5) { net_poll(); srv_tick(); now_ms += 5; } }
static int run_until_http(int max_ms) {
    for (int t = 0; t < max_ms; t += 5) { net_poll(); srv_tick(); now_ms += 5; if (!http_active()) return t; }
    return -1;
}
static void fetch(const char *url) { http_start(url); }
static int body_is(const char *s) { return http_len == strlen(s) && !memcmp(http_body, s, http_len); }
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s  (http_st=%d msg='%s' code=%d len=%u)\n", __LINE__, #c, http_st, http_msg, http_code, http_len); exit(1); } } while (0)
static void reset_wire(void) { q_h = q_t = 0; sv.active = 0; drop_nth_data = data_count = drop_all_data = 0; srv_server_stalls = 0; }

int main(void) {
    memcpy(net_mac, "\x52\x54\x00\x12\x34\x56", 6);
    net_have_nic = 1; http_tcp_up = 1;
    /* ---- helpers ---- */
    u32 v; CHECK(net_parse_ip("10.0.2.15", &v) && v == IP(10, 0, 2, 15)); CHECK(!net_parse_ip("10.0.2", &v)); CHECK(!net_parse_ip("1.2.3.256", &v)); CHECK(!net_parse_ip("a.b.c.d", &v)); CHECK(!net_parse_ip("1.2.3.4x", &v));
    char fm[20]; net_fmt_ip(fm, IP(192, 168, 1, 100)); CHECK(!strcmp(fm, "192.168.1.100")); net_fmt_ip(fm, IP(0, 0, 0, 0)); CHECK(!strcmp(fm, "0.0.0.0"));
    char o[400];
    url_join("http://a.test/x/y/z.html", "w.html", o, sizeof o); CHECK(!strcmp(o, "http://a.test/x/y/w.html"));
    url_join("http://a.test/x/y/z.html", "../w.html", o, sizeof o); CHECK(!strcmp(o, "http://a.test/x/w.html"));
    url_join("http://a.test/x/y/z.html", "../../../w.html", o, sizeof o); CHECK(!strcmp(o, "http://a.test/w.html"));
    url_join("http://a.test/x/y/z.html", "/abs", o, sizeof o); CHECK(!strcmp(o, "http://a.test/abs"));
    url_join("http://a.test:8080/x/", "p?q=1", o, sizeof o); CHECK(!strcmp(o, "http://a.test:8080/x/p?q=1"));
    url_join("http://a.test/x/y.html?a=1", "?b=2", o, sizeof o); CHECK(!strcmp(o, "http://a.test/x/y.html?b=2"));
    url_join("http://a.test/x/", "//b.test/k", o, sizeof o); CHECK(!strcmp(o, "http://b.test/k"));
    url_join("http://a.test/x/", "./k/./m", o, sizeof o); CHECK(!strcmp(o, "http://a.test/x/k/m"));
    url_join("http://a.test", "k", o, sizeof o); CHECK(!strcmp(o, "http://a.test/k"));
    url_join("http://a.test/x/", "https://s.test/", o, sizeof o); CHECK(!strcmp(o, "https://s.test/"));
    url_join("http://a.test/x/", "k#frag", o, sizeof o); CHECK(!strcmp(o, "http://a.test/x/k"));

    /* ---- DHCP ---- */
    CHECK(dhcp_state == 0); dhcp_start(); run_ms(200);
    CHECK(dhcp_state == 3 && net_ip == cli_ip && net_mask == 0xFFFFFF00u && net_gw == GW && net_dns == DNSIP);
    printf("dhcp ok: ip=%08x\n", net_ip);

    /* ---- ping on-link and off-link (ARP through the gateway) ---- */
    ping_send(LOCAL, 1); run_ms(100); CHECK(ping_got == 1 && ping_from == LOCAL);
    ping_send(REMOTE, 2); run_ms(100); CHECK(ping_got == 2 && ping_from == REMOTE);
    ping_send(DEAD, 3); run_ms(3000); CHECK(ping_got == -1);
    printf("ping ok\n");

    /* ---- DNS ---- */
    dns_start("local.test"); run_ms(100); CHECK(dns_state == 2 && dns_ip == LOCAL);
    dns_start("www.example.test"); run_ms(100); CHECK(dns_state == 2 && dns_ip == REMOTE);     /* CNAME then A */
    int dq = dns_queries; dns_start("local.test"); CHECK(dns_state == 2 && dns_ip == LOCAL && dns_queries == dq);   /* cached */
    dns_start("nosuch.test"); run_ms(100); CHECK(dns_state == 3 && dns_nx);
    dns_start("1.2.3.4"); CHECK(dns_state == 2 && dns_ip == IP(1, 2, 3, 4));
    printf("dns ok\n");

    /* ---- HTTP basics ---- */
    reset_wire();
    fetch("http://local.test/"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && http_code == 200);
    CHECK(body_is("<html><title>Hi</title><body>Hello from the fake server</body></html>")); CHECK(!strcmp(http_ctype, "text/html"));
    fetch("local.test"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && http_code == 200);       /* scheme optional */
    fetch("http://10.0.2.100:80/chunked"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE);
    CHECK(body_is("Hello, chunked world, in 3 chunks ok.")); CHECK(http_chunked);
    fetch("http://example.test/final"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && body_is("<b>final page</b>\n"));   /* via the gateway */
    fetch("http://local.test/redir"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && body_is("<b>final page</b>\n")); CHECK(!strcmp(http_url, "http://local.test/final"));
    fetch("http://local.test/redir2"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && body_is("<b>final page</b>\n")); CHECK(strstr(http_url, "www.example.test/final"));
    fetch("http://local.test/loop"); CHECK(run_until_http(8000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "redirects"));
    fetch("http://local.test/tls"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "HTTPS"));
    fetch("https://local.test/"); CHECK(http_st == H_ERR && strstr(http_msg, "HTTPS"));
    fetch("http://local.test/missing"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && http_code == 404 && strstr((char *)http_body, "Not Found"));
    fetch("http://local.test/close"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && body_is("no length, ends when the server closes"));
    fetch("http://local.test/nodata"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && http_code == 204 && http_len == 0);
    printf("http basics ok\n");

    /* ---- bigger transfers: windowing, flow control ---- */
    int t = (fetch("http://local.test/big"), run_until_http(20000)); CHECK(t >= 0); CHECK(http_st == H_DONE && http_len == 100000);
    for (int i = 0; i < 100000; i++) if (http_body[i] != pat(i)) { printf("big body mismatch at %d\n", i); exit(1); }
    printf("100 KB in %d ms (fake clock), %d server frames\n", t, srv_tx_frames);
    fetch("http://local.test/huge"); CHECK(run_until_http(30000) >= 0); CHECK(http_st == H_DONE && http_len == HTTP_MAX && http_trunc);   /* cut at the limit but finishes */
    for (int i = 0; i < 5000; i++) if (http_body[i] != pat(i)) { printf("huge body mismatch\n"); exit(1); }

    /* ---- packet loss: one dropped data segment must be repaired ---- */
    reset_wire(); srv_retrans = 0;
    fetch("http://local.test/big"); drop_nth_data = 12; CHECK(run_until_http(30000) >= 0);
    CHECK(http_st == H_DONE && http_len == 100000); for (int i = 0; i < 100000; i++) CHECK(http_body[i] == pat(i));
    CHECK(srv_retrans >= 1);
    printf("loss repaired (server retransmitted %d)\n", srv_retrans);
    /* the first data segment lost: client must time the page out or recover via the dup-ack path */
    reset_wire(); fetch("http://local.test/big"); drop_nth_data = 1; CHECK(run_until_http(30000) >= 0);
    CHECK(http_st == H_DONE && http_len == 100000);

    /* ---- failures ---- */
    reset_wire(); fetch("http://nosuch.test/"); CHECK(run_until_http(8000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "not found"));
    fetch("http://local.test:81/"); CHECK(run_until_http(8000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "refused"));
    fetch("http://dead.test/"); CHECK(run_until_http(60000) >= 0); CHECK(http_st == H_ERR && (strstr(http_msg, "connect") || strstr(http_msg, "in time")));
    reset_wire(); srv_server_stalls = 1; fetch("http://local.test/"); CHECK(run_until_http(60000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "in time"));
    reset_wire();
    fetch("http://local.test/"); net_poll(); net_poll(); CHECK(http_active()); http_cancel(); CHECK(http_st == H_ERR && !strcmp(http_msg, "Stopped")); run_ms(100);
    fetch("http://local.test/"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE);          /* works again after a cancel */
    for (int i = 0; i < NT; i++) CHECK(!tcps[i].used);                                                 /* no sockets leaked */
    printf("failures ok\n");

    /* ---- https through the host gateway (10.0.2.2:8080) ---- */
    char u2[400];
    url_join("https://a.test/x/y", "z", u2, sizeof u2); CHECK(!strcmp(u2, "https://a.test/x/z"));
    url_join("https://a.test:8443/x", "/k", u2, sizeof u2); CHECK(!strcmp(u2, "https://a.test:8443/k"));
    url_join("https://a.test/", "//b.test/k", u2, sizeof u2); CHECK(!strcmp(u2, "https://b.test/k"));
    url_join("http://a.test/", "//b.test/k", u2, sizeof u2); CHECK(!strcmp(u2, "http://b.test/k"));
    url_join("https://a.test:443/p", "q", u2, sizeof u2); CHECK(!strcmp(u2, "https://a.test/q"));
    reset_wire(); http_proxy_on = 0;
    fetch("https://secure.test/final"); CHECK(http_st == H_ERR && strstr(http_msg, "gateway"));
    http_proxy_on = 1; fake_gw_hits = 0;
    fetch("https://secure.test/final"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && body_is("<b>final page</b>\n"));
    CHECK(http_via_proxy && !strcmp(fake_gw_host, "secure.test") && fake_gw_tls && !strcmp(http_url, "https://secure.test/final"));
    fetch("https://secure.test:8443/final?x=1"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && !strcmp(fake_gw_host, "secure.test:8443"));
    fetch("http://local.test/final"); CHECK(run_until_http(5000) >= 0); CHECK(http_st == H_DONE && !http_via_proxy && fake_gw_hits == 2);   /* plain http stays direct */
    fetch("http://local.test/tls"); CHECK(run_until_http(5000) >= 0);                                  /* http -> https redirect follows through the gateway */
    CHECK(http_st == H_DONE && !strcmp(http_url, "https://secure.test/") && !strcmp(fake_gw_host, "secure.test") && strstr((char *)http_body, "Hello from the fake server"));
    fetch("https://secure.test/big"); CHECK(run_until_http(20000) >= 0); CHECK(http_st == H_DONE && http_len == 100000);
    fake_gw_down = 1; fetch("https://secure.test/"); CHECK(run_until_http(8000) >= 0); CHECK(http_st == H_ERR && strstr(http_msg, "Gateway not reachable")); fake_gw_down = 0;
    http_proxy_on = 0;
    printf("gateway ok\n");

    /* ---- garbage in must not crash ---- */
    for (int i = 0; i < 3000; i++) {
        u8 g[200]; int n = 14 + (int)(net_rand() % 150);
        for (int k = 0; k < n; k++) g[k] = (u8)net_rand();
        memcpy(g, net_mac, 6); if (i & 1) { nwr16(g + 12, 0x0800); g[14] = 0x45; } if (i & 2) { g[23] = (i & 4) ? 6 : (i & 8) ? 17 : 1; }
        net_rx(g, n);
    }
    printf("ALL NET TESTS PASSED\n");
    return 0;
}
