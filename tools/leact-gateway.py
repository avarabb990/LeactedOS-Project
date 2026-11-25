#!/usr/bin/env python3
"""leact-gateway.py - lets LeactedOS reach https:// sites (GitHub, ...) until it has its own TLS.

Run this on the PC that runs QEMU:

    python3 tools/leact-gateway.py            # listens on 127.0.0.1:8080

QEMU's user network makes the PC reachable from the OS as 10.0.2.2, so LeactedOS asks
http://10.0.2.2:8080/https/HOST/PATH and this script fetches https://HOST/PATH for it
(with real certificate checking) and hands the answer back as plain HTTP.  The Browser,
`wget` and `leact-get` use it automatically for every https:// address.

Safety: it only answers GET/HEAD, only listens on the loopback address, and refuses to
fetch private / local network addresses unless you pass --allow-private.  Redirects are not
followed here; the OS follows them itself (the Location header is made absolute).
If HTTPS_PROXY is set in your environment the gateway tunnels through that proxy.
Python 3.7+, standard library only.
"""
import argparse
import http.client
import ipaddress
import os
import re
import socket
import ssl
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 8 * 1024 * 1024
HOST_RE = re.compile(r"^[A-Za-z0-9.-]{1,253}(:[0-9]{1,5})?$")


class Gateway(BaseHTTPRequestHandler):
    server_version = "LeactGateway/1"
    protocol_version = "HTTP/1.0"
    allow_private = False
    verbose = True

    def log_message(self, fmt, *args):
        if self.verbose:
            sys.stdout.write("%s - %s\n" % (self.address_string(), fmt % args))
            sys.stdout.flush()

    def _reply(self, code, text, ctype="text/plain; charset=utf-8"):
        body = (text + "\n").encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _blocked(self, host):
        try:
            infos = socket.getaddrinfo(host, None)
        except OSError:
            return False                     # let the real fetch report the DNS error
        for info in infos:
            ip = ipaddress.ip_address(info[4][0])
            if ip.is_private or ip.is_loopback or ip.is_link_local or ip.is_reserved or ip.is_multicast:
                return True
        return False

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        m = re.match(r"^/(https|http)/([^/?]+)(/.*)?$", self.path)
        if not m:
            return self._reply(400, "LeactedOS gateway: use /https/HOST/PATH or /http/HOST/PATH")
        scheme, host, rest = m.group(1), m.group(2), m.group(3) or "/"
        if "?" in self.path and "?" not in rest:                  # /https/host?x=1
            rest = "/" + self.path[self.path.index("?"):]
        if not HOST_RE.match(host):
            return self._reply(400, "bad host name")
        hostname = host.split(":")[0]
        if not self.allow_private and self._blocked(hostname):
            return self._reply(403, "refused: %s is a private or local address (start the gateway with --allow-private to allow it)" % hostname)
        port = int(host.split(":")[1]) if ":" in host else (443 if scheme == "https" else 80)
        try:
            if scheme == "https":
                ctx = ssl.create_default_context()
                px = urllib.parse.urlparse(os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy") or "")
                if px.hostname:                                    # behind a proxy: tunnel through it (CONNECT)
                    conn = http.client.HTTPSConnection(px.hostname, px.port or 3128, timeout=15, context=ctx)
                    conn.set_tunnel(hostname, port)
                else:
                    conn = http.client.HTTPSConnection(hostname, port, timeout=15, context=ctx)
            else:
                conn = http.client.HTTPConnection(hostname, port, timeout=15)
            conn.request("GET" if self.command == "GET" else "HEAD", rest, headers={
                "Host": host if ":" in host else hostname,
                "User-Agent": "LeactedOS-gateway/1",
                "Accept": "*/*",
                "Accept-Encoding": "identity",
                "Connection": "close",
            })
            resp = conn.getresponse()
            body = resp.read(MAX_BODY + 1) if self.command == "GET" else b""
            if len(body) > MAX_BODY:
                conn.close()
                return self._reply(502, "refused: the page is bigger than %d MB" % (MAX_BODY // 1048576))
        except ssl.SSLCertVerificationError as e:
            return self._reply(502, "certificate check failed for %s: %s" % (hostname, e.verify_message))
        except (OSError, http.client.HTTPException) as e:
            return self._reply(502, "could not fetch %s://%s: %s" % (scheme, host, e))
        self.send_response(resp.status, resp.reason)
        for name in ("Content-Type", "Last-Modified", "ETag"):
            if resp.getheader(name):
                self.send_header(name, resp.getheader(name))
        loc = resp.getheader("Location")
        if loc:                                                    # make it absolute so the OS can follow it
            self.send_header("Location", urllib.parse.urljoin("%s://%s%s" % (scheme, host, rest), loc))
        self.send_header("Content-Length", str(len(body) if self.command == "GET" else int(resp.getheader("Content-Length") or 0)))
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command == "GET":
            self.wfile.write(body)
        conn.close()


def main():
    ap = argparse.ArgumentParser(description="HTTPS gateway for LeactedOS")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--bind", default="127.0.0.1", help="address to listen on (default: loopback only)")
    ap.add_argument("--allow-private", action="store_true", help="also fetch local / private network addresses")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    Gateway.allow_private = a.allow_private
    Gateway.verbose = not a.quiet
    srv = ThreadingHTTPServer((a.bind, a.port), Gateway)
    print("LeactedOS gateway on http://%s:%d  (in the OS this is http://10.0.2.2:%d)  Ctrl+C stops it" % (a.bind, a.port, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
