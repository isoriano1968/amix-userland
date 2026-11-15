import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from amix_installer import svr4  # noqa: E402


def archives(d):
    """Parse a datastream: header, then newc archives -> [[(name, mode, data)]]"""
    assert d[:20] == b"# PaCkAgE DaTaStReAm"
    off, out = 512, []
    while off < len(d):
        ents = []
        while True:
            h = d[off:off + 110]
            assert h[:6] == b"070701", (off, h[:6])
            f = [int(h[6 + 8 * i:14 + 8 * i], 16) for i in range(13)]
            mode, size, nsz = f[1], f[6], f[11]
            name = d[off + 110:off + 110 + nsz - 1].decode()
            o = (off + 110 + nsz + 3) & ~3
            data = d[o:o + size]
            off = (o + size + 3) & ~3
            if name == "TRAILER!!!":
                break
            ents.append((name, mode, data))
        off = (off + 511) // 512 * 512
        out.append(ents)
    return out


class Datastream(unittest.TestCase):
    def test_sum_as_svr4(self):
        self.assertEqual(svr4.svr4_sum(b"#!/bin/sh\necho hi\n"), 1370)   # OI's pkgmk: 1370
        self.assertEqual(svr4.svr4_sum(b""), 0)
        big = bytes([255]) * 70000                                        # folds above 16 bits
        s = 255 * 70000
        r = (s & 0xffff) + (s >> 16)
        self.assertEqual(svr4.svr4_sum(big), (r & 0xffff) + (r >> 16))

    def test_layout(self):
        E = svr4.Entry
        ents = [E("bin", "d"), E("bin/hello", "f", 0o555, data=b"#!/bin/sh\necho hi\n"),
                E("bin/hi", "s", target="hello")]
        d = svr4.datastream("AMIXtest", {"NAME": "Test", "VERSION": "1.0",
                                         "BASEDIR": "/opt/amixboot"}, ents,
                            {"postinstall": "#!/bin/sh\nexit 0\n"}, mtime=1000)
        self.assertEqual(len(d) % 512, 0)
        head = d[:512].split(b"\0")[0].decode().splitlines()
        self.assertEqual(head[0], "# PaCkAgE DaTaStReAm")
        self.assertTrue(head[1].startswith("AMIXtest 1 "))
        a1, a2 = archives(d)
        self.assertEqual([n for n, _, _ in a1], ["AMIXtest/pkginfo", "AMIXtest/pkgmap"])
        names = [n for n, _, _ in a2]
        self.assertEqual(names, ["pkginfo", "pkgmap", "install", "install/postinstall", "reloc",
                                 "reloc/bin", "reloc/bin/hello", "reloc/bin/hi"])
        pkgmap = dict((n, data) for n, _, data in a2)["pkgmap"].decode().splitlines()
        self.assertIn("1 f none bin/hello 0555 root bin 18 1370 1000", pkgmap)
        self.assertIn("1 s none bin/hi=hello", pkgmap)
        self.assertTrue(pkgmap[0].startswith(": 1 "))
        mode = dict((n, m) for n, m, _ in a2)
        self.assertEqual(mode["reloc/bin/hello"], 0o100555)
        self.assertEqual(mode["reloc/bin/hi"], 0o120777)
        info = dict((n, data) for n, _, data in a1)["AMIXtest/pkginfo"].decode()
        self.assertTrue(info.startswith("PKG=AMIXtest\nNAME=Test\nARCH=m68k\nVERSION=1.0\n"))


if __name__ == "__main__":
    unittest.main()
