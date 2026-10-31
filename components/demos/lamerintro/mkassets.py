#!/usr/bin/env python3
"""mkassets.py SRC OUT -- from the LamerIntro sources (SRC: the .tga files and
newbaveuse.ttf) the textures for AMIX in OUT, and letters3d.h: each letter of
the font as boxes, its pixels merged into rectangles, for the 3D scroller."""
import os, shutil, sys
from PIL import Image, ImageDraw, ImageFont

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)


def tga(img, name):
    """Uncompressed, bottom row first, like the originals."""
    img = img.convert("RGBA")
    w, h = img.size
    head = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 32, 0])
    rows = img.transpose(Image.FLIP_TOP_BOTTOM).tobytes()
    b = bytearray(rows)
    b[0::4], b[2::4] = rows[2::4], rows[0::4]
    with open(os.path.join(out, name), "wb") as f:
        f.write(head + bytes(b))


for n in "background fonte raster_plain raster star trombine sinelogo".split():
    shutil.copy(os.path.join(src, n + ".tga"), out)
# power of two for the logo, and far smaller
tga(Image.open(os.path.join(src, "sgi.tga")).resize((256, 256), Image.LANCZOS), "sgi.tga")

ttf = os.path.join(src, "newbaveuse.ttf")

# the header: LamerIntro, once, white with its shape in alpha
font = ImageFont.truetype(ttf, 48)
title = Image.new("RGBA", (512, 64), (255, 255, 255, 0))
ImageDraw.Draw(title).text((256, 32), "LamerIntro", font=font, fill=(255, 255, 255, 255), anchor="mm")
tga(title, "title.tga")

# the letters: 12 cells high
CELLS = 20
font = ImageFont.truetype(ttf, 25)
boxes, index = [], []
for c in range(32, 91):
    index.append(len(boxes))
    img = Image.new("L", (40, 30), 0)
    ImageDraw.Draw(img).text((2, 1), chr(c), font=font, fill=255)
    bb = img.point(lambda v: 255 if v > 110 else 0).getbbox()
    if not bb:
        continue
    cells = img.crop(bb).point(lambda v: 1 if v > 110 else 0)
    w, h = cells.size
    px = cells.load()
    used = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if not px[x, y] or used[y][x]:
                continue
            x1 = x
            while x1 + 1 < w and px[x1 + 1, y] and not used[y][x1 + 1]:
                x1 += 1
            y1 = y
            while y1 + 1 < h and all(px[i, y1 + 1] and not used[y1 + 1][i] for i in range(x, x1 + 1)):
                y1 += 1
            for j in range(y, y1 + 1):
                for i in range(x, x1 + 1):
                    used[j][i] = True
            # centred on the letter, y up
            boxes.append((x - w // 2, h // 2 - y1 - 1, x1 + 1 - w // 2, h // 2 - y))
index.append(len(boxes))
with open(os.path.join(out, "letters3d.h"), "w") as f:
    f.write("/* letters3d.h -- made by mkassets.py: the font as boxes x0, y0, x1, y1, in cells */\n")
    f.write("#define LETTER_CELLS %d\n" % CELLS)
    f.write("static const unsigned short letter_first[] = {" + ",".join(map(str, index)) + "};\n")
    f.write("static const signed char letter_box[][4] = {\n")
    for b in boxes:
        f.write("{%d,%d,%d,%d},\n" % b)
    f.write("};\n")
print(len(boxes), "boxes,", len(boxes) / 59, "a letter")
