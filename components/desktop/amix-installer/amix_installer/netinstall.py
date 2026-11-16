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
    "cde": ["desktop/cde", "desktop/cde-dtlogin"], "mwm": ["desktop/motif"], "netsurf": ["web/netsurf"],
    "media": ["multimedia/ffmpeg", "desktop/cde-apps"], "grafx2": ["graphics/grafx2"],
    "sysmon": ["desktop/cde-apps"], "games": ["games/quake", "games/ioquake3"],
    # gcc14/gcc27: developer/gcc-14 and gcc-27 are cross compilers on the PC; a
    # compiler that runs on AMIX is not packaged yet (reported as such)
    "pkgconf": ["developer/pkgconf"], "gnu": ["gnu/gnu-tools"], "python": ["runtime/python3"],
    "vim": ["editor/vim"],
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
    """IPS packages for the chosen options, and the options not packaged yet."""
    pkgs, missing = [], []
    for s in model.STEPS:
        for g in s.groups:
            if g.id in ("pkgclient", "kern", "boot", "src", "target", "mach", "accel", "cpu",
                        "layout", "dsize", "net", "keymap", "desk", "gfx"):
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
    if "x11" in cfg.tags() and X_SERVER not in pkgs:
        pkgs = [X_SERVER] + pkgs
    return pkgs, missing


def fs_uae_data_disk(pl):
    """(raw slice, sectors) of the data disk a plan wrote, as AMIX sees it in
    FS-UAE (SCSI ID 1, first partition = slice 1), or None without one.
    Raises OSError or hdf.NotAnImage when the disk cannot be read."""
    from . import hdf, plan
    dd = [a for a in pl.actions if a.id == "data-disk"]
    if not dd:
        return None
    with hdf.Image(os.path.join(pl.paths["out_dir"], dd[0].name)) as im:
        blocks = im.read_rdb()[0].blocks
    return ("/dev/rdsk/c%dd0s1" % plan.SCSI_ID["data"], blocks)


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


def report_path(pl):
    """foundation-report.txt in the plan's out folder (or the current one)."""
    return os.path.join(pl.paths.get("out_dir") or os.getcwd(), "foundation-report.txt")


def sysv_sum(data):
    """What AMIX's sum(1) prints first: the System V checksum."""
    s = sum(bytearray(data)) & 0xffffffff
    r = (s & 0xffff) + (s >> 16)
    return (r & 0xffff) + (r >> 16)


# the X card driver for /etc/X11/amix-card, by the installer's graphics card
# (the Z3660's becomes "z3660" when its card driver is split from zz9000)
X_CARD = {"picasso2": "cirrus", "rtg": "zz9000", "zz9000": "zz9000"}
# a HydraNet comes up as AMIX's aen0 does in /etc/inet/network-config: plumbed
# when the board answers, with the host name's address
HYDRA_NETCONFIG = """
# Hydra Systems AmigaNet (written by the AMIX Installer)
/usr/sbin/slink addaen /dev/hya0 hya0 2>/dev/null &&
	/usr/sbin/ifconfig hya0 `uname -n` up -trailers
"""
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


def foundation_lines(have, now):
    """Shell lines: the clock, the base set, syslogd, the random driver.
    have: the foundation packages the repository holds (the others fail the
    install). now: the host's time (UTC, a time.struct_time)."""
    stamp = "%02d%02d%02d%02d%04d" % (now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min,
                                      now.tm_year)
    gone = [p for p in FOUNDATION_PKGS if p not in have]
    lines = ["report 'foundation %s failed: not in the repository'" % p for p in gone]
    if gone:
        lines += ["fail 'the foundation needs %s: not in the repository'" % " ".join(gone)]
    for p in FOUNDATION_PKGS:
        if p in gone:
            continue
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
        lines += ["if $PKG list %s >/dev/null 2>&1; then report 'foundation %s present'; else" % (p, p),
                  "  $PKG --min-free $M install %s >>$LOG 2>&1 || "
                  "{ %sreport 'foundation %s failed'; fail 'installing %s'; }"
                  % (p, ("[ ! -f %s.core ] || /usr/bin/mv %s.core %s; " % ((HANDOVER[p],) * 3))
                     if p in HANDOVER else "", p, p),
                  "  report 'foundation %s installed'" % p,
                  "fi"]
        if p in HANDOVER:
            lines += ["/usr/bin/rm -f %s.core" % HANDOVER[p]]
    lines += [
        "# the battery clock keeps local time (as AmigaDOS): setclk reads it in this zone",
        "if [ -s /etc/clkzone ]; then report \"foundation clkzone present `/usr/bin/cat /etc/clkzone`\"; else",
        "  Z=`. /etc/TIMEZONE; echo $TZ`",
        "  [ -n \"$Z\" ] || Z=:GMT",
        "  echo \"$Z\" > /etc/clkzone || fail 'writing /etc/clkzone'",
        "  report \"foundation clkzone installed $Z\"",
        "fi",
        "# the time from this computer (UTC), then into the battery clock",
        "TZ=GMT0 /usr/bin/date %s >>$LOG 2>&1 || fail 'setting the date'" % stamp,
        "/usr/amiga/bin/setclk -s >>$LOG 2>&1 || report 'foundation clock: the battery clock was not written'",
        "report \"foundation clock installed `/usr/bin/date` (battery `/usr/amiga/bin/setclk -p`)\"",
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
        "  report 'foundation random missing: /dev/random is not packaged yet (a kernel relink)'",
        "fi"]
    return lines


def steps_script(cfg, host, http_port=HTTP_PORT, data_disk=None, repo=None, kernel=None,
                 patch=False, now=None):
    """The steps AMIX runs after the bootstrap. data_disk: (raw slice, sectors)
    of the empty data disk partition to make a file system on, or None.
    With repo, packages it does not hold are left out and reported.
    kernel: (size, System V sum) of the kernel served at /kernel, or None.
    patch: the user's patch disk is served at /patch. now: the host's UTC time."""
    pkgs, missing = packages_for(cfg)
    pkgs = [p for p in pkgs if p not in FOUNDATION_PKGS]
    reserve = model.SPLIT_RESERVE_MB if data_disk else model.ROOT_RESERVE_MB
    have = list(FOUNDATION_PKGS)
    if repo:
        gone = [(p, missing_in_repo(repo, p)) for p in pkgs]
        missing = missing + [("%s (not in the repository)" % p) if m == p else
                             ("%s (needs %s, not in the repository)" % (p, m))
                             for p, m in gone if m]
        pkgs = [p for p, m in gone if not m]
        have = [p for p in FOUNDATION_PKGS if not missing_in_repo(repo, p)]
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
             "RPKG=$B/bin/pkg",
             "report() { echo \"$*\"; echo \"$*\" | $RPKG http put \"$W/report/msg\" >/dev/null; }",
             "fail() { report \"FAILED: $*\"; tail -20 $LOG | $RPKG http put \"$W/report/log\" >/dev/null; exit 1; }",
             "step() { n=$1; shift; report \"step $n: $*\"; }",
             "",
             "step 1 \"the system: `uname -a`\"",
             "report \"root: `/usr/bin/df -k / | /usr/bin/tail -1`\"",
             "",
             "step 2 \"foundation: the AMIX patch level\""]
    lines += patch_check_lines(patch)
    lines += ["",
              "step 3 \"the package client\"",
              "$B/bin/pkg set-publisher -O $W/repo amix >>$LOG 2>&1 || fail set-publisher",
              "$B/bin/pkg refresh >>$LOG 2>&1 || fail refresh",
              "$B/bin/pkg --min-free $M install sysutils/amix-pkg >>$LOG 2>&1 || fail 'installing the package client'",
              "$B/bin/pkg --min-free $M update sysutils/amix-pkg >>$LOG 2>&1 || fail 'updating the package client'",
              "PKG=/usr/bin/pkg",
              "LD_LIBRARY_PATH=; export LD_LIBRARY_PATH",
              "$PKG refresh >>$LOG 2>&1 || fail 'the installed client'",
              "RPKG=$PKG",
              "report 'foundation sysutils/amix-pkg installed'"]
    if data_disk:
        slice_, sectors = data_disk
        lines += ["",
                  "step 4 \"the data disk on %s (%d sectors)\"" % (slice_, sectors),
                  "D=%s" % slice_,
                  "if /usr/sbin/mount | /usr/bin/grep /amixdata >/dev/null; then report 'data disk mounted already'; else",
                  "  /usr/sbin/mkfs -F ufs $D %d >>$LOG 2>&1 || fail mkfs" % sectors,
                  "  { [ -d /amixdata ] || /usr/bin/mkdir -p /amixdata; } && /usr/sbin/mount -F ufs `echo $D | /usr/bin/sed s/rdsk/dsk/` /amixdata >>$LOG 2>&1 || fail mount",
                  "  echo \"`echo $D | /usr/bin/sed s/rdsk/dsk/` $D /amixdata ufs 2 yes -\" >> /etc/vfstab",
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
    lines += foundation_lines(have, now or time.gmtime())
    lines += ["report 'foundation DONE'",
              "", "step 6 \"packages: %s\"" % (" ".join(pkgs) or "none")]
    if pkgs:
        lines += ["$PKG --min-free $M install %s >>$LOG 2>&1 || fail 'installing packages'"
                  % " ".join(pkgs)]
    if missing:
        lines += ["report 'not packaged yet, left out: %s'" % ", ".join(missing).replace("'", "")]
    if cfg.sel.get("kern") not in (None, "stock"):
        lines += ["", "report 'the %s kernel is not packaged yet: the stock kernel stays'"
                  % model.find_group("kern").option(cfg.sel["kern"]).title]
    card = X_CARD.get(cfg.sel.get("gfx")) if "x11" in cfg.tags() else None
    if card:
        lines += ["", "[ -d /etc/X11 ] || /usr/bin/mkdir /etc/X11 || fail 'mkdir /etc/X11'",
                  "echo %s > /etc/X11/amix-card || fail 'writing /etc/X11/amix-card'" % card,
                  "report 'X card driver: %s (/etc/X11/amix-card)'" % card]
    lines += kernel_lines(cfg, kernel)
    lines += unboot_lines()
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
    """Step 7: the kernel linked here with the cards' drivers (kernel_file =
    (size, sysv sum) or None), its device nodes, a HydraNet's network line."""
    from . import kernel as kmod
    from .plan import kernel_drivers
    ids = kernel_drivers(cfg.sel)
    if not ids:
        return []
    drvs = [kmod.DRIVERS[i] for i in ids]
    cards = " and ".join(d.card for d in drvs)
    if not kernel_file:
        return ["", "report 'no kernel with the %s driver was made: the card stays unused'"
                % cards]
    size, ksum = kernel_file
    nodes = ", ".join("/dev/%s (major %d)" % (d.node, d.major) for d in drvs)
    lines = ["",
             "step 7 \"the kernel with %s\"" % nodes,
             "$PKG http get $W/kernel /stand/unix.new >>$LOG 2>&1 || fail 'fetching the kernel'",
             "set -- `/usr/bin/sum /stand/unix.new`",
             "[ \"$1\" = %d ] && [ `/usr/bin/wc -c </stand/unix.new` -eq %d ] || "
             "{ /usr/bin/rm -f /stand/unix.new; fail 'the kernel arrived damaged'; }"
             % (ksum, size),
             "# the stock kernel stays, for pvmk's \"AMIX stock\" entry",
             "[ -f /stand/unix.stock ] || /usr/bin/cp /stand/unix /stand/unix.stock || "
             "fail 'keeping the stock kernel'",
             "/usr/bin/mv /stand/unix.new /stand/unix || fail 'installing the kernel'"]
    for d in drvs:
        lines += ["[ -c /dev/%s ] || /usr/sbin/mknod /dev/%s c %d 0 || fail 'mknod /dev/%s'"
                  % (d.node, d.node, d.major, d.node),
                  "/usr/bin/chmod %s /dev/%s" % ("666" if d.kind == "graphics" else "600", d.node)]
    if "hydra" in ids:
        N = "/etc/inet/network-config"
        lines += ["if /usr/bin/grep hya0 %s >/dev/null; then report 'hya0 is in %s already'; else"
                  % (N, N),
                  "  /usr/bin/cat >> %s <<'EOF'%sEOF" % (N, HYDRA_NETCONFIG),
                  "  report 'hya0 added to %s (the address of `uname -n`)'" % N,
                  "fi"]
    lines += ["report 'the new kernel starts at the next boot; pvmk lists the old one as AMIX stock'",
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
            "  report 'pkgrm %s failed: removing its files and records itself'" % BOOT_PKG,
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
            "  report '%s removed'" % BOOT_PKG,
            "}"]


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
                self.sessions[addr] = data() if callable(data) else data
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
        return steps_script(self.cfg, self.host, self.http_port, self.data_disk, self.repo,
                            self.kernel[1:] if self.kernel else None, bool(self.patch))

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

    def reported(self, kind, text):
        for line in text.splitlines() or [""]:
            t = time.strftime("%H:%M:%S")
            self.messages.append((t, kind, line))
            if kind == "msg" and line.startswith("foundation "):
                self.foundation.append(line)
                self._write_report()
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
