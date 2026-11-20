"""The publish gate for shared library dependencies (tools/amix-publish,
tools/amix-needed-check): every DT_NEEDED of every ELF file in a package must
be delivered by the package itself, by a package it requires (directly or
further down), or by the AMIX base system (tools/amix-base-libs); so must
every file it names in org.amix.uses (manifest "uses PATH": data such as
the X server's fonts and rgb.txt, programs it runs).

Where AMIX's ld.so.1 looks: a program's own RPATH, then /usr/lib, and the
program's path also serves the libraries it loads.  So a program's NEEDED
must be at RPATH/name or /usr/lib/name (or be an absolute path that is
there); a shared library's NEEDED only has to be delivered somewhere in the
closure, under its name.

2026-11-20: desktop/cde 2.1-26 NEEDED libamixc99.so.1, which no package
delivered and nothing required: xhold died at startcde.  Plain Python, no
binutils: it runs on Linux and OpenIndiana alike."""
import gzip
import os
import subprocess
import tempfile
import shlex
import struct
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
PUBLISHER = "amix"


def base_libs(path=None):
    with open(path or os.path.join(HERE, "amix-base-libs")) as f:
        return set(l.strip() for l in f if l.startswith("/"))


def dynamic(data):
    """(NEEDED names, RPATH dirs, is a shared library) of an ELF file's
    bytes, or None when it is not a dynamic ELF file."""
    if data[:4] != b"\x7fELF" or len(data) < 52:
        return None
    if data[4] != 1:                      # ELFCLASS32 only (m68k)
        return None
    e = ">" if data[5] == 2 else "<"
    etype = struct.unpack_from(e + "H", data, 16)[0]
    phoff, = struct.unpack_from(e + "I", data, 28)
    phentsize, phnum = struct.unpack_from(e + "HH", data, 42)
    loads, dyn = [], None
    for i in range(phnum):
        o = phoff + i * phentsize
        if o + 32 > len(data):
            return None
        ptype, off, vaddr, _, filesz = struct.unpack_from(e + "IIIII", data, o)
        if ptype == 1:
            loads.append((vaddr, off, filesz))
        elif ptype == 2:
            dyn = (off, filesz)
    if not dyn:
        return None

    def file_off(addr):
        for vaddr, off, filesz in loads:
            if vaddr <= addr < vaddr + filesz:
                return addr - vaddr + off
        return None

    ents, strtab = [], None
    for o in range(dyn[0], dyn[0] + dyn[1] - 7, 8):
        tag, val = struct.unpack_from(e + "iI", data, o)
        if tag == 0:
            break
        ents.append((tag, val))
        if tag == 5:
            strtab = file_off(val)
    if strtab is None:
        return None

    def s(off):
        end = data.index(b"\0", strtab + off)
        return data[strtab + off:end].decode("latin-1")

    needed = [s(v) for t, v in ents if t == 1]
    rpath = []
    for t, v in ents:
        if t in (15, 29):                 # DT_RPATH, DT_RUNPATH
            rpath += [d for d in s(v).split(":") if d]
    soname = any(t == 14 for t, _ in ents)
    return needed, rpath, etype == 3 or soname


def _act(line):
    # paths may be quoted (a space in a file name: setuptools)
    try:
        w = shlex.split(line, posix=True)
    except ValueError:
        w = line.split()
    kv = {}
    for x in w[1:]:
        if "=" in x:
            k, _, v = x.partition("=")
            kv.setdefault(k, v)
    return w[0] if w else "", w[1] if len(w) > 1 else "", kv


def _newest(repo, stem):
    d = os.path.join(repo, "publisher", PUBLISHER, "pkg", urllib.parse.quote(stem, safe=""))
    if not os.path.isdir(d):
        return None
    vs = [urllib.parse.unquote(v) for v in os.listdir(d) if not v.endswith(".part")]
    if not vs:
        return None

    def key(v):
        rel, _, rest = v.partition(",")
        br = rest.split("-", 1)[1].split(":")[0] if "-" in rest else "0"
        ts = v.split(":")[-1] if ":" in v else ""
        num = lambda x: [int(p) for p in x.split(".") if p.isdigit()]
        return (num(rel), num(br), ts)
    v = max(vs, key=key)
    with open(os.path.join(d, urllib.parse.quote(v, safe=""))) as f:
        return f.read()


def delivered_and_deps(manifest):
    """(paths a manifest delivers, the stems it requires)."""
    paths, deps = set(), []
    for line in manifest.splitlines():
        t, _, kv = _act(line)
        if t in ("file", "link", "hardlink") and "path" in kv:
            paths.add("/" + kv["path"].lstrip("/"))
        elif t == "depend" and kv.get("type") == "require" and "fmri" in kv:
            f = kv["fmri"]
            for p in ("pkg://%s/" % PUBLISHER, "pkg:/"):
                if f.startswith(p):
                    f = f[len(p):]
            deps.append(f.split("@")[0])
    return paths, deps


def closure_paths(repo, deps, problems, who):
    """Every path the packages deps and their requirements deliver."""
    have, seen, todo = set(), set(), list(deps)
    while todo:
        s = todo.pop()
        if s in seen:
            continue
        seen.add(s)
        man = _newest(repo, s)
        if man is None:
            problems.append("%s: requires %s, which the repository does not hold" % (who, s))
            continue
        p, d = delivered_and_deps(man)
        have |= p
        todo += d
    return have


def check_files(who, files, have, base):
    """files: (path on AMIX, bytes).  have: paths of the package and its
    closure.  Returns the problems, one line each."""
    problems = []
    have = have | base
    names = set(os.path.basename(p) for p in have)
    for path, data in files:
        d = dynamic(data)
        if not d:
            continue
        needed, rpath, is_lib = d
        for n in needed:
            if n.startswith("/"):
                cand = [n]
            else:
                cand = [r.rstrip("/") + "/" + n for r in rpath] + ["/usr/lib/" + n]
            if any(c in have for c in cand):
                continue
            if is_lib and not n.startswith("/") and n in names:
                continue
            problems.append("%s: %s NEEDS %s, which neither it nor a package it requires "
                            "delivers (looked for %s)" % (who, path, n, " ".join(cand)))
    return problems


def uses_of(manifest):
    """The paths of set name=org.amix.uses (every value=)."""
    out = []
    for line in manifest.splitlines():
        try:
            w = shlex.split(line, posix=True)
        except ValueError:
            continue
        if w[:2] == ["set", "name=org.amix.uses"]:
            out += [x[6:] for x in w[2:] if x.startswith("value=")]
    return out


def check_published(repo, stem, base=None):
    """The problems of the newest published version of stem."""
    base = base_libs() if base is None else base
    man = _newest(repo, stem)
    if man is None:
        return ["%s: not in the repository" % stem]
    paths, deps = delivered_and_deps(man)
    problems = []
    have = paths | closure_paths(repo, deps, problems, stem)
    for u in uses_of(man):
        if u not in have and u not in base:
            problems.append("%s: uses %s, which neither it nor a package it requires delivers"
                            % (stem, u))
    files = []
    for line in man.splitlines():
        t, h, kv = _act(line)
        if t == "file" and "path" in kv:
            f = os.path.join(repo, "publisher", PUBLISHER, "file", h[:2], h)
            try:
                with gzip.open(f) as g:
                    head = g.read(4)
                    if head != b"\x7fELF":
                        continue
                    data = head + g.read()
            except OSError:
                continue
            files.append(("/" + kv["path"].lstrip("/"), data))
    return problems + check_files(stem, files, have, base)


def _set_values(manifest, name):
    out = []
    for line in manifest.splitlines():
        try:
            w = shlex.split(line, posix=True)
        except ValueError:
            continue
        if w[:2] == ["set", "name=" + name]:
            out += [x[6:] for x in w[2:] if x.startswith("value=")]
    return out


def _cpu_tag(manifest):
    for line in manifest.splitlines():
        w = line.split()
        if w[:2] == ["set", "name=org.amix.cpu"] and len(w) > 2 and w[2].startswith("value="):
            return w[2][6:]
    return None


def scanner():
    """tools/amix-cpu-scan with an objdump that knows m68k."""
    od = os.environ.get("OBJDUMP")
    if not od:
        for c in (os.path.expanduser("~/opt/m68k-amix/bin/m68k-elf-objdump"), "m68k-elf-objdump"):
            if os.path.isabs(c) and os.access(c, os.X_OK):
                od = c
                break
        od = od or "m68k-elf-objdump"
    return os.path.join(HERE, "amix-cpu-scan"), od


def cpu_problems(repo, stem):
    """68040/060-only code in the newest published stem, unless it is tagged
    org.amix.cpu=68060: one line per file (amix-cpu-scan's verdict)."""
    man = _newest(repo, stem)
    if man is None or _cpu_tag(man) == "68060":
        return []
    scan, od = scanner()
    skip = set("/" + s.lstrip("/") for s in _set_values(man, "org.amix.cpu-skip"))
    allow = []
    for a in _set_values(man, "org.amix.cpu-allow"):
        allow += ["-x", a]
    out = []
    with tempfile.TemporaryDirectory() as d:
        n = 0
        for line in man.splitlines():
            t, h, kv = _act(line)
            if t != "file" or "path" not in kv or "/" + kv["path"].lstrip("/") in skip:
                continue
            f = os.path.join(repo, "publisher", PUBLISHER, "file", h[:2], h)
            try:
                with gzip.open(f) as g:
                    data = g.read()
            except OSError:
                continue
            if data[:4] != b"\x7fELF":
                continue
            n += 1
            p = os.path.join(d, "%04d" % n)
            with open(p, "wb") as o:
                o.write(data)
            r = subprocess.run([scan] + allow + [p], capture_output=True, text=True,
                               env=dict(os.environ, OBJDUMP=od))
            if r.returncode != 0:
                out.append("%s: /%s has 68040/060-only instructions (amix-cpu-scan), and it is not "
                           "tagged org.amix.cpu=68060" % (stem, kv["path"].lstrip("/")))
    return out


def _all_newest(repo):
    pk = os.path.join(repo, "publisher", PUBLISHER, "pkg")
    for qs in sorted(os.listdir(pk)) if os.path.isdir(pk) else []:
        stem = urllib.parse.unquote(qs)
        man = _newest(repo, stem)
        if man is not None:
            yield stem, man


def file_owners(repo):
    """path -> [stems] over the newest version of every package: files and
    links only (a directory may be shared)."""
    owners = {}
    for stem, man in _all_newest(repo):
        for line in man.splitlines():
            t, _, kv = _act(line)
            if t in ("file", "link", "hardlink") and "path" in kv:
                owners.setdefault("/" + kv["path"].lstrip("/"), []).append(stem)
    return owners


def path_conflicts(repo, stems=None):
    """A path two packages deliver: AMIX's pkg refuses to install the second
    (2026-11-20: XErrorDB and XKeysymDB in desktop/cde and x11/x11r6.3).
    With stems, only the conflicts that involve one of them."""
    out = []
    for path, who in sorted(file_owners(repo).items()):
        if len(who) > 1 and (stems is None or any(s in who for s in stems)):
            out.append("%s is delivered by %s" % (path, " and ".join(who)))
    return out
