#!/usr/bin/env python3
"""trimpaks.py SRC_DIR OUT.pk3 [map ...] -- the retail game's paks reduced for a
small disk: one pk3 with the maps named (and what is not map data), without
videos, without the speech of other maps and with one music track in eight.
Later paks replace files of earlier ones, as in the game."""
import os, re, sys, zipfile

src, out, maps = sys.argv[1], sys.argv[2], set(sys.argv[3:]) or {"escape1", "escape2", "tram"}
files = {}
for pk in sorted(os.listdir(src)):              # pak0, sp_pak1 ...: the last wins
    if pk.endswith(".pk3"):
        z = zipfile.ZipFile(os.path.join(src, pk))
        for i in z.infolist():
            if not i.is_dir():
                files[i.filename.lower()] = (z, i)


def keep(n):
    p = n.split("/")
    if p[0] == "video":
        return False
    if p[0] == "maps" and len(p) > 1:
        return re.sub(r"_b[0-9]$", "", p[1].split(".")[0]) in maps or not p[1].endswith((".bsp", ".aas", ".rcd", ".mdx", ".lmp"))
    if p[0] == "maps" or p[0] == "levelshots":
        return any(m in n for m in maps)
    if n.startswith("sound/scenaric/"):
        return len(p) > 3 and p[2] in maps
    if n.startswith("sound/music/"):
        return sum(ord(c) for c in n) % 8 == 0
    return True


w = zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9)
total = 0
for n in sorted(files):
    if keep(n):
        z, i = files[n]
        w.writestr(n, z.read(i))
        total += i.file_size
w.close()
print(out, os.path.getsize(out) >> 20, "MB,", len(w.namelist()), "files,", total >> 20, "MB unpacked")
