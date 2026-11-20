"""The network install (0.4): AMIX installs itself over its A2065.

The installer serves, while the install runs:
  TFTP  UDP 1069  "go" and "amixboot.pkg" (read-only; replies from the
                  listening port, as FS-UAE's slirp NAT needs)
  HTTP  TCP 8069  /repo/...    the IPS repository (read-only)
                  /steps       the step script for this install
                  /report/...  AMIX posts its progress here (PUT/POST)
At the AMIX console the user types (as root):
  tftp HOST 1069     binary     get go     quit     sh go
"go" installs the bootstrap (pkg with its own plain HTTP, and libz: an SVR4
datastream; no curl, no TLS: stock AMIX has no /dev/urandom for OpenSSL) with stock pkgadd into /opt/amixboot, fetches /steps and runs
it: the real package client through IPS, the data disk (AMIX's mkfs),
the packages, the kernel last; at the end the bootstrap is removed (pkgrm).
Only AMIX's own tools touch AMIX's file system.
"""
import gzip
import http.server
import json
import os
import re
import secrets
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
AGENT_PORT = 7777                   # where VS Code's agent listener waits (amix.agent.port)
BOOT_PKG = "AMIXboot"
BOOT_DIR = "/opt/amixboot"

# option id -> IPS packages (components); options without packages yet are
# reported, not installed
PACKAGES = {
    "cde": ["desktop/cde", "desktop/cde-dtlogin"], "mwm": ["desktop/motif"], "netsurf": ["web/netsurf"],
    "media": ["multimedia/ffmpeg", "desktop/cde-apps"], "grafx2": ["graphics/grafx2"],
    "sysmon": ["desktop/cde-apps"], "games": ["games/quake", "games/ioquake3"],
    # gcc14/gcc27: developer/gcc-14 and gcc-27 are cross compilers on the PC; a
    # compiler that runs on AMIX is not packaged yet (reported as such)
    "pkgconf": ["developer/pkgconf"], "gnu": ["gnu/gnu-tools"], "python": ["runtime/python3"],
    "vim": ["editor/vim"], "vscode": ["developer/vscode-agent"],
}
# what the bootstrap carries, from these packages of the repository
BOOT_FILES = {
    "sysutils/amix-pkg": {"usr/bin/pkg": "bin/pkg"},
    "library/zlib": {"usr/local/lib/libz.so.1": "lib/libz.so.1"},
}
# carried too when the repository has them: libamixc99 is a shared library
# from library/amixc99 version 2 on, and gcc 14 programs may need it
BOOT_OPTIONAL = {
    "library/amixc99": {"usr/lib/libamixc99.so.1": "lib/libamixc99.so.1"},
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


def _ready(repo):
    """(why AMIX could not install from repo or None, the bootstrap made from it)."""
    from . import plan
    if not repo:
        return plan.NO_REPO, None
    why = plan.repo_problem(repo)
    if why:
        return why, None
    try:
        return None, bootstrap(repo)
    except (KeyError, OSError, ValueError) as e:
        return ("the bootstrap (%s) cannot be made from %s: %s is missing"
                % (BOOT_PKG, repo, e), None)


def not_ready(repo):
    """Why a network install from repo cannot start, or None: a usable
    repository, and the bootstrap made from it before any server starts, so
    "go" never waits on a file that cannot be made."""
    return _ready(repo)[0]


def bootstrap(repo):
    """The SVR4 datastream AMIXboot, made of files from the repository."""
    E = svr4.Entry
    ents = [E("bin", "d"), E("lib", "d")]
    versions = []
    for stem, files in list(BOOT_FILES.items()) + list(BOOT_OPTIONAL.items()):
        try:
            v, man = _newest(repo, stem)
        except KeyError:
            if stem in BOOT_OPTIONAL:
                continue
            raise
        have = [src for src in files if " path=%s " % src in man.replace("\n", " \n ")]
        if stem in BOOT_OPTIONAL and not have:
            continue
        versions.append("%s@%s" % (stem, v.split(":")[0]))
        for src, dst in files.items():
            if stem in BOOT_OPTIONAL and src not in have:
                continue
            ents.append(E(dst, "f", 0o555, data=payload(repo, man, src)))
    ents = ents[:2] + sorted(ents[2:], key=lambda e: e.path)
    info = {"NAME": "AMIX Installer bootstrap (removed again at the end)",
            "VERSION": "1", "BASEDIR": BOOT_DIR, "DESC": ", ".join(versions),
            "VENDOR": "amix-userland"}
    return svr4.datastream(BOOT_PKG, info, ents)


# --- the scripts AMIX runs ----------------------------------------------------

# mail= (no mail) must be the LAST line: AMIX 2.1's pkginstall stops reading
# the admin file at the first empty value (it skips the line without clearing
# the parameter name it looks for, so nothing after it is found), and then
# asks for the base directory, which pkgadd -n cannot answer.
ADMIN = """instance=overwrite
partial=nocheck
runlevel=nocheck
idepend=nocheck
rdepend=nocheck
space=nocheck
setuid=nocheck
conflict=nocheck
action=nocheck
basedir=%s
mail=
""" % BOOT_DIR


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
# pkgadd -n does not create a missing base directory: it stops with
# "Installation of this package requires a base directory"
# (AMIX's mkdir -p fails with "File exists" on a directory that is there)
[ -d $B ] || /usr/bin/mkdir -p $B || { echo "go: cannot make $B"; exit 1; }
/usr/sbin/pkgadd -n -a /tmp/amixboot.admin -d /tmp/amixboot.pkg %(pkg)s || { echo "go: pkgadd failed"; exit 1; }
rm -f /tmp/amixboot.pkg
LD_LIBRARY_PATH=$B/lib; export LD_LIBRARY_PATH
$B/bin/pkg http get $W/steps /tmp/amix-steps || { echo "go: cannot reach $W"; exit 1; }
exec /bin/sh /tmp/amix-steps
""" % {"host": host, "http": http_port, "tftp": tftp_port, "boot": BOOT_DIR,
       "admin": ADMIN, "pkg": BOOT_PKG}


def packages_for(cfg):
    """IPS packages for the chosen options, and the options not packaged yet.
    A packaged option is installed even when the source disk has it already:
    pkg leaves an installed package as it is, updates an older one and puts
    its own over a copy made by hand.  Skipping it (0.6.8) left out the VS
    Code agent of a disk that had one, and with it the agent's new key."""
    pkgs, missing = [], []
    for s in model.STEPS:
        for g in s.groups:
            if g.id in ("pkgclient", "kern", "boot", "src", "target", "mach", "accel", "cpu",
                        "layout", "dsize", "net", "keymap", "desk", "gfx", "diskon", "dctl",
                        "netif", "gfxhw", "memory"):
                continue
            for o in g.options:
                if not cfg.chosen(g, o) or o.id == "headers":
                    continue
                if o.id in PACKAGES:
                    pkgs += [p for p in PACKAGES[o.id] if p not in pkgs]
                elif o.mb and not cfg.installed(o):
                    missing.append(o.title)
    desk = cfg.sel.get("desk")
    if desk in PACKAGES:
        pkgs = [p for p in PACKAGES[desk] if p not in pkgs] + pkgs
    if "x11" in cfg.tags() and X_SERVER not in pkgs:
        pkgs = [X_SERVER] + pkgs
    return pkgs, missing


def fs_uae_data_disk(pl):
    """(raw slice, sectors, raw slice on the machine) of the data disk a plan
    wrote: AMIX makes its file system in FS-UAE (SCSI ID 1, first partition =
    slice 1, on the emulated A3000 SCSI), and the machine's /etc/vfstab names
    it where the machine has it (c9d0s1 on the Z3660's SD card), or None.
    Raises OSError or hdf.NotAnImage when the disk cannot be read."""
    from . import hdf, plan
    dd = [a for a in pl.actions if a.id == "data-disk"]
    if not dd:
        return None
    with hdf.Image(os.path.join(pl.paths["out_dir"], dd[0].name)) as im:
        blocks = im.read_rdb()[0].blocks
    return (plan.disk_device(pl.cfg, "data", 1, fs_uae=True), blocks,
            plan.disk_device(pl.cfg, "data", 1))


def data_disk_text(dd):
    """The data disk from fs_uae_data_disk, for the user: where AMIX in FS-UAE
    makes the file system, its size, and where the machine has it."""
    raw, sectors, machine = dd
    t = "%s, %d sectors" % (raw, sectors)
    return t if machine == raw else "%s (on the machine: %s)" % (t, machine)


def running_data_disk(cfg, fs_uae_config):
    """The data disk for an AMIX that runs in FS-UAE with fs_uae_config: its
    empty AMIX disk at SCSI ID 1 as step 4 takes it (raw slice, sectors, the
    same slice), or None (none there, or it has its file system already:
    AMIX mounts that one itself)."""
    from . import fsuae, plan
    _, data, _ = fsuae.amix_disks(fs_uae_config)
    if not data or not data[2]:
        return None
    dev = plan.disk_device(cfg, "data", 1, fs_uae=True)
    return (dev, data[1], dev)


def hdf_movable():
    from . import hdf
    return hdf.MOVABLE


def plan_kernel(pl):
    """The kernel a plan linked for AMIX to install (the cards' drivers), or None."""
    a = next((a for a in pl.actions if a.id == "kernel" and a.available), None)
    if a is None:
        return None
    p = os.path.join(pl.paths["out_dir"], a.name)
    return p if os.path.isfile(p) else None


STATE_DIR = os.path.join("~", ".local", "share", "amix-installer")


def report_path(pl=None):
    """foundation-report.txt in the plan's out folder; for a running system
    (no out folder) in ~/.local/share/amix-installer, made if need be, where
    vscode-agent.key goes too."""
    out = pl.paths.get("out_dir") if pl is not None else None
    if not out:
        out = os.path.expanduser(STATE_DIR)
        os.makedirs(out, mode=0o700, exist_ok=True)
    return os.path.join(out, "foundation-report.txt")


def write_agent_key(key, out_dir):
    """The agent key, mode 600, in out_dir/vscode-agent.key and in
    STATE_DIR/vscode-agent.key (vscode-amix reads that one); returns the
    STATE_DIR file, the one to name."""
    state = os.path.expanduser(STATE_DIR)
    os.makedirs(state, mode=0o700, exist_ok=True)
    files = [os.path.join(state, "vscode-agent.key")]
    if os.path.realpath(out_dir) != os.path.realpath(state):
        files.append(os.path.join(out_dir, "vscode-agent.key"))
    for f in files:
        fd = os.open(f, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w") as fh:
            fh.write(key + "\n")
    return files[0]


def sysv_sum(data):
    """What AMIX's sum(1) prints first: the System V checksum."""
    s = sum(bytearray(data)) & 0xffffffff
    r = (s & 0xffff) + (s >> 16)
    return (r & 0xffff) + (r >> 16)


# the X card driver for /etc/X11/amix-card, by the installer's graphics card
# (the Z3660's becomes "z3660" when its card driver is split from zz9000)
# the card /etc/X11/amix-card names: the Z3660 apart from the ZZ9000 (xserver-amix opens
# /dev/z3660rtg, else the Z3660 kernel's built-in /dev/zz9000 when the board is a Z3660)
X_CARD = {"picasso2": "cirrus", "rtg": "z3660", "zz9000": "zz9000"}
# a network card whose driver is linked into the kernel (HydraNet, X-Surf
# 100) comes up as AMIX's aen0 does in /etc/inet/network-config: plumbed when
# the board answers, with the host name's address
NETCONFIG = """
# %(card)s (written by the AMIX Installer)
/usr/sbin/slink addaen /dev/%(ifc)s %(ifc)s 2>/dev/null &&
	/usr/sbin/ifconfig %(ifc)s `uname -n` up -trailers
"""


def net_config(drv):
    """The network-config lines for a linked-in network driver."""
    return NETCONFIG % {"card": drv.card, "ifc": drv.node}
# the X server, under every X desktop
X_SERVER = "x11/xserver-amix"


def missing_in_repo(repo, stem, _seen=None):
    """The first package down stem's requirements the repository lacks, or None."""
    seen = _seen if _seen is not None else set()
    if stem in seen:
        return None
    seen.add(stem)
    try:
        _, man = _newest(repo, stem)
    except KeyError:
        return stem
    for line in man.splitlines():
        w = line.split()
        if w and w[0] == "depend" and "type=require" in w:
            fmri = [x[5:] for x in w if x.startswith("fmri=")]
            if fmri:
                dep = fmri[0].replace("pkg://amix/", "").replace("pkg:/", "").split("@")[0]
                m = missing_in_repo(repo, dep, seen)
                if m:
                    return m
    return None


def _cpu(manifest):
    """The CPU a published package's code needs (org.amix.cpu: 68020 is the
    baseline, 68060 a 68060 only), or None: built before the tag."""
    for line in manifest.splitlines():
        w = line.split()
        if w[:2] == ["set", "name=org.amix.cpu"] and len(w) > 2 and w[2].startswith("value="):
            return w[2][6:]
    return None


def needs_060(repo, stem, _seen=None):
    """The first package down stem's requirements that holds 68060-only
    code (org.amix.cpu 68060), or None.  A 68020/030 stops on its first
    68040/060 instruction with "Bad System Call" (fdmul in libXm killed
    dtsession on the FS-UAE A3000 in 0.6.9), so such a package must not go
    onto a machine without a 68060."""
    seen = _seen if _seen is not None else set()
    if stem in seen:
        return None
    seen.add(stem)
    try:
        _, man = _newest(repo, stem)
    except KeyError:
        return None
    if _cpu(man) == "68060":
        return stem
    for line in man.splitlines():
        w = line.split()
        if w and w[0] == "depend" and "type=require" in w:
            fmri = [x[5:] for x in w if x.startswith("fmri=")]
            if fmri:
                dep = fmri[0].replace("pkg://amix/", "").replace("pkg:/", "").split("@")[0]
                m = needs_060(repo, dep, seen)
                if m:
                    return m
    return None


def fetch_size(repo, stems, skip=None):
    """(files, bytes) pkg fetches from repo to install stems: their files and
    their requirements', without the packages in skip (installed before; the
    set grows by what stems bring in).  Bytes: the files as the repository
    serves them (pkg.csize)."""
    skip = set() if skip is None else skip
    files = size = 0
    todo = list(stems)
    while todo:
        stem = todo.pop()
        if stem in skip:
            continue
        skip.add(stem)
        try:
            _, man = _newest(repo, stem)
        except KeyError:
            continue
        for line in man.splitlines():
            w = line.split()
            if not w:
                continue
            if w[0] == "file":
                files += 1
                cs = [x for x in w if x.startswith("pkg.csize=")]
                sz = [x for x in w if x.startswith("pkg.size=")]
                size += int((cs or sz or ["=0"])[0].split("=", 1)[1])
            elif w[0] == "depend" and "type=require" in w:
                fmri = [x[5:] for x in w if x.startswith("fmri=")]
                if fmri:
                    todo.append(fmri[0].replace("pkg://amix/", "").replace("pkg:/", "")
                                .split("@")[0])
    return files, size


def unpacked_kb(repo, stems):
    """{stem: (KB on the root, KB under hdf.MOVABLE)} for stems and every
    package they require, each once: what installing them takes on AMIX
    (pkg.size, plus 1 KB a file for its last fragment and inode)."""
    from . import hdf
    mov = tuple(d.lstrip("/") + "/" for d in hdf.MOVABLE)
    out = {}
    todo = list(stems)
    while todo:
        stem = todo.pop()
        if stem in out:
            continue
        try:
            _, man = _newest(repo, stem)
        except KeyError:
            out[stem] = (0, 0)
            continue
        root = movable = 0
        for line in man.splitlines():
            w = line.split()
            if not w:
                continue
            if w[0] == "file":
                path = [x[5:] for x in w if x.startswith("path=")]
                sz = [x for x in w if x.startswith("pkg.size=")]
                n = int(sz[0].split("=", 1)[1]) if sz else 0
                if path and path[0].startswith(mov):
                    movable += n + 1024
                else:
                    root += n + 1024
            elif w[0] == "depend" and "type=require" in w:
                fmri = [x[5:] for x in w if x.startswith("fmri=")]
                if fmri:
                    todo.append(fmri[0].replace("pkg://amix/", "").replace("pkg:/", "")
                                .split("@")[0])
        out[stem] = ((root + 1023) // 1024, (movable + 1023) // 1024)
    return out


ROOM_SHORT = "not enough room"


def room_lines(table, data_kb=None, extra_root_kb=0):
    """Shell lines (after step 1, before anything changes): the KB the
    packages of table (unpacked_kb) that are not installed yet take, against
    the free space, keeping $M MB on each file system.  With a data disk
    (data_kb: the one step 4 makes; else /amixdata when it is mounted) the
    MOVABLE trees count there, else on the root.  extra_root_kb: files the
    steps write besides packages (a kernel and the stock one kept).  Stops
    with FAILED: "not enough room: ..." (ROOM_SHORT) when it does not fit."""
    lines = ["# room: what the packages not installed yet take, against the free space;",
             "# too little: the bootstrap goes again (nothing else has changed), then FAILED",
             "room_fail() {",
             "  R=/tmp/amixboot-report; /usr/bin/rm -rf $R",
             "  /usr/bin/mkdir $R && /usr/bin/cp $B/bin/pkg $B/lib/* $R/ && RPKG=$R/pkg RLD=$R",
             "  (cd / && /usr/sbin/pkgrm -n -a /tmp/amixboot.admin %s >>$LOG 2>&1) || unboot" % BOOT_PKG,
             "  /usr/bin/rm -f /tmp/amixboot.admin /tmp/go /tmp/amix-steps",
             "  report \"FAILED: $*\"",
             "  /usr/bin/rm -rf $R",
             "  exit 1",
             "}",
             "NR=%d; NM=0" % extra_root_kb]
    for stem in sorted(table):
        rk, mk = table[stem]
        if rk or mk:
            lines += ["$B/bin/pkg list %s >/dev/null 2>&1 || { NR=`/usr/bin/expr $NR + %d`; "
                      "NM=`/usr/bin/expr $NM + %d`; }" % (stem, rk, mk)]
    lines += ["FR=`/usr/bin/df -k / | /usr/bin/tail -1 | /usr/bin/awk '{print $4}'`",
              "K=`/usr/bin/expr $M \\* 1024`"]
    if data_kb is not None:
        lines += ["FL=%d; WHERE='the new data disk'" % data_kb]
    else:
        lines += ["FL=; WHERE='the data disk'",
                  "if /usr/sbin/mount | /usr/bin/grep '^/amixdata ' >/dev/null; then",
                  "  FL=`/usr/bin/df -k /amixdata | /usr/bin/tail -1 | /usr/bin/awk '{print $4}'`",
                  "fi"]
    lines += [
        "if [ -n \"$FL\" ]; then",
        "  report \"room: the root needs $NR KB of $FR KB free, $WHERE $NM KB of $FL KB (each keeps $M MB)\"",
        "  [ `/usr/bin/expr $NR + $K` -le $FR ] || room_fail \"%s: the root needs $NR KB and $M MB to spare, it has $FR KB free\"" % ROOM_SHORT,
        "  [ `/usr/bin/expr $NM + $K` -le $FL ] || room_fail \"%s: $WHERE needs $NM KB and $M MB to spare, it has $FL KB free\"" % ROOM_SHORT,
        "else",
        "  N=`/usr/bin/expr $NR + $NM`",
        "  report \"room: no data disk: the root needs $N KB of $FR KB free (keeps $M MB)\"",
        "  [ `/usr/bin/expr $N + $K` -le $FR ] || room_fail \"%s: no data disk, the root needs $N KB and $M MB to spare, it has $FR KB free\"" % ROOM_SHORT,
        "fi"]
    return lines


def room_advice(failure, emu, fs_uae_config=None):
    """What to do when the room check stopped the install (failure starts
    with ROOM_SHORT), or None.  Nothing has changed on AMIX by then."""
    if not failure or not failure.startswith(ROOM_SHORT):
        return None
    if emu:
        how = ("amix-installer --fs-uae-config %s --add-data-disk 1g" % fs_uae_config
               if fs_uae_config else "Add a Data Disk on this page")
        return ("Nothing was changed on AMIX. Give it a data disk (%s), quit FS-UAE and start "
                "it again with that configuration, then start the install again; or choose "
                "fewer packages." % how)
    return ("Nothing was changed on AMIX. Choose fewer packages, or give the Amiga a second "
            "disk (or a free partition) with an AMIX file system mounted on /amixdata "
            "(an /etc/vfstab line), then start the install again.")


def fetching(what, size):
    """The report line before pkg fetches: what, its files and kilobytes;
    Session.reported parses it for the progress bar."""
    files, nbytes = size
    return "report 'installing %s: %d files, %d KB from this computer'" % (
        what, files, (nbytes + 1023) // 1024)


FETCH_RE = re.compile(r"^installing (.+): (\d+) files, (\d+) KB from this computer$")
# the steps of the go script (step 4, the data disk, and 7, the kernel, may be left out)
NSTEPS = 8


# --- the foundation -------------------------------------------------------------
# What every AMIX gets, on FS-UAE and on a real Amiga alike, before any optional
# set; each item is reported as "foundation <item> present|installed|missing|
# failed <detail>", so two installs compare line by line.
FOUNDATION_PKGS = ["library/zlib", "library/amixc99", "library/libm99", "sysutils/setclk",
                   "gnu/gnu-tools"]
# the patch disk's answers: no change list; "danger" (it runs outside single
# user, as the installer does); the kernel when Cdev and conf are there; no
# boot floppy; go
PATCH_ANSWERS_KERNEL = ["n", "danger", "y", "n", "go"]
PATCH_ANSWERS = ["n", "danger", "n", "go"]
# foundation packages that replace a file of the SVR4 core package
HANDOVER = {"sysutils/setclk": "/usr/amiga/bin/setclk"}
# foundation packages brought up to the repository's version when present
UPDATE_PKGS = {"sysutils/setclk"}


def patch_check_lines(patch):
    """Shell lines: the AMIX patch level (Patch Disk 1-2a markers); with patch
    (the user's patch disk served at /patch) apply it when it is missing."""
    lines = ["P=`/usr/bin/sed -n 's/^#define PATCHLEVEL *//p' /usr/include/patchlevel.h 2>/dev/null`",
             "[ -n \"$P\" ] || P=`/usr/bin/sed -n 's/.*Amiga Version 2\\.1p\\([0-9a-z]*\\).*/\\1/p' "
             "/etc/motd 2>/dev/null | /usr/bin/head -1`",
             "if [ -n \"$P\" ] && [ -f /var/patch/README ]; then",
             "  report \"foundation patch present 2.1p$P (kernel `/usr/bin/uname -v`)\""]
    if patch:
        lines += [
            "else",
            "  report 'foundation patch missing: applying your patch disk (AmigaUNIX Patch Disk 1-2a)'",
            "  $B/bin/pkg http get $W/patch /tmp/amixpatch >>$LOG 2>&1 || fail 'fetching the patch disk'",
            "  if /usr/bin/pkginfo -q Cdev && /usr/bin/pkginfo -q conf; then",
            "    A='%s'; K='with the 2.1c kernel'" % " ".join(PATCH_ANSWERS_KERNEL),
            "  else",
            "    A='%s'; K='without the kernel update (Cdev or conf missing)'" % " ".join(PATCH_ANSWERS),
            "  fi",
            "  (cd /tmp && for a in $A; do echo $a; done | /sbin/sh /tmp/amixpatch) >>$LOG 2>&1",
            "  /usr/bin/rm -f /tmp/amixpatch",
            "  P=`/usr/bin/sed -n 's/^#define PATCHLEVEL *//p' /usr/include/patchlevel.h 2>/dev/null`",
            "  [ -n \"$P\" ] || fail 'the patch disk did not apply: see /var/patch'",
            "  report \"foundation patch installed 2.1p$P $K; AMIX asks its setup questions at the next boot\"",
            "  REBOOT=yes"]
    lines += ["else",
              "  report 'foundation patch missing: AmigaUNIX Patch Disk 1-2a is not applied'",
              "  fail 'apply Patch Disk 1-2a from your own disk first, or give the installer its image'",
              "fi"] if not patch else ["fi"]
    return lines


def foundation_pkg_lines(p, sizes=None):
    """Shell lines: foundation package p installed ($PKG, $M), Commodore's file
    handed over first (HANDOVER); a package of UPDATE_PKGS that is there
    already is updated to the repository's version."""
    sizes = sizes or {}
    lines = []
    if p in HANDOVER:
        # a file of Commodore's that the package replaces: core hands it
        # over (removef, as SVR4 does), so pkg may own it; the file stays
        # AMIX 2.1 ships installf but not removef: then its one change,
        # the path's line out of the contents file (kept as .amix-installer)
        path = HANDOVER[p]
        C = "/var/sadm/install/contents"
        lines += ["if /usr/bin/grep '^%s ' %s >/dev/null; then" % (path, C),
                  "  if [ -x /usr/sbin/removef ]; then",
                  "    /usr/sbin/removef core %s >>$LOG 2>&1 && /usr/sbin/removef -f core >>$LOG 2>&1 || "
                  "fail 'removef %s'" % (path, path),
                  "  else",
                  "    [ -f %s.amix-installer ] || /usr/bin/cp -p %s %s.amix-installer || "
                  "fail 'keeping %s'" % (C, C, C, C),
                  "    a=`/usr/bin/wc -l < %s`" % C,
                  "    # .new as a copy first: the original's owner, group and mode",
                  "    /usr/bin/cp -p %s %s.new || fail 'copying %s'" % (C, C, C),
                  "    /usr/bin/grep -v '^%s ' %s > %s.new || fail 'the contents without %s'"
                  % (path, C, C, path),
                  "    b=`/usr/bin/wc -l < %s.new`" % C,
                  "    [ `/usr/bin/expr $a - $b` -eq 1 ] || { /usr/bin/rm -f %s.new; "
                  "fail 'the contents would lose more than %s'; }" % (C, path),
                  "    /usr/bin/mv %s.new %s || fail 'writing %s'" % (C, C, C),
                  "  fi",
                  "  report 'foundation %s: %s handed from core'" % (p, path),
                  "fi",
                  "# pkg replaces no unowned file: Commodore's binary steps aside for the",
                  "# install only, back if it fails, gone when the package has its own",
                  "[ ! -f %s ] || $PKG list %s >/dev/null 2>&1 || /usr/bin/mv %s %s.core || "
                  "fail 'moving %s aside'" % (path, p, path, path, path)]
    if p in UPDATE_PKGS:
        lines += ["if $PKG list %s >/dev/null 2>&1; then" % p,
                  "  $PKG --min-free $M update %s >>$LOG 2>&1 || fail 'updating %s'" % (p, p),
                  "  report \"foundation %s present `$PKG list %s 2>/dev/null | /usr/bin/awk 'END {print $2}'`\"" % (p, p),
                  "else"]
    else:
        lines += ["if $PKG list %s >/dev/null 2>&1; then report 'foundation %s present'; else" % (p, p)]
    if p in sizes:
        lines += ["  " + fetching(p, sizes[p])]
    lines += ["  $PKG --min-free $M install %s >>$LOG 2>&1 || "
              "{ %sreport 'foundation %s failed'; fail 'installing %s'; }"
              % (p, ("[ ! -f %s.core ] || /usr/bin/mv %s.core %s; " % ((HANDOVER[p],) * 3))
                 if p in HANDOVER else "", p, p),
              "  report 'foundation %s installed'" % p,
              "fi"]
    if p in HANDOVER:
        lines += ["/usr/bin/rm -f %s.core" % HANDOVER[p]]
    return lines


def clock_lines(stamp):
    """Shell lines: /etc/clkzone, then the time from this computer to the
    second (GET /now, setclk -t), then into the battery clock; stamp
    (MMDDhhmmYYYY, UTC, when the steps were made) only when /now cannot be
    fetched or setclk has no -t."""
    return [
        "# the battery clock keeps local time (as AmigaDOS): setclk reads it in this zone",
        "if [ -s /etc/clkzone ]; then report \"foundation clkzone present `/usr/bin/cat /etc/clkzone`\"; else",
        "  Z=`. /etc/TIMEZONE; echo $TZ`",
        "  [ -n \"$Z\" ] || Z=:GMT",
        "  echo \"$Z\" > /etc/clkzone || fail 'writing /etc/clkzone'",
        "  report \"foundation clkzone installed $Z\"",
        "fi",
        "# the time from this computer now (UTC, to the second), then into the battery clock",
        "S=; $PKG http get $W/now /tmp/amix-now >>$LOG 2>&1 && S=`/usr/bin/cat /tmp/amix-now`",
        "/usr/bin/rm -f /tmp/amix-now",
        "if [ -n \"$S\" ] && /usr/amiga/bin/setclk -t $S -s >>$LOG 2>&1; then :; else",
        "  report 'foundation clock: to the minute only (no time from this computer, or an old setclk)'",
        "  TZ=GMT0 /usr/bin/date %s >>$LOG 2>&1 || fail 'setting the date'" % stamp,
        "  /usr/amiga/bin/setclk -s >>$LOG 2>&1 || report 'foundation clock: the battery clock was not written'",
        "fi",
        "report \"foundation clock installed `/usr/bin/date` (battery `/usr/amiga/bin/setclk -p`)\""]


def foundation_lines(have, now, sizes=None):
    """Shell lines: the clock, the base set, syslogd, the random driver.
    have: the foundation packages the repository holds (the others fail the
    install). now: the host's time (UTC, a time.struct_time).  sizes: package
    -> (files, bytes) to fetch, for a report line before each install."""
    sizes = sizes or {}
    stamp = "%02d%02d%02d%02d%04d" % (now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min,
                                      now.tm_year)
    gone = [p for p in FOUNDATION_PKGS if p not in have]
    lines = ["report 'foundation %s failed: not in the repository'" % p for p in gone]
    if gone:
        lines += ["fail 'the foundation needs %s: not in the repository'" % " ".join(gone)]
    for p in FOUNDATION_PKGS:
        if p not in gone:
            lines += foundation_pkg_lines(p, sizes)
    lines += clock_lines(stamp)
    lines += [
        "# syslogd: Patch Disk 1-2a ships it off (an exit at the top of its script)",
        "if /usr/bin/grep '^exit$' /etc/init.d/syslogd >/dev/null 2>&1; then",
        "  [ -f /etc/init.d/syslogd.stock ] || /usr/bin/cp /etc/init.d/syslogd /etc/init.d/syslogd.stock",
        "  /usr/bin/sed 's/^exit$/#exit  (syslogd on: AMIX Installer)/' /etc/init.d/syslogd.stock "
        "> /etc/init.d/syslogd || fail 'enabling syslogd'",
        "  /etc/init.d/syslogd start >>$LOG 2>&1",
        "  report 'foundation syslogd installed'",
        "elif [ -f /etc/init.d/syslogd ]; then report 'foundation syslogd present'; else",
        "  report 'foundation syslogd missing: no /etc/init.d/syslogd (Patch Disk 1-2a)'",
        "fi",
        "if [ -c /dev/urandom ]; then report 'foundation random present'; else",
        "  report 'foundation random: not in this release'",
        "fi"]
    return lines


def agent_lines(host, key, port=AGENT_PORT):
    """vscode-agent's configuration, once its package is installed: it dials
    the PC (host) and signs in with key, the one this install saved in
    vscode-agent.key; the programs run as the user an existing configuration
    names, else the first login user, else a new user developer."""
    C = "/etc/vscode-agent.conf"
    return ["",
            "if [ -x /usr/local/bin/vscode-agent ]; then",
            "  U=",
            "  [ -f %s ] && U=`/usr/bin/awk '$1 == \"user\" {print $2; exit}' %s`" % (C, C),
            "  [ -n \"$U\" ] || U=`/usr/bin/awk -F: '$3 >= 101 && $3 < 60000 && $1 != \"vmsys\" && $1 != \"oasys\" {print $1; exit}' /etc/passwd`",
            "  if [ -z \"$U\" ]; then",
            "    [ -d /home ] || /usr/bin/mkdir /home",
            "    /usr/sbin/useradd -d /home/developer -s /sbin/sh -c 'VS Code programs' developer >>$LOG 2>&1 || fail 'useradd developer'",
            "    [ -d /home/developer ] || /usr/bin/mkdir /home/developer",
            "    /usr/bin/chown developer /home/developer",
            "    U=developer",
            "    report 'VS Code agent: new user developer (no password yet: passwd developer)'",
            "  fi",
            "  # the key and the PC's address are this install's: VS Code has them in",
            "  # vscode-agent.key; the old file is kept as .old",
            "  [ ! -f %s ] || /usr/bin/cp %s %s.old" % (C, C, C),
            "  (umask 077; { echo 'connect %s:%d'; echo \"user $U\"; echo 'secret %s';" % (host, port, key),
            "    echo 'log /var/log/vscode-agent.log'; } > %s) || fail 'writing %s'" % (C, C),
            "  /usr/bin/chown root %s %s.old 2>/dev/null; /usr/bin/chmod 600 %s %s.old 2>/dev/null" % (C, C, C, C),
            "  report \"VS Code agent: runs programs as $U, dials %s:%d\"" % (host, port),
            "  /etc/init.d/vscode-agent stop >>$LOG 2>&1",
            "  /etc/init.d/vscode-agent start >>$LOG 2>&1",
            "fi"]


AGENT_PKG = "developer/vscode-agent"


CLOCK_PKG = "sysutils/setclk"


def agent_only_script(host, http_port, repo, agent_key, now=None, extra=()):
    """The steps for model.agent_only: the bootstrap's pkg installs (or
    updates) setclk and sets the clock from this computer, then installs (or
    updates) the VS Code agent and nothing else; its configuration is written
    (an existing one kept), the agent started, the bootstrap removed.
    now: the host's time (UTC), the clock's fallback to the minute.
    extra: further packages installed or updated with the agent (Agent and
    Motif: model.AGENT_MOTIF_PKGS)."""
    W = "http://%s:%d" % (host, http_port)
    now = now or time.gmtime()
    stamp = "%02d%02d%02d%02d%04d" % (now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min,
                                      now.tm_year)
    extra = [p for p in extra if p != AGENT_PKG]
    if repo:
        gone = [p for p in extra if missing_in_repo(repo, p)]
        extra = [p for p in extra if p not in gone]
    else:
        gone = []
    size = fetch_size(repo, [AGENT_PKG] + extra) if repo else None
    clock_sizes = {CLOCK_PKG: fetch_size(repo, [CLOCK_PKG])} if repo else {}
    lines = ["#!/bin/sh",
             "# AMIX Installer: the VS Code agent%s, fetched from %s/steps"
             % (" and " + " ".join(extra) if extra else " alone", W),
             "W=%s" % W, "B=%s" % BOOT_DIR,
             "LD_LIBRARY_PATH=$B/lib; export LD_LIBRARY_PATH",
             "LOG=/tmp/amix-install.log; : > $LOG",
             "RPKG=$B/bin/pkg; RLD=$B/lib",
             "report() { echo \"$*\"; echo \"$*\" | LD_LIBRARY_PATH=$RLD $RPKG http put \"$W/report/msg\" >/dev/null; }",
             "fail() { report \"FAILED: $*\"; tail -20 $LOG | LD_LIBRARY_PATH=$RLD $RPKG http put \"$W/report/log\" >/dev/null; exit 1; }",
             "step() { n=$1; shift; report \"step $n: $*\"; }",
             "",
             "step 1 \"the system: `uname -a`\"",
             "report \"root: `/usr/bin/df -k / | /usr/bin/tail -1`\""]
    lines += unboot_lines()
    lines += ["PKG=$B/bin/pkg",
             "# MB pkg keeps free on each file system (the installer's core reserve)",
             "M=%d" % model.RESERVE_MB,
             "$B/bin/pkg set-publisher -O $W/repo amix >>$LOG 2>&1 || fail set-publisher",
             "$B/bin/pkg refresh >>$LOG 2>&1 || fail refresh",
             "",
             "step 5 \"the clock: %s, the time from this computer\"" % CLOCK_PKG]
    if repo:
        lines += room_lines(unpacked_kb(repo, [CLOCK_PKG, AGENT_PKG] + extra))
    if repo and missing_in_repo(repo, CLOCK_PKG):
        lines += ["report 'the clock: %s is not in the repository, the clock stays as it is'"
                  % CLOCK_PKG]
    else:
        lines += foundation_pkg_lines(CLOCK_PKG, clock_sizes)
        lines += clock_lines(stamp)
    lines += ["",
              "step 6 \"packages: %s\"" % (" ".join([AGENT_PKG] + extra) if extra
                                            else AGENT_PKG + " (alone)")]
    lines += ["report 'not in the repository, left out: %s'" % " ".join(gone)] if gone else []
    if size:
        lines += [fetching(" ".join([AGENT_PKG] + extra), size)]
    for p in extra:
        lines += ["if $B/bin/pkg list %s >/dev/null 2>&1; then" % p,
                  "  $PKG --min-free $M update %s >>$LOG 2>&1 || fail 'updating %s'" % (p, p),
                  "  report '%s updated'" % p,
                  "else",
                  "  $PKG --min-free $M install %s >>$LOG 2>&1 || fail 'installing %s'" % (p, p),
                  "  report '%s installed'" % p,
                  "fi"]
    lines += ["if $B/bin/pkg list %s >/dev/null 2>&1; then" % AGENT_PKG,
              "  $B/bin/pkg update %s >>$LOG 2>&1 || fail 'updating the VS Code agent'" % AGENT_PKG,
              "  report 'VS Code agent: %s updated'" % AGENT_PKG,
              "else",
              "  $B/bin/pkg install %s >>$LOG 2>&1 || fail 'installing the VS Code agent'" % AGENT_PKG,
              "  report 'VS Code agent: %s installed'" % AGENT_PKG,
              "fi"]
    lines += agent_lines(host, agent_key)
    lines += ["",
              "# no installed pkg reports here: the bootstrap's pkg and its lib/",
              "# are copied out first, the bootstrap's removal takes them with it",
              "R=/tmp/amixboot-report; /usr/bin/rm -rf $R",
              "/usr/bin/mkdir $R && /usr/bin/cp $B/bin/pkg $B/lib/* $R/ && RPKG=$R/pkg RLD=$R || "
              "fail 'copying the reporter to /tmp'",
              "step 8 \"removing the bootstrap\"",
              "cd / && /usr/sbin/pkgrm -n -a /tmp/amixboot.admin %s >>$LOG 2>&1 || unboot" % BOOT_PKG,
              "rm -f /tmp/amixboot.admin /tmp/go /tmp/amix-steps",
              "/usr/bin/sync",
              "report DONE",
              "/usr/bin/rm -rf $R",
              ""]
    return "\n".join(lines)


def steps_script(cfg, host, http_port=HTTP_PORT, data_disk=None, repo=None, kernel=None,
                 patch=False, now=None, agent_key=None):
    """The steps AMIX runs after the bootstrap. data_disk: (raw slice, sectors)
    of the empty data disk partition to make a file system on, or None.
    With repo, packages it does not hold are left out and reported.
    kernel: (size, System V sum) of the kernel served at /kernel, or None.
    patch: the user's patch disk is served at /patch. now: the host's UTC time.
    agent_key: the VS Code agent's key, when its package is chosen."""
    pkgs, missing = packages_for(cfg)
    pkgs = [p for p in pkgs if p not in FOUNDATION_PKGS]
    reserve = model.RESERVE_MB
    have = list(FOUNDATION_PKGS)
    if repo:
        gone = [(p, missing_in_repo(repo, p)) for p in pkgs]
        missing = missing + [("%s (not in the repository)" % p) if m == p else
                             ("%s (needs %s, not in the repository)" % (p, m))
                             for p, m in gone if m]
        pkgs = [p for p, m in gone if not m]
        have = [p for p in FOUNDATION_PKGS if not missing_in_repo(repo, p)]
        if "060" not in cfg.tags():
            fast = [(p, needs_060(repo, p)) for p in pkgs]
            missing = missing + [("%s (68060 code: this machine has no 68060)" % p) if m == p else
                                 ("%s (needs %s, 68060 code: this machine has no 68060)" % (p, m))
                                 for p, m in fast if m]
            pkgs = [p for p, m in fast if not m]
    sizes = {}
    if repo:
        # what each install fetches, in the order the script installs them
        done = set()
        for p in ["sysutils/amix-pkg"] + have + (["\0options"] if pkgs else []):
            sizes[p] = fetch_size(repo, pkgs if p == "\0options" else [p], done)
    W = "http://%s:%d" % (host, http_port)
    lines = ["#!/bin/sh",
             "# AMIX Installer: the install steps, fetched from %s/steps" % W,
             "W=%s" % W, "B=%s" % BOOT_DIR,
             "LD_LIBRARY_PATH=$B/lib; export LD_LIBRARY_PATH",
             "# MB pkg keeps free on each file system (the installer's core reserve)",
             "M=%d" % reserve,
             "REBOOT=",
             "LOG=/tmp/amix-install.log; : > $LOG",
             "# the reports go through the bootstrap's pkg, then the installed one",
             "# with the library path each needs: the bootstrap's own lib/, so that a",
             "# FAILED still reaches the installer after LD_LIBRARY_PATH is cleared",
             "RPKG=$B/bin/pkg; RLD=$B/lib",
             "report() { echo \"$*\"; echo \"$*\" | LD_LIBRARY_PATH=$RLD $RPKG http put \"$W/report/msg\" >/dev/null; }",
             "fail() { report \"FAILED: $*\"; tail -20 $LOG | LD_LIBRARY_PATH=$RLD $RPKG http put \"$W/report/log\" >/dev/null; exit 1; }",
             "step() { n=$1; shift; report \"step $n: $*\"; }",
             "",
             "step 1 \"the system: `uname -a`\"",
             "report \"root: `/usr/bin/df -k / | /usr/bin/tail -1`\""]
    lines += unboot_lines()
    if repo:
        table = unpacked_kb(repo, ["sysutils/amix-pkg"] + have + pkgs)
        data_kb = data_disk[1] // 2 * 9 // 10 if data_disk else None     # less UFS's own
        lines += room_lines(table, data_kb, 2 * (kernel[0] // 1024 + 1) if kernel else 0)
    lines += ["",
              "step 2 \"foundation: the AMIX patch level\""]
    lines += patch_check_lines(patch)
    lines += ["",
              "step 3 \"the package client\"",
              "$B/bin/pkg set-publisher -O $W/repo amix >>$LOG 2>&1 || fail set-publisher",
              "$B/bin/pkg refresh >>$LOG 2>&1 || fail refresh"]
    if "sysutils/amix-pkg" in sizes:
        lines += [fetching("sysutils/amix-pkg", sizes["sysutils/amix-pkg"])]
    lines += [
              "$B/bin/pkg --min-free $M install sysutils/amix-pkg >>$LOG 2>&1 || fail 'installing the package client'",
              "$B/bin/pkg --min-free $M update sysutils/amix-pkg >>$LOG 2>&1 || fail 'updating the package client'",
              "PKG=/usr/bin/pkg",
              "LD_LIBRARY_PATH=; export LD_LIBRARY_PATH",
              "$PKG refresh >>$LOG 2>&1 || fail 'the installed client'",
              "RPKG=$PKG; RLD=",
              "report 'foundation sysutils/amix-pkg installed'"]
    if data_disk:
        slice_, sectors = data_disk[:2]
        # where the machine finds it: its /etc/vfstab line (the same in FS-UAE
        # unless the disks go on the Z3660's SD card)
        on_machine = data_disk[2] if len(data_disk) > 2 else slice_
        lines += ["",
                  "step 4 \"the data disk on %s (%d sectors)\"" % (slice_, sectors),
                  "D=%s" % slice_,
                  "V=%s" % on_machine,
                  "if /usr/sbin/mount | /usr/bin/grep /amixdata >/dev/null; then report 'data disk mounted already'; else",
                  "  # never over a file system: only an empty disk is made",
                  "  if /usr/lib/fs/ufs/fstyp $D >/dev/null 2>&1; then fail \"$D has a file system already: "
                  "the installer does not overwrite it (mount it on /amixdata, or give an empty disk)\"; fi",
                  "  /usr/sbin/mkfs -F ufs $D %d >>$LOG 2>&1 || fail mkfs" % sectors,
                  "  { [ -d /amixdata ] || /usr/bin/mkdir -p /amixdata; } && /usr/sbin/mount -F ufs `echo $D | /usr/bin/sed s/rdsk/dsk/` /amixdata >>$LOG 2>&1 || fail mount",
                  "  echo \"`echo $V | /usr/bin/sed s/rdsk/dsk/` $V /amixdata ufs 2 yes -\" >> /etc/vfstab",
                  "fi",
                  "for d in %s; do" % " ".join(d.lstrip("/") for d in hdf_movable()),
                  "  [ ! -h /$d ] || continue",
                  "  n=`echo $d | /usr/bin/sed 's|/|-|g'`",
                  "  if [ ! -d /$d ]; then",
                  "    # not there yet: its packages go straight to the data disk",
                  "    { [ -d /amixdata/$n ] || /usr/bin/mkdir /amixdata/$n; } && /usr/bin/ln -s /amixdata/$n /$d || fail \"link /$d\"",
                  "    report \"/$d is on the data disk\"",
                  "    continue",
                  "  fi",
                  "  report \"moving /$d to /amixdata/$n\"",
                  "  [ -d /amixdata/$n ] || /usr/bin/mkdir /amixdata/$n || fail \"mkdir /amixdata/$n\"",
                  "  (cd /$d && /usr/bin/find . -print | /usr/bin/cpio -pdm /amixdata/$n) >>$LOG 2>&1 || fail \"copy /$d\"",
                  "  a=`cd /$d && /usr/bin/find . -print | /usr/bin/wc -l`; b=`cd /amixdata/$n && /usr/bin/find . -print | /usr/bin/wc -l`",
                  "  [ $a -eq $b ] || fail \"/$d: $a entries, copy has $b\"",
                  "  /usr/bin/mv /$d /$d.moved && /usr/bin/ln -s /amixdata/$n /$d && /usr/bin/rm -rf /$d.moved || fail \"link /$d\"",
                  "done"]
    lines += ["", "step 5 \"foundation: clock, base packages, syslogd\""]
    lines += foundation_lines(have, now or time.gmtime(), sizes)
    lines += ["report 'foundation DONE'",
              "", "step 6 \"packages: %s\"" % (" ".join(pkgs) or "none")]
    if pkgs:
        if "\0options" in sizes:
            lines += [fetching("%d packages" % len(pkgs), sizes["\0options"])]
        # install leaves an older installed version as it is (the source disk
        # may have one, e.g. vscode-agent 0.1.1): update brings it up to date
        lines += ["$PKG --min-free $M install %s >>$LOG 2>&1 || fail 'installing packages'"
                  % " ".join(pkgs),
                  "$PKG --min-free $M update %s >>$LOG 2>&1 || fail 'updating packages'"
                  % " ".join(pkgs)]
    if missing:
        lines += ["report 'not packaged yet, left out: %s'" % ", ".join(missing).replace("'", "")]
    if agent_key and "developer/vscode-agent" in pkgs:
        lines += agent_lines(host, agent_key)
    card = X_CARD.get(cfg.sel.get("gfx")) if "x11" in cfg.tags() else None
    if card:
        lines += ["", "[ -d /etc/X11 ] || /usr/bin/mkdir /etc/X11 || fail 'mkdir /etc/X11'",
                  "echo %s > /etc/X11/amix-card || fail 'writing /etc/X11/amix-card'" % card,
                  "report 'X card driver: %s (/etc/X11/amix-card)'" % card]
    lines += kernel_lines(cfg, kernel)
    lines += ["",
              "step 8 \"removing the bootstrap\"",
              "cd / && /usr/sbin/pkgrm -n -a /tmp/amixboot.admin %s >>$LOG 2>&1 || unboot" % BOOT_PKG,
              "rm -f /tmp/amixboot.admin /tmp/go /tmp/amix-steps",
              "/usr/bin/sync; /usr/bin/sync",
              "[ -z \"$REBOOT\" ] || report 'restart AMIX now: cd /; shutdown -y -g0 -i6'",
              "report DONE",
              ""]
    return "\n".join(lines)


def kernel_lines(cfg, kernel_file):
    """Step 7: the kernel made here (kernel_file = (size, sysv sum) or None):
    the stock one with the cards' drivers, or our 68040/060 kernel; its device
    nodes, the install network card's network line."""
    from . import kernel as kmod
    from .plan import kernel_built, kernel_drivers
    if not kernel_built(cfg):
        return []
    ids = kernel_drivers(cfg)
    drvs = [kmod.DRIVERS[i] for i in ids]
    k040 = cfg.sel.get("kern", "stock") != "stock"
    what = model.find_group("kern").option(cfg.sel["kern"]).title if k040 else \
        "the %s driver%s" % (" and ".join(d.card for d in drvs), "s" if len(drvs) > 1 else "")
    if not kernel_file:
        return ["", "report 'no kernel with %s was made: the stock kernel stays'" % what]
    size, ksum = kernel_file
    nodes = ", ".join(kmod.describe(d) for d in drvs) or what
    lines = ["",
             "step 7 \"the kernel with %s\"" % nodes,
             "$PKG http get $W/kernel /stand/unix.new >>$LOG 2>&1 || fail 'fetching the kernel'",
             "set -- `/usr/bin/sum /stand/unix.new`",
             "[ \"$1\" = %d ] && [ `/usr/bin/wc -c </stand/unix.new` -eq %d ] || "
             "{ /usr/bin/rm -f /stand/unix.new; fail 'the kernel arrived damaged'; }"
             % (ksum, size),
             "# the stock kernel stays (pvmk's \"AMIX stock\" entry on a 68030)",
             "[ -f /stand/unix.stock ] || /usr/bin/cp /stand/unix /stand/unix.stock || "
             "fail 'keeping the stock kernel'",
             "/usr/bin/mv /stand/unix.new /stand/unix || fail 'installing the kernel'"]
    for d in drvs:
        if d.kind == "storage" or not d.node:
            continue                 # disks of AMIX's dd/sd: /dev/dsk, /dev/rdsk exist
        lines += ["[ -c /dev/%s ] || /usr/sbin/mknod /dev/%s c %d 0 || fail 'mknod /dev/%s'"
                  % (d.node, d.node, d.major, d.node),
                  "/usr/bin/chmod %s /dev/%s" % ("666" if d.kind == "graphics" else "600", d.node)]
    # only the card the install runs over gets the host name's address; the
    # further network cards have their driver and device node, set up on AMIX
    from .plan import NET_DRIVER
    for d in drvs:
        if d.kind != "network" or NET_DRIVER.get(cfg.sel.get("net")) != d.id:
            continue
        N, ifc = "/etc/inet/network-config", d.node
        lines += ["if /usr/bin/grep %s %s >/dev/null; then report '%s is in %s already'; else"
                  % (ifc, N, ifc, N),
                  "  /usr/bin/cat >> %s <<'EOF'%sEOF" % (N, net_config(d)),
                  "  report '%s added to %s (the address of `uname -n`)'" % (ifc, N),
                  "fi"]
    lines += ["report 'the new kernel starts at the next boot; %s'" % (
                  "the stock one is kept as /stand/unix.stock (it does not run on a "
                  "68040/060)" if k040 else "pvmk lists the old one as AMIX stock"),
              "REBOOT=yes"]
    return lines


def unboot_lines():
    """Shell function unboot: the bootstrap removed without pkgrm, when pkgrm
    cannot (AMIX's pkgrm stops at any contents line it cannot parse, such as
    X11R5's "/usr/x11r5/fonts/server/MacFS/TrueType Fonts" with its space):
    its files, its lines of the contents file (only lines under BOOT_DIR that
    end in the package name; the old file kept as .amix-installer), its
    /var/sadm/pkg record."""
    C = "/var/sadm/install/contents"
    return ["",
            "unboot() {",
            "  # the usual case on AMIX: no failure, nothing to report until the end",
            "  n=`/usr/bin/grep -c ' %s$' %s`" % (BOOT_PKG, C),
            "  m=`/usr/bin/grep -c '^%s[/ ].* %s$' %s`" % (BOOT_DIR, BOOT_PKG, C),
            "  [ \"$n\" -eq \"$m\" ] || { report \"%s owns $n lines, $m under %s: remove it by hand\"; return; }"
            % (BOOT_PKG, BOOT_DIR),
            "  [ -f %s.amix-installer ] || /usr/bin/cp -p %s %s.amix-installer || "
            "{ report 'keeping %s failed: remove %s by hand'; return; }" % (C, C, C, C, BOOT_PKG),
            "  a=`/usr/bin/wc -l < %s`" % C,
            "  /usr/bin/cp -p %s %s.new && /usr/bin/grep -v ' %s$' %s > %s.new || "
            "{ /usr/bin/rm -f %s.new; report 'the contents without %s failed: remove it by hand'; return; }"
            % (C, C, BOOT_PKG, C, C, C, BOOT_PKG),
            "  b=`/usr/bin/wc -l < %s.new`" % C,
            "  [ `/usr/bin/expr $a - $b` -eq $n ] || { /usr/bin/rm -f %s.new; "
            "report 'the contents would lose more than %s: remove it by hand'; return; }" % (C, BOOT_PKG),
            "  /usr/bin/mv %s.new %s || { report 'writing %s failed'; return; }" % (C, C, C),
            "  /usr/bin/rm -rf %s /var/sadm/pkg/%s" % (BOOT_DIR, BOOT_PKG),
            "  report '%s removed (its files and records directly: AMIX pkgrm cannot read the contents file)'" % BOOT_PKG,
            "}"]


# --- the servers --------------------------------------------------------------

def _mb(n):
    return "%.1f MB" % (n / 1048576.0) if n >= 1048576 else "%d KB" % ((n + 1023) // 1024)


def net_progress(step, nsteps, fetch, got, t_fetch, quiet, done=False, failed=False,
                 started=True):
    """The network install's bar: (fraction, bar text, line under it), from
    Session.activity().  While pkg fetches, the bar is its bytes served; else
    the step.  quiet: seconds without a report or a file served."""
    if done:
        return 1.0, "Done", ""
    if not started:
        return 0.0, "Waiting for AMIX", "Nothing yet: type the lines above at the AMIX console"
    where = "step %d of %d" % (step, nsteps) if step else "starting"
    if failed:
        return max(0.0, (step - 1) / float(nsteps)), "Stopped at %s" % where, ""
    if fetch:
        what, files, total = fetch
        frac = min(got[1] / float(total), 0.99) if total else 0.0
        rate = got[1] / 1024.0 / max(t_fetch, 1.0)
        if got[0] >= files:
            frac, bar = 0.99, "%s: fetched, unpacking on AMIX" % what
        else:
            bar = "%s: %s of %s" % (what, _mb(got[1]), _mb(total))
        text = "%d of %d files fetched, %d KB/s, %s" % (min(got[0], files), files, rate, where)
    else:
        frac = max(0.0, (step - 1) / float(nsteps))
        bar = where[0].upper() + where[1:]
        text = ""
    if quiet >= 120:
        text = ("No report or download from AMIX for %d min %02d s: it may still be working, "
                "or waiting at its console" % (quiet // 60, quiet % 60))
    elif quiet >= 5:
        text = (text + ", " if text else "") + "last sign of AMIX %d s ago" % quiet
    return frac, bar, text



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
        self.progress = {}
        self.stopped = False

    def stop(self):
        self.stopped = True
        self.sock.close()

    def _send(self, addr, block):
        data = self.sessions[addr]
        chunk = data[(block - 1) * 512: block * 512]
        self.sock.sendto(struct.pack("!HH", 3, block & 0xffff) + chunk, addr)

    def _progress(self, addr, blk, total, size):
        """A log line every 10 % of a transfer, and one at its end: an emulated
        A2065 can take minutes for the bootstrap."""
        p = self.progress.get(addr)
        if p is None:
            return
        name, t0, nxt = p
        pct = 100 * blk // total
        if pct < nxt and blk < total:
            return
        done = min(blk * 512, size)
        dt = max(time.time() - t0, 0.001)
        if blk >= total:
            self.log("tftp: %s sent in %d s (%d KB/s)" % (name, dt, done / 1024 / dt))
            del self.progress[addr]
        else:
            self.log("tftp: %s %d%% (%.1f of %.1f MB, %d KB/s)"
                     % (name, pct, done / 1048576.0, size / 1048576.0, done / 1024 / dt))
            p[2] = pct // 10 * 10 + 10

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
                try:
                    data = data() if callable(data) else data
                except Exception as e:  # noqa: BLE001 -- the server must keep answering
                    # a file that cannot be made: an ERROR, so the client stops
                    # waiting, and the thread lives on for the next request
                    self.sock.sendto(struct.pack("!HH", 5, 0) + ("%s cannot be made: %s"
                                     % (name, e))[:200].encode("latin-1", "replace") + b"\0",
                                     addr)
                    self.log("tftp: %s asked for %s: it cannot be made (%s)" % (addr[0], name, e))
                    continue
                self.sessions[addr] = data
                self.progress[addr] = [name, time.time(), 10]
                self.log("tftp: sending %s to %s (%d bytes)" % (name, addr[0],
                                                               len(self.sessions[addr])))
                self._send(addr, 1)
            elif op == 4 and addr in self.sessions:       # ACK
                blk = struct.unpack("!H", pkt[2:4])[0]
                data = self.sessions[addr]
                total = len(data) // 512 + 1
                self._progress(addr, blk, total, len(data))
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
        if path == "/now":
            # this computer's time to the second (UTC), for setclk -t
            return self._reply(200, b"%d\n" % int(time.time()))
        if path == "/kernel" and st.kernel:
            return self._reply(200, st.kernel[0], "application/octet-stream")
        if path == "/patch" and st.patch:
            with open(st.patch, "rb") as fh:
                return self._reply(200, fh.read(), "application/octet-stream")
        if path.startswith("/repo/"):
            rel = os.path.normpath(path[len("/repo/"):])
            if rel.startswith("..") or os.path.isabs(rel):
                return self._reply(403)
            f = os.path.join(st.repo, rel)
            if os.path.isfile(f):
                with open(f, "rb") as fh:
                    data = fh.read()
                self._reply(200, data, "application/octet-stream")
                if self.command == "GET":
                    st.served(len(data))
                return
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
                 tftp_port=TFTP_PORT, http_port=HTTP_PORT, bind="0.0.0.0", kernel=None,
                 patch=None, report=None):
        self.cfg = cfg
        self.patch = patch          # the user's patch disk image, served at /patch
        self.report = report        # foundation report file, or None
        self.foundation = []        # its lines, as AMIX sent them
        self.kernel = None          # (bytes, size, sum) of the kernel for AMIX
        if kernel:
            with open(kernel, "rb") as f:
                data = f.read()
            self.kernel = (data, len(data), sysv_sum(data))
        self.repo = repo
        self.host = host or FS_UAE_HOST
        self.data_disk = data_disk
        self.log = log or (lambda t: None)
        self.messages = []
        self.step = 0               # the go script's step, from its "step N: ..." reports
        self.fetch = None           # [what, files, bytes] of the install pkg is fetching
        self.got = [0, 0]           # files and bytes served from the repository since then
        self.last = time.time()     # the last sign of AMIX: a report or a file served
        self.t_fetch = self.last
        self._lock = threading.Lock()
        self.done = False
        self.failed = False
        self.failure = None             # the reason of the FAILED line
        self._boot = None
        # the VS Code agent's key: in AMIX's /etc/vscode-agent.conf and, for
        # AMIX: Set Agent Key in VS Code, in vscode-agent.key beside the report
        self.agent_key = None
        self.agent_key_file = None
        if cfg.on.get("vscode"):
            self.agent_key = secrets.token_hex(32)
            if report:
                # beside the report, and always where vscode-amix's "Use the
                # AMIX Installer's key" reads it (STATE_DIR): 0.6.8 wrote only
                # the first, and VS Code offered the key of an older install
                self.agent_key_file = write_agent_key(self.agent_key, os.path.dirname(report))
                self.log("VS Code agent key in %s: AMIX: Set Agent Key in VS Code" % self.agent_key_file)
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
        if getattr(self.cfg, "agent_only", False):
            return agent_only_script(self.host, self.http_port, self.repo, self.agent_key,
                                     extra=getattr(self.cfg, "agent_extra", []))
        return steps_script(self.cfg, self.host, self.http_port, self.data_disk, self.repo,
                            self.kernel[1:] if self.kernel else None, bool(self.patch),
                            agent_key=self.agent_key)

    def _write_report(self):
        """foundation-report.txt: one line per item, the same on every machine."""
        if not self.report:
            return
        try:
            with open(self.report + ".part", "w") as f:
                f.write("# AMIX foundation, %s\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
                f.write("\n".join(self.foundation) + "\n")
            os.replace(self.report + ".part", self.report)
        except OSError as e:
            self.log("foundation report: %s" % e)

    def commands(self):
        """What to type at the AMIX console, as root."""
        return ["tftp %s %d" % (self.host, self.tftp.port), "binary", "get go", "quit", "sh go"]

    def served(self, nbytes):
        """A repository file sent to AMIX: progress of the current fetch."""
        with self._lock:
            self.got[0] += 1
            self.got[1] += nbytes
            self.last = time.time()

    def activity(self):
        """What the window shows: (step, NSTEPS, fetch or None, files and bytes
        served for it, seconds since it began, seconds since AMIX's last sign)."""
        now = time.time()
        with self._lock:
            return (self.step, NSTEPS, self.fetch, tuple(self.got), now - self.t_fetch,
                    now - self.last)

    def reported(self, kind, text):
        for line in text.splitlines() or [""]:
            with self._lock:
                self.last = time.time()
                if kind == "msg":
                    m = re.match(r"^step (\d+)", line)
                    f = FETCH_RE.match(line)
                    if m:
                        self.step, self.fetch = int(m.group(1)), None
                    elif f:
                        self.fetch = [f.group(1), int(f.group(2)), int(f.group(3)) * 1024]
                        self.got, self.t_fetch = [0, 0], self.last
                    elif self.fetch and (" installed" in line or "FAILED" in line):
                        self.fetch = None
            t = time.strftime("%H:%M:%S")
            self.messages.append((t, kind, line))
            if kind == "msg" and line.startswith("foundation "):
                self.foundation.append(line)
                self._write_report()
            if line == "DONE":
                self.done = True
            if line.startswith("FAILED"):
                self.failed = True
                self.failure = line[len("FAILED"):].lstrip(": ") or None
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
