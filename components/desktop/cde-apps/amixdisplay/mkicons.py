#!/usr/bin/env python3
"""mkicons.py -- the Display icon as CDE XPMs (48, 32, 16 pixels): a screen
with a blue desktop, a smaller frame drawn in it and arrows to its corners
(a resolution change), on a stand."""

PAL = {'.': 'None', 'f': '#5A6270', 'b': '#3C7DD0', 'd': '#2A5A9C', 'w': '#F2F2F2',
       'y': '#E8C33D', 's': '#8A93A3'}

def icon(n):
    g = [['.'] * n for _ in range(n)]
    m = max(1, n // 16)
    top, bot = m, n * 3 // 4                    # the screen
    for y in range(top, bot):
        for x in range(m, n - m):
            edge = y in (top, bot - 1) or x in (m, n - m - 1)
            g[y][x] = 'f' if edge else ('b' if y < (top + bot) // 2 + 2 else 'd')
    # the smaller frame, white
    x0, x1 = m + (n - 2 * m) * 3 // 10, n - m - 1 - (n - 2 * m) * 3 // 10
    y0, y1 = top + (bot - top) * 3 // 10, bot - 1 - (bot - top) * 3 // 10
    for x in range(x0, x1 + 1):
        g[y0][x] = g[y1][x] = 'w'
    for y in range(y0, y1 + 1):
        g[y][x0] = g[y][x1] = 'w'
    # diagonal arrows from the frame's corners to the screen's, yellow
    if n >= 32:
        for (cx, cy, dx, dy) in ((x0, y0, -1, -1), (x1, y0, 1, -1), (x0, y1, -1, 1), (x1, y1, 1, 1)):
            x, y = cx + dx, cy + dy
            while m + 1 < x < n - m - 2 and top + 1 < y < bot - 2:
                g[y][x] = 'y'
                x += dx
                y += dy
            hx, hy = x - dx, y - dy           # arrow head
            g[hy][hx - dx] = g[hy - dy][hx] = 'y'
    w = max(2, n // 6)                          # the stand
    for y in range(bot, min(n, bot + max(1, n // 10))):
        for x in range(n // 2 - w // 2, n // 2 + w // 2 + 1):
            g[y][x] = 's'
    for y in range(min(n - 1, bot + max(1, n // 10)), n - m):
        for x in range(n // 4, n - n // 4):
            g[y][x] = 's'
    return g

def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'

for size, suffix in ((48, 'l'), (32, 'm'), (16, 't')):
    open('icons/Display.%s.pm' % suffix, 'w').write(xpm('Display_%s_pm' % suffix, icon(size)))
