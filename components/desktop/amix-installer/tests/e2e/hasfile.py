#!/usr/bin/env python3
"""hasfile.py IMAGE PATH... -- every PATH is in IMAGE's AMIX root (read-only,
through the installer's own UFS reader); prints what is missing."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from amix_installer import hdf  # noqa: E402

with hdf.Image(sys.argv[1]) as img:
    img.read_rdb()
    root = [p for p in img.partitions if p.kind == "amix-root"][0]
    fs = hdf.Ufs(img, root)
    gone = [p for p in sys.argv[2:] if not fs.exists(p)]
for p in sys.argv[2:]:
    print("hasfile: %s %s" % (p, "MISSING" if p in gone else "present"))
sys.exit(1 if gone else 0)
