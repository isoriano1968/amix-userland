import hashlib
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from amix_installer import hdf, model  # noqa: E402

# the user's own images, read-only; skipped where they are not
HDFDIR = os.path.expanduser("~/Documents/FS-UAE/Hard Drives")
STOCK = os.environ.get("AMIX_TEST_STOCK_HDF", os.path.join(HDFDIR, "AMIGA_UNIX.hdf"))
Z3660 = os.environ.get("AMIX_TEST_Z3660_HDF",
                       os.path.join(HDFDIR, "hd0Z3660AMIX_AMIGA3000_26102026.hdf"))


def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def summed(b, longs):
    b = bytearray(b)
    struct.pack_into(">I", b, 4, longs)
    struct.pack_into(">I", b, 8, 0)
    s = sum(struct.unpack_from(">%dI" % longs, b, 0)) & 0xffffffff
    struct.pack_into(">I", b, 8, (-s) & 0xffffffff)
    return bytes(b)


def make_disk(path, parts, heads=1, secs=32, cyls=64):
    """A small RDB disk: parts = [(name, dostype, lowcyl, highcyl, first_block_bytes)]."""
    cb = heads * secs
    img = bytearray(cyls * cb * 512)
    rdsk = bytearray(512)
    rdsk[:4] = b"RDSK"
    struct.pack_into(">I", rdsk, 28, 1)                 # PartitionList
    struct.pack_into(">III", rdsk, 64, cyls, secs, heads)
    struct.pack_into(">I", rdsk, 144, cb)
    img[0:512] = summed(rdsk, 64)
    for i, (name, dt, lo, hi, first) in enumerate(parts):
        blk = 1 + i
        p = bytearray(512)
        p[:4] = b"PART"
        struct.pack_into(">I", p, 16, blk + 1 if i + 1 < len(parts) else 0xffffffff)
        p[36] = len(name)
        p[37:37 + len(name)] = name.encode()
        struct.pack_into(">II", p, 164, lo, hi)
        struct.pack_into(">I", p, 192, dt)
        img[blk * 512:blk * 512 + 512] = summed(p, 64)
        if first:
            off = lo * cb * 512
            img[off:off + len(first)] = first
    with open(path, "wb") as f:
        f.write(img)


class Rdb(unittest.TestCase):
    def test_partitions_and_pvmk(self):
        boot = bytearray(1024)
        struct.pack_into(">I", boot, 0, hdf.DOS1)
        struct.pack_into(">I", boot, hdf.PVBB_OFFSET, hdf.PVBB_MAGIC)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.hdf")
            make_disk(p, [("PVMK", 0x444f5301, 2, 10, bytes(boot)),
                          ("UNIX_Root", 0x554e4901, 11, 50, None),
                          ("UNIX_Swap", 0x72657376, 51, 60, None),
                          ("Work", 0x444f5303, 61, 63, b"DOS\x03")])
            before = sha(p)
            with hdf.Image(p) as im:
                parts = im.read_rdb()
            self.assertEqual([x.kind for x in parts], ["pvmk", "amix-root", "reserved", "amigados"])
            self.assertEqual(parts[0].start, 64)
            self.assertEqual(parts[1].blocks, 40 * 32)
            # no UFS on the synthetic root: refused cleanly, nothing written
            with self.assertRaises(hdf.UfsError):
                hdf.inspect(p)
            self.assertEqual(before, sha(p))

    def test_not_an_image(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.hdf")
            with open(p, "wb") as f:
                f.write(b"\0" * 65536)
            with self.assertRaises(hdf.NotAnImage):
                hdf.inspect(p)

    def test_bad_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.hdf")
            make_disk(p, [("UNIX_Root", 0x554e4901, 2, 50, None)])
            with open(p, "r+b") as f:
                f.seek(12)
                f.write(b"\1")
            with self.assertRaises(hdf.NotAnImage):
                hdf.inspect(p)


@unittest.skipUnless(os.path.exists(STOCK), "no stock AMIX 2.1c HDF here")
class StockImage(unittest.TestCase):
    def test_stock_core(self):
        st = os.stat(STOCK)
        info = hdf.inspect(STOCK, probes=model.all_probes())
        self.assertEqual(os.stat(STOCK).st_mtime, st.st_mtime)
        self.assertTrue(info.has_core())
        self.assertEqual(info.root.name, "UNIX_Root")
        self.assertIn(("unix", 1475144), info.kernels)
        self.assertIsNone(info.pvmk)
        cfg = model.Config(source=info)
        self.assertFalse(cfg.installed(model.find_group("pkgclient").option("pkg")))
        disks, fits = cfg.space()
        self.assertAlmostEqual(disks[0]["total"], 459, delta=20)   # 500 MB partition, minfree off


@unittest.skipUnless(os.path.exists(Z3660), "no Z3660 AMIX HDF here")
class Z3660Image(unittest.TestCase):
    def test_du_matches_superblock(self):
        with hdf.Image(Z3660) as im:
            im.read_rdb()
            fs = hdf.Ufs(im, im.partitions[0])
            # fsck-level slack (a few fragments) aside, every used block belongs to a file
            self.assertAlmostEqual(fs.du("/"), fs.used_bytes(), delta=1 << 20)

    def test_installed(self):
        info = hdf.inspect(Z3660, probes=model.all_probes())
        cfg = model.Config(source=info)
        self.assertTrue(cfg.installed(model.find_group("desk").option("cde")))
        self.assertTrue(cfg.installed(model.find_group("tools").option("vim")))
        self.assertGreater(info.movable["/usr/local"], 50 << 20)


if __name__ == "__main__":
    unittest.main()
