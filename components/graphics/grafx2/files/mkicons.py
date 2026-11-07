#!/usr/bin/env python3
"""mkicons.py SRC NAME DIR -- CDE icons NAME.{l,m,t}.pm (48, 32, 16 pixels) as XPM,
scaled from the program's own icon."""
import sys
from PIL import Image

CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%&*+@"


def icon(src, n):
    im = Image.open(src).convert("RGBA").resize((n, n), Image.LANCZOS)
    alpha = im.getchannel("A")
    pal = im.convert("RGB").quantize(colors=48)
    rgb = pal.getpalette()
    rows, used = [], {}
    for y in range(n):
        row = ""
        for x in range(n):
            if alpha.getpixel((x, y)) < 128:
                row += " "
                used[" "] = "None"
                continue
            i = pal.getpixel((x, y))
            c = CHARS[i]
            used[c] = "#%02x%02x%02x" % tuple(rgb[3 * i:3 * i + 3])
            row += c
        rows.append(row)
    return rows, used


def write(path, name, rows, used):
    with open(path, "w") as f:
        f.write('/* XPM */\nstatic char * %s[] = {\n"%d %d %d 1",\n' % (name, len(rows[0]), len(rows), len(used)))
        for c in sorted(used):
            f.write('"%s c %s",\n' % (c, used[c]))
        f.write(",\n".join('"%s"' % r for r in rows) + "};\n")


for n, s in ((48, "l"), (32, "m"), (16, "t")):
    rows, used = icon(sys.argv[1], n)
    write("%s/%s.%s.pm" % (sys.argv[3], sys.argv[2], s), sys.argv[2] + "_" + s, rows, used)
