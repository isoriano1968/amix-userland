"""The network install (0.4): AMIX installs itself over its A2065.

The installer serves, while the install runs:
  TFTP  UDP 1069  "go" and "amixboot.pkg" (read-only; replies from the
                  listening port, as FS-UAE's slirp NAT needs)
  HTTP  TCP 8069  /repo/...    the IPS repository (read-only)
                  /steps       the step script for this install
                  /report/...  AMIX posts its progress here (PUT/POST)
At the AMIX console the user types (as root):
  tftp HOST 1069     binary     get go     quit     sh go
"go" installs the bootstrap (pkg, curl and their libraries, an SVR4
datastream) with stock pkgadd into /opt/amixboot, fetches /steps and runs
it: the real package client through IPS, the data disk (AMIX's mkfs),
the packages, the kernel last; at the end the bootstrap is removed (pkgrm).
Only AMIX's own tools touch AMIX's file system.
"""
import gzip
import http.server
import json
import os
import socket
import socketserver
import struct
import threading
import time
import urllib.parse

from . import model, svr4

TFTP_PORT = 1069
HTTP_PORT = 8069
FS_UAE_HOST = "10.0.2.2"            # the PC as FS-UAE's slirp NAT shows it to AMIX
BOOT_PKG = "AMIXboot"
BOOT_DIR = "/opt/amixboot"

# option id -> IPS packages (components); options without packages yet are
# reported, not installed
PACKAGES = {
    "cde": ["desktop/cde"], "mwm": ["desktop/motif"], "netsurf": ["web/netsurf"],
    "media": ["multimedia/ffmpeg", "desktop/cde-apps"], "grafx2": ["graphics/grafx2"],
    "sysmon": ["desktop/cde-apps"], "games": ["games/quake", "games/ioquake3"],
    "gcc14": ["developer/gcc-14"], "gcc27": ["developer/gcc-27"],
    "pkgconf": ["developer/pkgconf"], "gnu": ["gnu/gnu-tools"], "python": ["runtime/python3"],
    "vim": ["editor/vim"],
}
# what the bootstrap carries, from these packages of the repository
BOOT_FILES = {
    "sysutils/amix-pkg": {"usr/bin/pkg": "bin/pkg"},
    "network/curl": {"usr/local/bin/curl": "bin/curl", "usr/local/lib/libcurl.so.4": "lib/libcurl.so.4"},
    "library/openssl": {"usr/local/lib/libssl.so.3": "lib/libssl.so.3",
                        "usr/local/lib/libcrypto.so.3": "lib/libcrypto.so.3"},
    "library/zlib": {"usr/local/lib/libz.so.1": "lib/libz.so.1"},
}


def lan_address():
    """The PC's address on the LAN (for a real Amiga)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))         # no packet is sent: picks the outgoing interface
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


# --- the repository ---------------------------------------------------------

def _newest(repo, stem):
    d = os.path.join(repo, "publisher", "amix", "pkg", urllib.parse.quote(stem, safe=""))
    if not os.path.isdir(d):
        raise KeyError(stem)
    versions = sorted(urllib.parse.unquote(v) for v in os.listdir(d) if not v.endswith(".part"))
    if not versions:
        raise KeyError(stem)
    # the newest: by release, branch, timestamp (the catalog order is good enough here)
    def key(v):
        rel, _, rest = v.partition(",")
        branch = rest.split("-", 1)[1].split(":")[0] if "-" in rest else "0"
        ts = v.split(":")[-1] if ":" in v else ""
        return ([int(x) for x in rel.split(".") if x.isdigit()],
                [int(x) for x in branch.split(".") if x.isdigit()], ts)
    v = max(versions, key=key)
    with open(os.path.join(d, urllib.parse.quote(v, safe=""))) as f:
        return v, f.read()


def payload(repo, manifest, path):
    """The content of path in a published manifest, from the repository."""
    for line in manifest.splitlines():
        w = line.split()
        if len(w) > 2 and w[0] == "file" and ("path=" + path) in w:
            h = w[1]
            with gzip.open(os.path.join(repo, "publisher", "amix", "file", h[:2], h)) as f:
                return f.read()
    raise KeyError(path)


def bootstrap(repo):
    """The SVR4 datastream AMIXboot, made of files from the repository."""
    E = svr4.Entry
    ents = [E("bin", "d"), E("lib", "d")]
    versions = []
    for stem, files in BOOT_FILES.items():
        v, man = _newest(repo, stem)
        versions.append("%s@%s" % (stem, v.split(":")[0]))
        for src, dst in files.items():
            ents.append(E(dst, "f", 0o555, data=payload(repo, man, src)))
    ents = ents[:2] + sorted(ents[2:], key=lambda e: e.path)
    info = {"NAME": "AMIX Installer bootstrap (removed again at the end)",
            "VERSION": "1", "BASEDIR": BOOT_DIR, "DESC": ", ".join(versions),
            "VENDOR": "amix-userland"}
    return svr4.datastream(BOOT_PKG, info, ents)


# --- the scripts AMIX runs ----------------------------------------------------

ADMIN = """mail=
instance=overwrite
partial=nocheck
runlevel=nocheck
idepend=nocheck
rdepend=nocheck
space=nocheck
setuid=nocheck
conflict=nocheck
action=nocheck
basedir=default
"""


def go_script(host, tftp_port=TFTP_PORT, http_port=HTTP_PORT):
    """What the user runs: fetch and install the bootstrap, then the steps."""
    return """#!/bin/sh
# AMIX Installer: the bootstrap. Fetched with tftp, run as root: sh go
H=%(host)s
W=http://%(host)s:%(http)d
B=%(boot)s
[ "`/usr/bin/id | /usr/bin/sed 's/(.*//'`" = "uid=0" ] || { echo "go: run it as root"; exit 1; }
cd /tmp || exit 1
echo "go: fetching the bootstrap from $H"
/usr/bin/tftp $H %(tftp)d <<EOF
binary
get amixboot.pkg /tmp/amixboot.pkg
quit
EOF
[ -s /tmp/amixboot.pkg ] || { echo "go: no bootstrap: is the network up (ping $H)?"; exit 1; }
cat > /tmp/amixboot.admin <<EOF
%(admin)sEOF
echo "go: installing it with pkgadd into $B"
/usr/sbin/pkgadd -n -a /tmp/amixboot.admin -d /tmp/amixboot.pkg %(pkg)s || { echo "go: pkgadd failed"; exit 1; }
rm -f /tmp/amixboot.pkg
LD_LIBRARY_PATH=$B/lib; export LD_LIBRARY_PATH
$B/bin/curl -s -f -o /tmp/amix-steps $W/steps || { echo "go: cannot reach $W"; exit 1; }
exec /bin/sh /tmp/amix-steps
""" % {"host": host, "http": http_port, "tftp": tftp_port, "boot": BOOT_DIR,
       "admin": ADMIN, "pkg": BOOT_PKG}


def packages_for(cfg):
    """IPS packages for the chosen options, and the options not packaged yet."""
    pkgs, missing = [], []
    for s in model.STEPS:
        for g in s.groups:
            if g.id in ("pkgclient", "kern", "boot", "src", "target", "mach", "accel", "cpu",
                        "layout", "dsize", "net", "keymap", "desk"):
                continue
            for o in g.options:
                if not cfg.chosen(g, o) or cfg.installed(o) or o.id == "headers":
                    continue
                if o.id in PACKAGES:
                    pkgs += [p for p in PACKAGES[o.id] if p not in pkgs]
                elif o.mb:
                    missing.append(o.title)
    desk = cfg.sel.get("desk")
    if desk in PACKAGES and not cfg.installed(model.find_group("desk").option(desk)):
        pkgs = [p for p in PACKAGES[desk] if p not in pkgs] + pkgs
    return pkgs, missing


def steps_script(cfg, host, http_port=HTTP_PORT, data_disk=None):
    """The steps AMIX runs after the bootstrap. data_disk: (raw slice, sectors)
    of the empty data disk partition to make a file system on, or None."""
    pkgs, missing = packages_for(cfg)
    W = "http://%s:%d" % (host, http_port)
    lines = ["#!/bin/sh",
             "# AMIX Installer: the install steps, fetched from %s/steps" % W,
             "W=%s" % W, "B=%s" % BOOT_DIR,
             "LD_LIBRARY_PATH=$B/lib; export LD_LIBRARY_PATH",
             "LOG=/tmp/amix-install.log; : > $LOG",
             "report() { echo \"$*\"; echo \"$*\" | $B/bin/curl -s -T - \"$W/report/msg\" >/dev/null; }",
             "fail() { report \"FAILED: $*\"; tail -20 $LOG | $B/bin/curl -s -T - \"$W/report/log\" >/dev/null; exit 1; }",
             "step() { n=$1; shift; report \"step $n: $*\"; }",
             "",
             "step 1 \"the system: `uname -a`\"",
             "report \"root: `/usr/bin/df -k / | /usr/bin/tail -1`\"",
             "",
             "step 2 \"the package client\"",
             "$B/bin/pkg set-publisher -O $W/repo amix >>$LOG 2>&1 || fail set-publisher",
             "$B/bin/pkg refresh >>$LOG 2>&1 || fail refresh",
             "$B/bin/pkg install sysutils/amix-pkg >>$LOG 2>&1 || fail 'installing the package client'",
             "PKG=/usr/bin/pkg",
             "LD_LIBRARY_PATH=; export LD_LIBRARY_PATH",
             "$PKG refresh >>$LOG 2>&1 || fail 'the installed client'"]
    if data_disk:
        slice_, sectors = data_disk
        lines += ["",
                  "step 3 \"the data disk on %s (%d sectors)\"" % (slice_, sectors),
                  "D=%s" % slice_,
                  "if /usr/sbin/mount | /usr/bin/grep /amixdata >/dev/null; then report 'data disk mounted already'; else",
                  "  /usr/sbin/mkfs -F ufs $D %d >>$LOG 2>&1 || fail mkfs" % sectors,
                  "  /usr/bin/mkdir -p /amixdata && /usr/sbin/mount -F ufs `echo $D | /usr/bin/sed s/rdsk/dsk/` /amixdata >>$LOG 2>&1 || fail mount",
                  "  echo \"`echo $D | /usr/bin/sed s/rdsk/dsk/` $D /amixdata ufs 2 yes -\" >> /etc/vfstab",
                  "fi",
                  "for d in usr/local usr/gnu opt; do",
                  "  [ -d /$d ] && [ ! -h /$d ] || continue",
                  "  n=`echo $d | /usr/bin/sed 's|/|-|g'`",
                  "  report \"moving /$d to /amixdata/$n\"",
                  "  (cd /$d && /usr/bin/find . -print | /usr/bin/cpio -pdm /amixdata/$n) >>$LOG 2>&1 || fail \"copy /$d\"",
                  "  a=`cd /$d && /usr/bin/find . -print | /usr/bin/wc -l`; b=`cd /amixdata/$n && /usr/bin/find . -print | /usr/bin/wc -l`",
                  "  [ $a -eq $b ] || fail \"/$d: $a entries, copy has $b\"",
                  "  /usr/bin/mv /$d /$d.moved && /usr/bin/ln -s /amixdata/$n /$d && /usr/bin/rm -rf /$d.moved || fail \"link /$d\"",
                  "done"]
    lines += ["", "step 4 \"packages: %s\"" % (" ".join(pkgs) or "none")]
    if pkgs:
        lines += ["$PKG install %s >>$LOG 2>&1 || fail 'installing packages'" % " ".join(pkgs)]
    if missing:
        lines += ["report 'not packaged yet, left out: %s'" % ", ".join(missing).replace("'", "")]
    if cfg.sel.get("kern") not in (None, "stock"):
        lines += ["", "report 'the %s kernel is not packaged yet: the stock kernel stays'"
                  % model.find_group("kern").option(cfg.sel["kern"]).title]
    lines += ["",
              "step 5 \"removing the bootstrap\"",
              "cd / && /usr/sbin/pkgrm -n -a /tmp/amixboot.admin %s >>$LOG 2>&1 || report 'pkgrm %s failed: remove it by hand'"
              % (BOOT_PKG, BOOT_PKG),
              "rm -f /tmp/amixboot.admin /tmp/go /tmp/amix-steps",
              "report DONE",
              ""]
    return "\n".join(lines)


# --- the servers --------------------------------------------------------------

class TftpServer(threading.Thread):
    """Read-only TFTP (RFC 1350, octet) from one socket, files from a dict."""

    def __init__(self, files, port=TFTP_PORT, bind="0.0.0.0", log=None):
        super(TftpServer, self).__init__(daemon=True)
        self.files = files
        self.log = log or (lambda t: None)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((bind, port))
        self.port = self.sock.getsockname()[1]
        self.sock.settimeout(0.5)
        self.sessions = {}
        self.stopped = False

    def stop(self):
        self.stopped = True
        self.sock.close()

    def _send(self, addr, block):
        data = self.sessions[addr]
        chunk = data[(block - 1) * 512: block * 512]
        self.sock.sendto(struct.pack("!HH", 3, block & 0xffff) + chunk, addr)

    def run(self):
        while not self.stopped:
            try:
                pkt, addr = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            if len(pkt) < 4:
                continue
            op = struct.unpack("!H", pkt[:2])[0]
            if op == 1:                                   # RRQ
                name = os.path.basename(pkt[2:].split(b"\0")[0].decode("latin-1"))
                data = self.files.get(name)
                if data is None:
                    self.sock.sendto(struct.pack("!HH", 5, 1) + b"file not found\0", addr)
                    self.log("tftp: %s asked for %s: not here" % (addr[0], name))
                    continue
                self.sessions[addr] = data() if callable(data) else data
                self.log("tftp: sending %s to %s (%d bytes)" % (name, addr[0],
                                                               len(self.sessions[addr])))
                self._send(addr, 1)
            elif op == 4 and addr in self.sessions:       # ACK
                blk = struct.unpack("!H", pkt[2:4])[0]
                data = self.sessions[addr]
                total = len(data) // 512 + 1
                if blk >= total:
                    del self.sessions[addr]
                    continue
                self._send(addr, blk + 1)
            else:
                self.sock.sendto(struct.pack("!HH", 5, 4) + b"illegal operation\0", addr)


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
        st = self.server.state
        path = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path)
        if path == "/steps":
            return self._reply(200, st.steps().encode())
        if path.startswith("/repo/"):
            rel = os.path.normpath(path[len("/repo/"):])
            if rel.startswith("..") or os.path.isabs(rel):
                return self._reply(403)
            f = os.path.join(st.repo, rel)
            if os.path.isfile(f):
                with open(f, "rb") as fh:
                    return self._reply(200, fh.read(), "application/octet-stream")
        return self._reply(404, b"not found\n")

    do_HEAD = do_GET

    def _report(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if not n and self.headers.get("Transfer-Encoding", "").lower() == "chunked":
            parts = []
            while True:
                size = int(self.rfile.readline().strip() or b"0", 16)
                if not size:
                    self.rfile.readline()
                    break
                parts.append(self.rfile.read(size))
                self.rfile.readline()
            body = b"".join(parts)
        kind = os.path.basename(urllib.parse.urlsplit(self.path).path)
        self.server.state.reported(kind, body.decode("latin-1", "replace"))
        self._reply(200, b"ok\n")

    def do_PUT(self):
        if self.path.startswith("/report/"):
            return self._report()
        self._reply(403)

    do_POST = do_PUT


class _HttpServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class Session(object):
    """One network install: both servers, the scripts, the log."""

    def __init__(self, cfg, repo, host=None, data_disk=None, log=None,
                 tftp_port=TFTP_PORT, http_port=HTTP_PORT, bind="0.0.0.0"):
        self.cfg = cfg
        self.repo = repo
        self.host = host or FS_UAE_HOST
        self.data_disk = data_disk
        self.log = log or (lambda t: None)
        self.messages = []
        self.done = False
        self.failed = False
        self._boot = None
        self.http = _HttpServer((bind, http_port), _Handler)
        self.http.state = self
        self.http_port = self.http.server_address[1]
        self.tftp = TftpServer({"go": lambda: go_script(self.host, self.tftp.port,
                                                         self.http_port).encode(),
                                "amixboot.pkg": self.boot_pkg},
                               tftp_port, bind, self.log)

    def boot_pkg(self):
        if self._boot is None:
            self._boot = bootstrap(self.repo)
        return self._boot

    def steps(self):
        return steps_script(self.cfg, self.host, self.http_port, self.data_disk)

    def commands(self):
        """What to type at the AMIX console, as root."""
        return ["tftp %s %d" % (self.host, self.tftp.port), "binary", "get go", "quit", "sh go"]

    def reported(self, kind, text):
        for line in text.splitlines() or [""]:
            t = time.strftime("%H:%M:%S")
            self.messages.append((t, kind, line))
            if line == "DONE":
                self.done = True
            if line.startswith("FAILED"):
                self.failed = True
            self.log("%s amix: %s" % (t, line) if kind == "msg" else "%s amix log: %s" % (t, line))

    def start(self):
        self.tftp.start()
        threading.Thread(target=self.http.serve_forever, daemon=True).start()
        self.log("serving TFTP on UDP %d and HTTP on TCP %d" % (self.tftp.port, self.http_port))

    def stop(self):
        self.tftp.stop()
        self.http.shutdown()
        self.http.server_close()


def state_json(session):
    return json.dumps({"done": session.done, "failed": session.failed,
                       "messages": session.messages})
