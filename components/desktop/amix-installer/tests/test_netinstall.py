import os
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)

from amix_installer import model, netinstall  # noqa: E402
from test_svr4 import archives  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BOOT_STAGES = ["sysutils/amix-pkg", "library/zlib"]


def tftp_get(port, name, host="127.0.0.1"):
    """A TFTP client as AMIX's: RRQ, then ACK every block; checks the sender port."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(3)
    s.sendto(struct.pack("!H", 1) + name.encode() + b"\0octet\0", (host, port))
    out, want = b"", 1
    while True:
        pkt, addr = s.recvfrom(1024)
        assert addr[1] == port, "reply from port %d, not %d" % (addr[1], port)
        op, blk = struct.unpack("!HH", pkt[:4])
        if op == 5:
            s.close()
            raise FileNotFoundError(pkt[4:-1].decode())
        assert op == 3 and blk == want & 0xffff
        out += pkt[4:]
        s.sendto(struct.pack("!HH", 4, blk), addr)
        if len(pkt) - 4 < 512:
            s.close()
            return out
        want += 1


class Servers(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = os.path.join(self.tmp.name, "repo")
        os.makedirs(os.path.join(self.repo, "publisher/amix/catalog"))
        with open(os.path.join(self.repo, "publisher/amix/catalog/catalog.attrs"), "w") as f:
            f.write('{"version":1}\n')
        self.log = []
        self.s = netinstall.Session(model.Config(), self.repo, host="127.0.0.1",
                                    log=self.log.append, tftp_port=0, http_port=0,
                                    bind="127.0.0.1")
        self.s._boot = bytes(range(256)) * 9                # 2304 bytes: 5 blocks
        self.s.start()

    def tearDown(self):
        self.s.stop()
        self.tmp.cleanup()

    def url(self, p):
        return "http://127.0.0.1:%d%s" % (self.s.http_port, p)

    def test_tftp(self):
        go = tftp_get(self.s.tftp.port, "go").decode()
        self.assertIn("H=127.0.0.1\n", go)
        self.assertIn("/usr/bin/tftp $H %d <<EOF" % self.s.tftp.port, go)
        self.assertIn("http://127.0.0.1:%d" % self.s.http_port, go)
        self.assertEqual(tftp_get(self.s.tftp.port, "amixboot.pkg"), self.s._boot)
        self.assertEqual(tftp_get(self.s.tftp.port, "/tmp/../amixboot.pkg"), self.s._boot)
        with self.assertRaises(FileNotFoundError):
            tftp_get(self.s.tftp.port, "etc/passwd")

    def test_http(self):
        self.assertIn(b"step 2 \"the package client\"", urllib.request.urlopen(self.url("/steps")).read())
        self.assertEqual(urllib.request.urlopen(
            self.url("/repo/publisher/amix/catalog/catalog.attrs")).read(), b'{"version":1}\n')
        for bad in ("/repo/../../etc/passwd", "/repo/%2e%2e/%2e%2e/etc/passwd", "/nothing"):
            with self.assertRaises(urllib.error.HTTPError):
                urllib.request.urlopen(self.url(bad))
        for text in ("step 1 the system", "DONE"):
            r = urllib.request.Request(self.url("/report/msg"), data=text.encode(), method="PUT")
            self.assertEqual(urllib.request.urlopen(r).read(), b"ok\n")
        self.assertTrue(self.s.done)
        self.assertFalse(self.s.failed)
        self.assertEqual([m[2] for m in self.s.messages], ["step 1 the system", "DONE"])
        with self.assertRaises(urllib.error.HTTPError):
            urllib.request.urlopen(urllib.request.Request(self.url("/repo/x"), data=b"x",
                                                          method="PUT"))

    def test_commands(self):
        self.assertEqual(self.s.commands(), ["tftp 127.0.0.1 %d" % self.s.tftp.port, "binary",
                                             "get go", "quit", "sh go"])


class Scripts(unittest.TestCase):
    def sh_ok(self, text):
        with tempfile.NamedTemporaryFile("w", suffix=".sh", delete=False) as f:
            f.write(text)
        try:
            r = subprocess.run(["sh", "-n", f.name], stderr=subprocess.PIPE)
            self.assertEqual(r.returncode, 0, r.stderr)
        finally:
            os.unlink(f.name)

    def test_go(self):
        t = netinstall.go_script("10.0.2.2")
        self.sh_ok(t)
        self.assertIn("/usr/sbin/pkgadd -n -a /tmp/amixboot.admin -d /tmp/amixboot.pkg AMIXboot", t)
        self.assertNotIn("$(", t)                       # Bourne shell: backticks only

    def test_steps(self):
        cfg = model.Config(sel={"mach": "a3000", "accel": "z3660", "desk": "cde"},
                           on={"vim": True, "python": True, "gpu": True})
        t = netinstall.steps_script(cfg, "10.0.2.2", data_disk=("/dev/rdsk/c1d0s1", 4093952))
        self.sh_ok(t)
        self.assertNotIn("$(", t)
        self.assertIn("mkfs -F ufs $D 4093952", t)
        self.assertIn("M=%d" % model.SPLIT_RESERVE_MB, t)
        self.assertIn("$PKG --min-free $M install", t)
        self.assertIn("editor/vim", t)
        self.assertIn("desktop/cde", t)
        self.assertIn("not packaged yet", t)            # the ARM core services
        self.assertIn("kernel is not packaged yet", t)
        self.assertLess(t.index("sysutils/amix-pkg"), t.index("step 4"))
        self.assertIn("pkgrm -n -a /tmp/amixboot.admin AMIXboot", t)
        self.assertNotIn("step 3", netinstall.steps_script(cfg, "10.0.2.2"))

    def test_admin_empty_values_last(self):
        # AMIX's pkginstall stops reading the admin file at an empty value
        lines = netinstall.ADMIN.strip().splitlines()
        empty = [i for i, l in enumerate(lines) if l.endswith("=")]
        self.assertTrue(all(i >= len(lines) - len(empty) for i in empty))
        self.assertIn("basedir=%s" % netinstall.BOOT_DIR, lines)

    def test_steps_only_what_the_repo_has(self):
        cfg = model.Config(sel={"desk": "cde"}, on={"vim": True})
        with tempfile.TemporaryDirectory() as repo:
            t = netinstall.steps_script(cfg, "10.0.2.2", repo=repo)
        self.sh_ok(t)
        self.assertNotIn("$PKG install", t)
        self.assertIn("editor/vim (not in the repository)", t)

    def test_installed_not_again(self):
        cfg = model.Config(on={"vim": True})
        self.assertIn("editor/vim", netinstall.packages_for(cfg)[0])


@unittest.skipUnless(all(os.path.isdir(os.path.join(ROOT, "components", c, "build", "stage"))
                         for c in BOOT_STAGES), "bootstrap stages not built")
class Bootstrap(unittest.TestCase):
    def test_from_published_repository(self):
        with tempfile.TemporaryDirectory() as d:
            repo = os.path.join(d, "repo")
            for c in BOOT_STAGES:
                comp = os.path.join(ROOT, "components", c)
                p5m = os.path.join(d, c.replace("/", "_") + ".p5m")
                subprocess.run([os.path.join(ROOT, "tools", "amix-p5m"), comp, "-o", p5m],
                               check=True, stderr=subprocess.DEVNULL)
                subprocess.run([os.path.join(ROOT, "tools", "amix-publish"), "-s", repo,
                                "-d", os.path.join(comp, "build", "stage"), "-d", comp, p5m],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            ds = netinstall.bootstrap(repo)
        a1, a2 = archives(ds)
        names = [n for n, _, _ in a2]
        for n in ("reloc/bin/pkg", "reloc/lib/libz.so.1"):
            self.assertIn(n, names)
        self.assertFalse([n for n in names if "curl" in n or "ssl" in n or "crypto" in n])
        files = dict((n, data) for n, _, data in a2)
        self.assertEqual(files["reloc/bin/pkg"][:4], b"\x7fELF")
        with open(os.path.join(ROOT, "components/sysutils/amix-pkg/build/stage/usr/bin/pkg"), "rb") as f:
            self.assertEqual(files["reloc/bin/pkg"], f.read())
        self.assertIn(b"BASEDIR=/opt/amixboot", files["pkginfo"])


if __name__ == "__main__":
    unittest.main()
