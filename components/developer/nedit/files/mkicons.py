#!/usr/bin/env python3
"""mkicons.py DIR -- NEdit's CDE icons (48, 32, 16 pixels) as XPM: a page of text
with a pencil."""
import sys

COL = {" ": "None", ".": "#ffffff", "k": "#000000", "g": "#7f8fa6", "b": "#3060c0", "y": "#e8c040", "r": "#d04030"}


def icon(n):
    p = [[" "] * n for _ in range(n)]
    m = max(1, n // 12)
    w = n * 3 // 4
    for y in range(m, n - m):
        for x in range(m, w):
            p[y][x] = "k" if y in (m, n - m - 1) or x in (m, w - 1) else "."
    for i, y in enumerate(range(2 * m + 1, n - 2 * m - 1, max(2, n // 8))):
        c = "b" if i % 3 == 0 else "g"
        for x in range(2 * m + 1, w - 2 * m - 1 - (i % 2) * (n // 8)):
            p[y][x] = c
    t = max(1, n // 16)
    for i in range(n // 2):
        x, y = n - 2 - n // 8 - i, n // 8 + 1 + i
        for d in range(-t, t + 1):
            if 0 <= x + d < n and 0 <= y < n:
                p[y][x + d] = "r" if i < n // 10 else "y"
        if i >= n // 2 - 2:
            p[y][x] = "k"
    return ["".join(r) for r in p]


def write(path, name, rows):
    used = sorted({c for r in rows for c in r})
    with open(path, "w") as f:
        f.write('/* XPM */\nstatic char * %s[] = {\n"%d %d %d 1",\n' % (name, len(rows[0]), len(rows), len(used)))
        for c in used:
            f.write('"%s c %s",\n' % (c, COL[c]))
        f.write(",\n".join('"%s"' % r for r in rows) + "};\n")


for n, s in ((48, "l"), (32, "m"), (16, "t")):
    write("%s/NEdit.%s.pm" % (sys.argv[1], s), "NEdit_" + s, icon(n))
