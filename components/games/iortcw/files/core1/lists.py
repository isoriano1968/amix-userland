#!/usr/bin/env python3
"""lists.py SP_DIR -- the game modules' sources for the core 1 build, from
upstream's Makefile (Q3CGOBJ_, Q3GOBJ_, Q3UIOBJ_), as amix/build.py takes
them: make variables CGAME, QAGAME, UI (paths below code/)."""
import os, re, sys

top = sys.argv[1]
code = os.path.join(top, "code")
mk = open(os.path.join(top, "Makefile")).read().replace("\\\n", " ")


def mod(var, extra):
    names = re.findall(r"\$\(B\)/\$\(BASEGAME\)/(\S+)\.o", re.search(r"^\s*%s\s*:?=\s*(.*)$" % var, mk, re.M).group(1))
    out = []
    for n in names + extra:
        rel, base = n.rsplit("/", 1)
        for sub in ([rel] if rel not in ("cgame", "game", "ui") else [rel, "game", "cgame", "ui", "qcommon"]):
            if os.path.exists(os.path.join(code, sub, base + ".c")):
                out.append("%s/%s.c" % (sub, base))
                break
        else:
            sys.exit("no source for " + n)
    return " ".join(out)


print("CGAME := " + mod("Q3CGOBJ_", ["cgame/cg_syscalls"]))
print("QAGAME := " + mod("Q3GOBJ_", ["game/g_syscalls"]))
print("UI := " + mod("Q3UIOBJ_", ["ui/ui_syscalls"]))
