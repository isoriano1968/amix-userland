"""SVR4 package datastreams, as pkgtrans writes them, for AMIX's stock pkgadd.

    # PaCkAgE DaTaStReAm             header, padded to 512 bytes
    PKG 1 SIZE
    # end of header
    cpio (070701): PKG/pkginfo, PKG/pkgmap, TRAILER!!!       padded to 512
    cpio (070701): pkginfo, pkgmap, install/..., reloc/...    padded to 512

One part. pkgmap lines: "1 d none path mode owner group",
"1 f none path mode owner group size sum mtime", "1 s none path=target",
"1 i name size sum mtime". The sum is SVR4 sum(1): the bytes added up,
folded to 16 bits. AMIX's cpio and pkgadd read 070701 (checked in its
binaries); the format is checked with OpenIndiana's pkgtrans and pkgchk.
"""
import time

BLOCK = 512


def svr4_sum(data):
    s = sum(data)
    r = (s & 0xffff) + ((s >> 16) & 0xffff)
    return (r & 0xffff) + (r >> 16)


class Entry(object):
    """A path below BASEDIR: kind 'd', 'f' or 's'."""

    def __init__(self, path, kind, mode=0o755, owner="root", group="bin", data=b"",
                 target=None):
        self.path = path.strip("/")
        self.kind = kind
        self.mode = mode
        self.owner = owner
        self.group = group
        self.data = data
        self.target = target


def _pad(b, n):
    return b + b"\0" * ((-len(b)) % n)


def _cpio(members, mtime):
    """members: (name, mode, data); a newc (070701) archive with TRAILER!!!"""
    out = bytearray()
    ino = 1
    for name, mode, data in members + [("TRAILER!!!", 0, b"")]:
        nb = name.encode() + b"\0"
        is_tr = name == "TRAILER!!!"
        fields = [0 if is_tr else ino, mode, 0, 0, 1 if is_tr else (2 if mode & 0o40000 else 1),
                  0 if is_tr else mtime, len(data), 0, 0, 0, 0, len(nb), 0]
        hdr = b"070701" + b"".join(b"%08x" % f for f in fields)
        out += _pad(hdr + nb, 4)
        out += _pad(data, 4)
        ino += 1
    return _pad(bytes(out), BLOCK)


def datastream(pkg, info, entries, scripts=None, mtime=None):
    """pkg: the instance name (AMIXboot); info: pkginfo keys (NAME, VERSION,
    BASEDIR...); entries: Entry list (parents before children); scripts:
    {"postinstall": text, ...}. Returns the datastream bytes."""
    mtime = int(mtime if mtime is not None else time.time())
    scripts = scripts or {}
    keys = dict(info)
    keys.setdefault("PKG", pkg)
    keys.setdefault("ARCH", "m68k")
    keys.setdefault("CATEGORY", "application")
    keys.setdefault("CLASSES", "none")
    order = ["PKG", "NAME", "ARCH", "VERSION", "CATEGORY", "BASEDIR"]
    lines = ["%s=%s" % (k, keys[k]) for k in order if k in keys]
    lines += ["%s=%s" % (k, v) for k, v in keys.items() if k not in order]
    pkginfo = ("\n".join(lines) + "\n").encode()

    body = []
    blocks = 0
    for e in entries:
        if e.kind == "d":
            body.append("1 d none %s %04o %s %s" % (e.path, e.mode, e.owner, e.group))
        elif e.kind == "f":
            body.append("1 f none %s %04o %s %s %d %d %d" % (
                e.path, e.mode, e.owner, e.group, len(e.data), svr4_sum(e.data), mtime))
            blocks += (len(e.data) + BLOCK - 1) // BLOCK
        elif e.kind == "s":
            body.append("1 s none %s=%s" % (e.path, e.target))
        else:
            raise ValueError(e.kind)
    for name in sorted(scripts):
        data = scripts[name].encode()
        body.append("1 i %s %d %d %d" % (name, len(data), svr4_sum(data), mtime))
        blocks += (len(data) + BLOCK - 1) // BLOCK
    body.append("1 i pkginfo %d %d %d" % (len(pkginfo), svr4_sum(pkginfo), mtime))
    size = blocks + len(entries) + 8
    pkgmap = (": 1 %d\n" % size + "\n".join(body) + "\n").encode()

    header = _pad(("# PaCkAgE DaTaStReAm\n%s 1 %d\n# end of header\n" % (pkg, size)).encode(),
                  BLOCK)
    first = _cpio([("%s/pkginfo" % pkg, 0o100644, pkginfo),
                   ("%s/pkgmap" % pkg, 0o100644, pkgmap)], mtime)
    members = [("pkginfo", 0o100644, pkginfo), ("pkgmap", 0o100644, pkgmap)]
    if scripts:
        members.append(("install", 0o40755, b""))
        for name in sorted(scripts):
            members.append(("install/" + name, 0o100644, scripts[name].encode()))
    members.append(("reloc", 0o40755, b""))
    for e in entries:
        if e.kind == "d":
            members.append(("reloc/" + e.path, 0o40000 | e.mode, b""))
        elif e.kind == "f":
            members.append(("reloc/" + e.path, 0o100000 | e.mode, e.data))
        else:
            members.append(("reloc/" + e.path, 0o120777, e.target.encode()))
    second = _cpio(members, mtime)
    return header + first + second
