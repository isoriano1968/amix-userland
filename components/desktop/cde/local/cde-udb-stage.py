#!/usr/bin/env python3
"""cde-udb-stage.py -- assemble an installable CDE tree for AMIX from the
build tree, following CDE's own install databases (databases/*.udb).

CDE installs through admin/IntegTools/dbTools/installCDE, a ksh script that
also stops running desktop processes on the machine it runs on; there is no
ksh on the build host, and the install target is a different machine anyway.
This reads the same .udb records and copies the files into OUT/ the way
installCDE would lay them out:

    <source path relative to the build top>
    { default            (or: linux, sun, hpux, ... -- one block per platform)
        install_target = /usr/dt/bin/dtexec
        mode = 0555
        type = file | sym_link | directory
    }

Per record the "linux" block is used if there is one, else "default"
(linux selects the X11-style font aliases; nothing in it is Linux-only for
the filesets staged here).  #ifdef blocks in the .udb are treated as false.
m68k executables are stripped.  OUT/MISSING lists sources that were not built.

Usage: cde-udb-stage.py BUILDTOP OUT FILESET...
"""
import os
import re
import shutil
import stat
import subprocess
import sys

top, out = sys.argv[1], sys.argv[2]
filesets = sys.argv[3:]
STRIP = os.path.expanduser("~/opt/m68k-amix/bin/m68k-elf-strip")
PREFER = ("linux", "default")


def records(path):
    """yield (source, {platform: {key: value}}, defaults) from one .udb"""
    lines, skip = [], []
    for raw in open(path, encoding="latin-1"):
        s = raw.strip()
        if s.startswith("#ifdef") or s.startswith("#if "):
            skip.append(True); continue
        if s.startswith("#ifndef"):
            skip.append(False); continue
        if s.startswith("#else"):
            skip[-1] = not skip[-1]; continue
        if s.startswith("#endif"):
            skip.pop(); continue
        if any(skip) or not s or s.startswith("#"):
            continue
        lines.append(s)
    defaults, src, blocks, i = {}, None, {}, 0
    while i < len(lines):
        s = lines[i]
        if s.startswith("{"):
            head = s[1:].strip()
            body = {}
            i += 1
            while i < len(lines) and not lines[i].startswith("}"):
                m = re.match(r"(\w+)\s*=\s*(.*)", lines[i])
                if m:
                    body[m.group(1)] = m.group(2).strip()
                i += 1
            if ": defaults" in head:
                defaults[head.split(":")[0].strip()] = body
            elif src is not None:
                for plat in head.split():
                    blocks[plat] = body
        else:
            if src is not None:
                yield src, blocks, defaults
            src, blocks = s, {}
        i += 1
    if src is not None:
        yield src, blocks, defaults


def is_m68k_exec(p):
    with open(p, "rb") as f:
        h = f.read(20)
    return h[:4] == b"\x7fELF" and h[18:20] == b"\x00\x04" and h[16:18] == b"\x00\x02"


fixed = []
missing, n = [], {"file": 0, "sym_link": 0, "directory": 0}
for fs in filesets:
    udb = os.path.join(top, "databases", fs + ".udb")
    for src, blocks, defaults in records(udb):
        plat = next((p for p in PREFER if p in blocks), None)
        if plat is None:
            continue
        rec = dict(defaults.get("default", {}))
        rec.update(blocks[plat])
        tgt = rec.get("install_target", "").strip()
        if not tgt:
            continue
        kind = rec.get("type", "file")
        dst = os.path.join(out, tgt.lstrip("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if kind == "directory":
            os.makedirs(dst, exist_ok=True)
        elif kind == "sym_link":
            if os.path.lexists(dst):
                os.remove(dst)
            os.symlink(src, dst)
        else:
            sp = os.path.join(top, src)
            if not os.path.isfile(sp):
                missing.append(f"{fs}\t{src}\t{tgt}")
                continue
            if os.path.lexists(dst):
                os.remove(dst)
            if is_m68k_exec(sp):
                subprocess.run([STRIP, "-o", dst, sp], check=True)
            else:
                data = open(sp, "rb").read()
                # scripts made by cpp (CppScriptTarget) start with the blank
                # lines cpp left behind; "#!" only works on line 1
                body = data.lstrip(b"\n")
                if body.startswith(b"#!") and body != data:
                    data = body
                    fixed.append(tgt)
                # CDE's scripts name the X11R6.3 build tree's bin (X11ProjectRoot);
                # AMIX's X clients (xset, xrdb) are in /usr/X/bin
                data = data.replace(b"/usr/x11r6/xc/exports/bin", b"/usr/X/bin")
                open(dst, "wb").write(data)
            mode = rec.get("mode", "0444")
            os.chmod(dst, int(mode, 8) & 0o7777 if re.fullmatch(r"0?[0-7]+", mode) else 0o444)
        n[kind] = n.get(kind, 0) + 1

open(os.path.join(out, "MISSING"), "w").write("\n".join(missing) + "\n")
print(f"staged {n} into {out}; {len(fixed)} scripts with \"#!\" moved to line 1; "
      f"{len(missing)} sources missing (see {out}/MISSING)")
