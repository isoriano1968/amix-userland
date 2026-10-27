#!/usr/bin/env python3
"""mklogo.py SRC OUT.pm [SIZE] -- the AMIX dtlogin logo: a square crop around the
logo, scaled to SIZE (default 239, the size of CDE's own Dtlogo.pm) and
reduced to a small palette, written as XPM for Dtlogin*logo*bitmapFile."""
import sys
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
size = int(sys.argv[3]) if len(sys.argv) > 3 else 239
im = Image.open(src).convert('RGB')
w, h = im.size
s = int(min(w, h) * 0.955)                       # trim the outer border a little
im = im.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))
im = im.resize((size, size), Image.LANCZOS)
q = im.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
pal = q.getpalette()[:3 * 32]
used = sorted(set(q.getdata()))
chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
code = {c: chars[i] for i, c in enumerate(used)}
with open(out, 'w') as f:
    f.write('/* XPM */\nstatic char * AmixLogo_pm[] = {\n')
    f.write('"%d %d %d 1",\n' % (size, size, len(used)))
    for c in used:
        r, g, b = pal[3 * c:3 * c + 3]
        f.write('"%s\tc #%02X%02X%02X",\n' % (code[c], r, g, b))
    px = list(q.getdata())
    for y in range(size):
        row = ''.join(code[p] for p in px[y * size:(y + 1) * size])
        f.write('"%s"%s\n' % (row, ',' if y < size - 1 else '};'))
print(out, size, 'x', size, len(used), 'colours')
