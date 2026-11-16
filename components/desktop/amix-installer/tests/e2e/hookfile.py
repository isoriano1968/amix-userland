#!/usr/bin/env python3
"""hookfile.py IMAGE PATH NEWFILE -- put NEWFILE's bytes into the existing
regular file PATH of IMAGE's AMIX root, in place: the same size (NEWFILE is
padded with a '#' line), no inode, directory or other file touched.  For the
end-to-end test's boot hook on a DISPOSABLE disk copy only: it refuses an
image outside $AMIX_E2E_DIR, a file over one block, and reads it back.
(After the scratch tool inplace.py of the FS-UAE Picasso II test.)"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
from amix_installer import hdf  # noqa: E402

IFMT, IFREG = 0o170000, 0o100000


def main():
    img_path, path, new = sys.argv[1:4]
    top = os.environ.get("AMIX_E2E_DIR")
    if not top or not os.path.realpath(img_path).startswith(os.path.realpath(top) + os.sep):
        sys.exit("hookfile: %s is not inside $AMIX_E2E_DIR: disposable copies only" % img_path)
    with hdf.Image(img_path) as img:
        img.read_rdb()
        root = [p for p in img.partitions if p.kind == "amix-root"][0]
        fs = hdf.Ufs(img, root)
        ino = fs.lookup(path)
        if ino is None or ino.mode & IFMT != IFREG:
            sys.exit("hookfile: %s is not a regular file in the image" % path)
        if ino.size > fs.bsize:
            sys.exit("hookfile: %s is over one block" % path)
        with open(new, "rb") as f:
            data = f.read()
        pad = ino.size - len(data)
        if pad < 2:
            sys.exit("hookfile: %s has %d bytes, the hook %d" % (path, ino.size, len(data)))
        data += b"#" * (pad - 1) + b"\n"
        off = fs.base + fs.bmap(ino, 0) * fs.fsize
    fd = os.open(img_path, os.O_WRONLY)
    try:
        os.pwrite(fd, data, off)
    finally:
        os.close(fd)
    with hdf.Image(img_path) as img:
        img.read_rdb()
        fs = hdf.Ufs(img, [p for p in img.partitions if p.kind == "amix-root"][0])
        if fs.read_file(path) != data:
            sys.exit("hookfile: %s does not read back" % path)
    print("hookfile: %s, %d bytes" % (path, len(data)))


if __name__ == "__main__":
    main()
