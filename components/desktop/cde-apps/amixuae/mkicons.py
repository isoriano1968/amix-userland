#!/usr/bin/env python3
"""mkicons.py -- the Amiga (amixuae) icon as CDE XPMs (48, 32, 16 pixels):
a 3.5" floppy in front of a small monitor."""

PAL = {'.': 'None', 'f': '#5A6270', 'b': '#3C7DD0', 'd': '#2A5A9C', 'k': '#202020',
       'm': '#C8C8C8', 'w': '#F2F2F2', 's': '#8A93A3'}

def icon(n):
    g = [['.'] * n for _ in range(n)]
    m = max(1, n // 16)
    # the monitor, upper right
    x0, y0, x1, y1 = n // 4, m, n - m - 1, n * 9 // 16
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            g[y][x] = 'f' if y in (y0, y1) or x in (x0, x1) else 'b'
    # the floppy, lower left: dark body, metal shutter, white label
    fx0, fy0, fx1, fy1 = m, n * 3 // 8, n * 5 // 8, n - m - 1
    for y in range(fy0, fy1 + 1):
        for x in range(fx0, fx1 + 1):
            g[y][x] = 'k'
    sw = (fx1 - fx0) // 2
    for y in range(fy0, fy0 + (fy1 - fy0) // 3):
        for x in range(fx0 + sw // 2, fx0 + sw // 2 + sw):
            g[y][x] = 'm'
    for y in range(fy1 - (fy1 - fy0) * 2 // 5, fy1):
        for x in range(fx0 + 2 * m, fx1 - 2 * m + 1):
            g[y][x] = 'w'
    return g

def xpm(name, g):
    used = sorted(set(ch for row in g for ch in row))
    out = ['/* XPM */', 'static char * %s[] = {' % name,
           '"%d %d %d 1",' % (len(g[0]), len(g), len(used))]
    out += ['"%s c %s",' % (ch, PAL[ch]) for ch in used]
    out += ['"%s"%s' % (''.join(row), ',' if i < len(g) - 1 else '};') for i, row in enumerate(g)]
    return '\n'.join(out) + '\n'

if __name__ == '__main__':
    import os
    os.makedirs('icons', exist_ok=True)
    for size, n in (('l', 48), ('m', 32), ('t', 16)):
        open('icons/AmixUAE.%s.pm' % size, 'w').write(xpm('AmixUAE_%s' % size, icon(n)))
