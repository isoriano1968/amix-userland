"""Copy a directory tree out of an AMIX UFS image (read-only) with pvmk's ufstool."""
import os, subprocess, sys
T = os.path.expanduser("~/Sources/usr-amix/pvmk/build/host/ufstool")
img, start, src, dst = sys.argv[1:5]
def walk(s, d):
    os.makedirs(d, exist_ok=True)
    out = subprocess.run([T, img, start, "ls", s], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        mode, size, name = line.split(None, 2)
        kind = name[-1] if name[-1] in "/@" else ""
        name = name.rstrip("/@")
        if name in (".", ".."): continue
        if kind == "/": walk(s + "/" + name, os.path.join(d, name))
        else:			# a symlink is copied as the file it points to
            subprocess.run([T, img, start, "get", s + "/" + name, os.path.join(d, name)], check=True)
walk(src, dst)
