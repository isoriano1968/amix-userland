"""Serve Packages: this computer as the package server of AMIX machines.

The AMIX package repository (an IPS repository, publisher amix) served
read-only over HTTP at /repo/, as the network install serves it, so that
AMIX's pkg installs and updates from it:

    pkg set-publisher -O http://10.0.2.2:8069/repo amix    (FS-UAE here)
    pkg set-publisher -O http://<this computer>:8069/repo amix

and an answer to the AMIX Software Center's "Find on Network": a UDP
request "AMIX-REPO?" on port 8070 gets

    AMIX-REPO 1 port=8069 path=/repo packages=95 date=2026.11.20 name=<host>

The Amiga makes the address from where the answer came from (10.0.2.2 in
FS-UAE).  Replaces the hand-run "python3 -m http.server" of 2026-11."""
import datetime
import http.server
import os
import socket
import socketserver
import threading
import time
import urllib.parse

HTTP_PORT = 8069
DISCOVERY_PORT = 8070
REQUEST = b"AMIX-REPO?"
FS_UAE_HOST = "10.0.2.2"     # FS-UAE's slirp: this computer, as AMIX in it sees it


def package_count(repo):
    d = os.path.join(repo, "publisher", "amix", "pkg")
    try:
        return len([n for n in os.listdir(d) if not n.startswith(".")])
    except OSError:
        return 0


def repo_date(repo):
    """The catalog's date, YYYY.MM.DD (what the packages are as of)."""
    f = os.path.join(repo, "publisher", "amix", "catalog", "catalog.attrs")
    try:
        t = os.stat(f).st_mtime
    except OSError:
        return "?"
    return datetime.datetime.fromtimestamp(t).strftime("%Y.%m.%d")


def lan_addresses():
    """This computer's IPv4 addresses an Amiga on the network can reach:
    the one of the default route (no packet is sent), then the others the
    host name resolves to."""
    out = []
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))          # TEST-NET-1: only picks the route
        out.append(s.getsockname()[0])
    except OSError:
        pass
    finally:
        s.close()
    try:
        for a in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = a[4][0]
            if ip not in out and not ip.startswith("127."):
                out.append(ip)
    except OSError:
        pass
    return out


def answer(port, path, repo):
    return ("AMIX-REPO 1 port=%d path=%s packages=%d date=%s name=%s\n"
            % (port, path, package_count(repo), repo_date(repo),
               socket.gethostname().split(".")[0] or "computer")).encode()


class _Handler(http.server.BaseHTTPRequestHandler):
    server_version = "amix-installer"

    def log_message(self, fmt, *a):
        pass

    def _reply(self, code, body=b"", ctype="text/plain"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_GET(self):
        srv = self.server.owner
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        if path.startswith("/repo/"):
            rel = os.path.normpath(path[len("/repo/"):])
            if rel.startswith("..") or os.path.isabs(rel):
                return self._reply(403)
            f = os.path.join(srv.repo, rel)
            if os.path.isfile(f):
                with open(f, "rb") as fh:
                    data = fh.read()
                self._reply(200, data, "application/octet-stream")
                srv.logged(self.client_address[0], rel, 200, len(data) if self.command == "GET" else 0)
                return
            srv.logged(self.client_address[0], rel, 404, 0)
            return self._reply(404, b"not found\n")
        if path in ("/", "/repo"):
            return self._reply(200, ("AMIX package repository: %d packages, %s\n"
                                     % (package_count(srv.repo), repo_date(srv.repo))).encode())
        return self._reply(404, b"not found\n")

    do_HEAD = do_GET

    def do_PUT(self):
        self._reply(403)

    do_POST = do_PUT


class _HttpServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class RepoServer(object):
    """The repository on HTTP and the discovery answer, until stop()."""

    def __init__(self, repo, port=HTTP_PORT, local=False, discovery=True,
                 discovery_port=DISCOVERY_PORT, log=None):
        self.repo = os.path.abspath(repo)
        self.port = port
        self.local = local
        self.discovery = discovery
        self.discovery_port = discovery_port
        self.log = log
        self.bytes = 0
        self.requests = 0
        self._http = None
        self._udp = None
        self._threads = []
        self._stop = threading.Event()
        self._lock = threading.Lock()

    @property
    def bind(self):
        return "127.0.0.1" if self.local else "0.0.0.0"

    def start(self):
        """Both listeners, or OSError (the port in use: another server)."""
        self._http = _HttpServer((self.bind, self.port), _Handler)
        self._http.owner = self
        self.port = self._http.server_address[1]
        t = threading.Thread(target=self._http.serve_forever, name="repo-http", daemon=True)
        t.start()
        self._threads.append(t)
        if self.discovery:
            try:
                self._udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self._udp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                self._udp.bind((self.bind, self.discovery_port))
                self.discovery_port = self._udp.getsockname()[1]
                self._udp.settimeout(0.5)
            except OSError:
                self.stop()
                raise
            t = threading.Thread(target=self._discovery, name="repo-discovery", daemon=True)
            t.start()
            self._threads.append(t)
        return self

    def _discovery(self):
        while not self._stop.is_set():
            try:
                data, addr = self._udp.recvfrom(512)
            except socket.timeout:
                continue
            except OSError:
                return
            if data.strip().startswith(REQUEST):
                try:
                    self._udp.sendto(answer(self.port, "/repo", self.repo), addr)
                except OSError:
                    pass
                if self.log:
                    self.log(time.strftime("%H:%M:%S"), addr[0], "Find on Network", "answered", 0)

    def logged(self, client, rel, code, nbytes):
        with self._lock:
            self.requests += 1
            self.bytes += nbytes
        if self.log:
            self.log(time.strftime("%H:%M:%S"), client, rel, code, nbytes)

    def stop(self):
        self._stop.set()
        if self._http is not None:
            self._http.shutdown()
            self._http.server_close()
            self._http = None
        if self._udp is not None:
            try:
                self._udp.close()
            except OSError:
                pass
            self._udp = None

    def urls(self):
        """(what, url) for the address lines: FS-UAE here, then the network."""
        out = [("FS-UAE on this computer", "http://%s:%d/repo" % (FS_UAE_HOST, self.port))]
        if not self.local:
            out += [("An Amiga on the network", "http://%s:%d/repo" % (ip, self.port))
                    for ip in lan_addresses()]
        return out


def command(url):
    """What to type on AMIX."""
    return "pkg set-publisher -O %s amix" % url


def _say(text):
    print(text, flush=True)


def main_cli(repo, port=HTTP_PORT, local=False, out=_say):
    """amix-installer --serve: until Ctrl-C (or SIGTERM)."""
    import signal
    from . import plan

    def term(_sig, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, term)
    why = plan.repo_problem(repo)
    if why:
        out("amix-installer: %s" % why)
        return 1

    def log(t, client, rel, code, nbytes):
        out("%s  %-15s %-48s %s%s" % (t, client, rel[:48], code,
                                      "  %d bytes" % nbytes if nbytes else ""))
    srv = RepoServer(repo, port=port, local=local, log=log)
    try:
        srv.start()
    except OSError as e:
        out("amix-installer: cannot serve on port %d: %s (another server?)" % (port, e.strerror or e))
        return 1
    out("Serving %d packages (%s) from %s" % (package_count(repo), repo_date(repo), srv.repo))
    out("Type this on AMIX:")
    for what, url in srv.urls():
        out("  %-24s %s" % (what + ":", command(url)))
    out("Find on Network answers on UDP port %d.  Ctrl-C stops." % srv.discovery_port)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        out("stopped: %d requests, %d bytes" % (srv.requests, srv.bytes))
    finally:
        srv.stop()
    return 0
