#!/usr/bin/env python3
"""mkicons.py -- the Z3660 front panel icon as CDE XPMs (48, 32, 16 pixels):
a chip with gold pins, a white "Z", and two cores below, core 1 lit green."""
import sys

PAL = {'.': 'None', 'p': '#D4A72C', 'b': '#2B3A55', 'e': '#4A5F86', 'w': '#F2F2F2',
       'g': '#39D353', 'c': '#7A8AA8', 'k': '#141B29'}

# a 7x7 "Z", scaled
Z = ["#######", "     ##", "    ## ", "   ##  ", "  ##   ", " ##    ", "#######"]

def icon(n):
    g = [['.'] * n for _ in range(n)]
    m = max(2, n // 8)                  # pin length = margin around the body
    lo, hi = m, n - m                   # body
    step = max(3, n // 8)
    for i in range(lo + 1, hi - 1, step):   # pins on all four sides
        for t in range(m):
            for w in range(max(1, step // 2)):
                if i + w < hi - 1:
                    g[t][i + w] = g[n - 1 - t][i + w] = 'p'
                    g[i + w][t] = g[i + w][n - 1 - t] = 'p'
    for y in range(lo, hi):
        for x in range(lo, hi):
            edge = y in (lo, hi - 1) or x in (lo, hi - 1)
            g[y][x] = 'k' if edge else ('e' if y < lo + 2 and n >= 32 else 'b')
    body = hi - lo
    s = max(1, (body * 55 // 100) // 7)     # the Z's scale
    zx = lo + (body - 7 * s) // 2
    zy = lo + max(1, body // 10)
    for r, row in enumerate(Z):
        for c, ch in enumerate(row):
            if ch == '#':
                for dy in range(s):
                    for dx in range(s):
                        g[zy + r * s + dy][zx + c * s + dx] = 'w'
    cs = max(2, body // 6)                   # the two cores
    cy = hi - 1 - max(1, body // 12) - cs
    gap = max(1, cs // 2)
    cx0 = lo + (body - 2 * cs - gap) // 2
    for k, colour in enumerate(('c', 'g')):
        for y in range(cy, cy + cs):
            for x in range(cx0 + k * (cs + gap), cx0 + k * (cs + gap) + cs):
                g[y][x] = colour
    return g

def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'

for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
    open('icons/Z3660.%s.pm' % suffix, 'w').write(xpm('Z3660_%s_pm' % suffix, icon(size)))
