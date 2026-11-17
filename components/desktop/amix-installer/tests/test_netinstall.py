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
        self.assertIn(b"step 3 \"the package client\"", urllib.request.urlopen(self.url("/steps")).read())
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
        # every place packages install to is on the data disk, there or not yet
        self.assertIn("for d in usr/local usr/gnu opt usr/x11r6 usr/motif usr/dt; do", t)
        self.assertIn("/usr/bin/ln -s /amixdata/$n /$d", t)
        self.assertIn("M=%d" % model.RESERVE_MB, t)
        self.assertIn("$PKG --min-free $M install", t)
        self.assertIn("editor/vim", t)
        self.assertIn("desktop/cde", t)
        self.assertIn("not packaged yet", t)            # the ARM core services
        self.assertIn("no kernel with Z3660 optimised was made: the stock kernel stays", t)
        self.assertLess(t.index("sysutils/amix-pkg"), t.index("step 4"))
        self.assertIn("pkgrm -n -a /tmp/amixboot.admin AMIXboot >>$LOG 2>&1 || unboot", t)
        self.assertLess(t.index("unboot() {"), t.index("|| unboot"))
        # reports outlive the bootstrap: the installed pkg from step 3 on
        self.assertLess(t.index("RPKG=$PKG"), t.index("step 4"))
        # a FAILED from the installed client still gets out: the bootstrap's pkg
        # reports with its own lib/ while LD_LIBRARY_PATH is cleared (2026-11-17)
        self.assertIn("RPKG=$B/bin/pkg; RLD=$B/lib", t)
        self.assertIn("echo \"$*\" | LD_LIBRARY_PATH=$RLD $RPKG http put", t)
        self.assertIn("tail -20 $LOG | LD_LIBRARY_PATH=$RLD $RPKG http put", t)
        self.assertLess(t.index("LD_LIBRARY_PATH=; export"), t.index("RPKG=$PKG; RLD="))
        self.assertLess(t.index("fail 'the installed client'"), t.index("RPKG=$PKG; RLD="))
        self.assertNotIn("the data disk", netinstall.steps_script(cfg, "10.0.2.2"))

    def test_unboot(self):
        """pkgrm stops at a contents line it cannot parse: only AMIXboot's
        lines go, the rest stays as it was, the old file is kept."""
        d = tempfile.mkdtemp()
        try:
            boot, sadm = d + "/opt/amixboot", d + "/sadm/AMIXboot"
            os.makedirs(boot + "/bin")
            os.makedirs(sadm)
            others = ("/usr/x11r5/fonts/server/MacFS/TrueType Fonts f none 0444 x sys 5 6 7 X11R5 X11R5\n"
                      "/z f none 0644 root bin 1 2 3 core\n")
            with open(d + "/contents", "w") as f:
                f.write(others + "%s/bin d none 0755 root bin AMIXboot\n"
                        "%s/bin/pkg f none 0555 root bin 1 2 3 AMIXboot\n" % (boot, boot))
            fn = "\n".join(netinstall.unboot_lines())
            fn = (fn.replace("/var/sadm/install/contents", d + "/contents")
                  .replace(netinstall.BOOT_DIR, boot).replace("/var/sadm/pkg/AMIXboot", sadm))
            r = subprocess.run(["sh", "-c", "report() { echo \"$*\"; }\n%s\nunboot" % fn],
                               stdout=subprocess.PIPE, universal_newlines=True)
            self.assertIn("AMIXboot removed", r.stdout)
            with open(d + "/contents") as f:
                self.assertEqual(f.read(), others)
            self.assertTrue(os.path.exists(d + "/contents.amix-installer"))
            self.assertFalse(os.path.exists(boot) or os.path.exists(sadm))
        finally:
            subprocess.run(["rm", "-rf", d])

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


class Picasso(unittest.TestCase):
    """The Picasso II: a 68030 with the stock kernel, X through /dev/rtgfb."""

    def cfg(self, **sel):
        base = {"mach": "uae", "accel": "none", "kern": "stock", "boot": "pvmk",
                "gfx": "picasso2", "desk": "cde"}
        base.update(sel)
        return model.Config(sel=base)

    def test_choice_rules(self):
        c = self.cfg()
        self.assertEqual(c.sel["gfx"], "picasso2")
        self.assertEqual(c.sel["desk"], "cde")
        # the 68060 kernels cannot run a 68030: no Picasso II with them
        self.assertNotEqual(self.cfg(mach="a3000", accel="z3660", kern="z3opt").sel["gfx"],
                            "picasso2")
        # the stock boot partition holds one kernel: pvmk boots the new one
        self.assertNotEqual(self.cfg(boot="amixboot").sel["gfx"], "picasso2")
        # no card, no X desktop
        c = self.cfg(gfx="chipset")
        self.assertNotIn(c.sel["desk"], ("cde", "mwm"))
        self.assertNotIn("Picasso II", " ".join(netinstall.packages_for(self.cfg())[1]))

    def test_steps_install_the_kernel(self):
        t = netinstall.steps_script(self.cfg(), "10.0.2.2", kernel=(1478000, 4242))
        Scripts.sh_ok(self, t)
        self.assertNotIn("$(", t)
        self.assertIn("$PKG http get $W/kernel /stand/unix.new", t)
        self.assertIn('[ "$1" = 4242 ]', t)
        self.assertIn("-eq 1478000", t)
        self.assertIn("/usr/sbin/mknod /dev/rtgfb c 54 0", t)
        self.assertIn("echo cirrus > /etc/X11/amix-card", t)
        self.assertLess(t.index("/stand/unix.stock"), t.index("mv /stand/unix.new /stand/unix"))
        self.assertLess(t.index("step 7"), t.index("step 8"))
        no = netinstall.steps_script(self.cfg(), "10.0.2.2")
        self.assertNotIn("/stand/unix.new", no)
        self.assertIn("no kernel with the Picasso II driver", no)
        self.assertEqual(netinstall.packages_for(self.cfg())[0][0], "x11/xserver-amix")
        z = netinstall.steps_script(model.Config(sel={"desk": "cde"}), "10.0.2.2")
        self.assertIn("echo z3660 > /etc/X11/amix-card", z)     # the Z3660 apart from a ZZ9000
        self.assertNotIn("/stand/unix.new", z)
        # the 68040/060 port kernel: the Z3660 RTG from its kit, /dev/z3660rtg
        k = model.Config(sel={"mach": "a3000", "accel": "z3660", "kern": "k040", "gfx": "rtg",
                              "desk": "cde"})
        kz = netinstall.steps_script(k, "10.0.2.2", kernel=(1490000, 777))
        Scripts.sh_ok(self, kz)
        self.assertIn("/usr/sbin/mknod /dev/z3660rtg c 55 0", kz)
        self.assertIn("echo z3660 > /etc/X11/amix-card", kz)
        self.assertNotIn("amix-card", netinstall.steps_script(self.cfg(desk="console"), "10.0.2.2"))

    def test_steps_hydra_and_zz9000(self):
        c = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "zz9000", "net": "nethydra", "desk": "cde"})
        t = netinstall.steps_script(c, "10.0.2.2", kernel=(1490000, 777))
        Scripts.sh_ok(self, t)
        self.assertIn("/usr/sbin/mknod /dev/zz9000 c 49 0", t)
        self.assertIn("/usr/sbin/mknod /dev/hya0 c 47 0", t)
        self.assertIn("echo zz9000 > /etc/X11/amix-card", t)
        self.assertIn("/usr/sbin/slink addaen /dev/hya0 hya0 2>/dev/null &&", t)
        self.assertIn("ifconfig hya0 `uname -n` up -trailers", t)

    def test_steps_xsurf100(self):
        c = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "picasso2", "net": "netxsurf", "desk": "cde"})
        self.assertEqual(c.sel["net"], "netxsurf")
        t = netinstall.steps_script(c, "10.0.2.2", kernel=(1490000, 777))
        Scripts.sh_ok(self, t)
        self.assertIn("/usr/sbin/mknod /dev/xsf0 c 56 0", t)
        self.assertIn("/usr/bin/chmod 600 /dev/xsf0", t)
        self.assertIn("# X-Surf 100 (written by the AMIX Installer)", t)
        self.assertIn("/usr/sbin/slink addaen /dev/xsf0 xsf0 2>/dev/null &&", t)
        self.assertIn("ifconfig xsf0 `uname -n` up -trailers", t)
        self.assertNotIn("hya0", t)

    def test_steps_zz9000eth(self):
        c = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "zz9000", "net": "netzz9000", "desk": "cde"})
        t = netinstall.steps_script(c, "10.0.2.2", kernel=(1490000, 777))
        Scripts.sh_ok(self, t)
        self.assertIn("/usr/sbin/mknod /dev/zz9000 c 49 0", t)
        self.assertIn("/usr/sbin/mknod /dev/zz0 c 52 0", t)
        self.assertIn("/usr/sbin/slink addaen /dev/zz0 zz0 2>/dev/null &&", t)
        self.assertIn("ifconfig zz0 `uname -n` up -trailers", t)

    def test_steps_further_network_cards(self):
        """the install card gets the host name's address; the further cards
        only their driver and device node"""
        c = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "zz9000", "net": "nethydra", "desk": "cde"},
                         on={"hw_xsurf": True, "hw_zz9000eth": True, "hw_picasso2": True})
        t = netinstall.steps_script(c, "10.0.2.2", kernel=(1490000, 777))
        Scripts.sh_ok(self, t)
        for node, major in (("zz9000", 49), ("rtgfb", 54), ("hya0", 47), ("xsf0", 56), ("zz0", 52)):
            self.assertIn("/usr/sbin/mknod /dev/%s c %d 0" % (node, major), t)
        self.assertIn("ifconfig hya0 `uname -n` up -trailers", t)
        self.assertNotIn("ifconfig xsf0", t)
        self.assertNotIn("ifconfig zz0", t)
        self.assertIn("echo zz9000 > /etc/X11/amix-card", t)

    def test_data_disk_vfstab_on_the_machine(self):
        """mkfs in FS-UAE on c1d0s1; the machine's vfstab names c9d0s1 when
        the disks are on the Z3660's SD card"""
        c = model.Config(sel={"mach": "a3000", "accel": "z3660", "target": "sd"})
        t = netinstall.steps_script(c, "10.0.2.2",
                                    data_disk=("/dev/rdsk/c1d0s1", 4000000, "/dev/rdsk/c9d0s1"))
        Scripts.sh_ok(self, t)
        self.assertIn("D=/dev/rdsk/c1d0s1", t)
        self.assertIn("V=/dev/rdsk/c9d0s1", t)
        self.assertIn('echo \"`echo $V | /usr/bin/sed s/rdsk/dsk/` $V /amixdata', t)
        # the old two-part form: the machine is FS-UAE's
        t2 = netinstall.steps_script(c, "10.0.2.2", data_disk=("/dev/rdsk/c1d0s1", 4000000))
        self.assertIn("V=/dev/rdsk/c1d0s1", t2)

    def test_storage_driver_no_mknod(self):
        """a storage driver gets no device node, a network one beside it does"""
        from amix_installer import plan
        old = plan.kernel_drivers
        plan.kernel_drivers = lambda cfg: ["z3660scsi", "xsurf100"]
        try:
            t = "\n".join(netinstall.kernel_lines(
                model.Config(sel={"net": "netxsurf"}), (1490000, 777)))
        finally:
            plan.kernel_drivers = old
        self.assertIn("Z3660 SD card (a controller of AMIX's dd/sd, b18/c40)", t)
        self.assertIn("/usr/sbin/mknod /dev/xsf0 c 56 0", t)
        self.assertEqual([ln for ln in t.splitlines() if "/usr/sbin/mknod" in ln],
                         ["[ -c /dev/xsf0 ] || /usr/sbin/mknod /dev/xsf0 c 56 0 || fail 'mknod /dev/xsf0'"])

    def test_sysv_sum(self):
        data = os.urandom(70000)
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(data)
        try:
            r = subprocess.run(["sum", "-s", f.name], stdout=subprocess.PIPE,
                               universal_newlines=True)
        except OSError:
            self.skipTest("no sum(1)")
        finally:
            os.unlink(f.name)
        self.assertEqual(int(r.stdout.split()[0]), netinstall.sysv_sum(data))

    def test_served(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"\x7fELF kernel")
        try:
            s = netinstall.Session(self.cfg(), "/nonexistent", host="127.0.0.1",
                                   tftp_port=0, http_port=0, bind="127.0.0.1", kernel=f.name)
            s.start()
            try:
                u = "http://127.0.0.1:%d" % s.http_port
                self.assertEqual(urllib.request.urlopen(u + "/kernel").read(), b"\x7fELF kernel")
                self.assertIn("-eq 11 ]", urllib.request.urlopen(u + "/steps").read().decode())
            finally:
                s.stop()
        finally:
            os.unlink(f.name)


class Foundation(unittest.TestCase):
    """The same foundation on FS-UAE and real machines, before any optional set."""

    def test_order_and_items(self):
        import time
        now = time.struct_time((2026, 11, 16, 12, 5, 0, 0, 320, 0))
        t = netinstall.steps_script(model.Config(sel={"desk": "cde"}), "10.0.2.2", now=now)
        Scripts.sh_ok(self, t)
        self.assertNotIn("$(", t)
        # the patch level first, then the client, the base set, then the options
        self.assertLess(t.index("foundation patch present"), t.index("the package client"))
        self.assertLess(t.index("the package client"), t.index("foundation library/amixc99"))
        self.assertLess(t.index("foundation DONE"), t.index("step 6"))
        for p in netinstall.FOUNDATION_PKGS:
            self.assertIn("$PKG --min-free $M install %s " % p, t)
        self.assertIn("TZ=GMT0 /usr/bin/date 111612052026", t)
        self.assertIn("/usr/sbin/removef core /usr/amiga/bin/setclk", t)
        self.assertIn("grep -v '^/usr/amiga/bin/setclk ' /var/sadm/install/contents", t)
        self.assertIn("/var/sadm/install/contents.amix-installer", t)
        self.assertLess(t.index("/usr/bin/cp -p /var/sadm/install/contents /var/sadm/install/contents.new"),
                        t.index("grep -v '^/usr/amiga/bin/setclk '"))
        self.assertNotIn("chmod 644 /var/sadm", t)
        self.assertIn("/usr/bin/mv /usr/amiga/bin/setclk /usr/amiga/bin/setclk.core", t)
        self.assertIn("/usr/bin/mv /usr/amiga/bin/setclk.core /usr/amiga/bin/setclk; report", t)
        self.assertLess(t.index("install sysutils/setclk"),
                        t.index("rm -f /usr/amiga/bin/setclk.core"))
        self.assertLess(t.index("removef core /usr/amiga/bin/setclk"),
                        t.index("install sysutils/setclk"))
        self.assertIn("/etc/clkzone", t)
        self.assertIn("/etc/init.d/syslogd.stock", t)
        self.assertIn("foundation random missing", t)
        # without the patch disk a missing patch stops the install
        self.assertIn("fail 'apply Patch Disk 1-2a", t)
        self.assertNotIn("$W/patch", t)
        # the base set is not installed twice as an option
        step6 = t[t.index("step 6"):]
        self.assertNotIn("gnu/gnu-tools", step6)

    def test_patch_disk_applied(self):
        t = netinstall.steps_script(model.Config(), "10.0.2.2", patch=True)
        Scripts.sh_ok(self, t)
        self.assertIn("$B/bin/pkg http get $W/patch /tmp/amixpatch", t)
        self.assertIn("A='n danger y n go'", t)
        self.assertIn("A='n danger n go'", t)
        self.assertIn("/sbin/sh /tmp/amixpatch", t)
        self.assertIn("REBOOT=yes", t)
        self.assertNotIn("fail 'apply Patch Disk 1-2a", t)

    def test_base_set_must_be_in_the_repo(self):
        with tempfile.TemporaryDirectory() as repo:
            t = netinstall.steps_script(model.Config(), "10.0.2.2", repo=repo)
        Scripts.sh_ok(self, t)
        self.assertIn("foundation sysutils/setclk failed: not in the repository", t)
        self.assertEqual(t.count("fail 'the foundation needs"), 1)

    def test_report_file(self):
        with tempfile.TemporaryDirectory() as d:
            rep = os.path.join(d, "foundation-report.txt")
            s = netinstall.Session(model.Config(), d, host="127.0.0.1", tftp_port=0,
                                   http_port=0, bind="127.0.0.1", report=rep)
            s.start()
            try:
                s.reported("msg", "step 2: foundation: the AMIX patch level")
                s.reported("msg", "foundation patch present 2.1p2a (kernel 2.1c 0800430)")
                s.reported("msg", "foundation sysutils/setclk installed")
            finally:
                s.stop()
            with open(rep) as f:
                lines = f.read().splitlines()
        self.assertEqual(lines[1:], ["foundation patch present 2.1p2a (kernel 2.1c 0800430)",
                                     "foundation sysutils/setclk installed"])
