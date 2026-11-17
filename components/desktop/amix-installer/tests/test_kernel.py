import hashlib
import os
import shutil
import stat
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from amix_installer import kernel  # noqa: E402

BASE = list(kernel.BASE_NEEDS) + ["parinit"]
RTGFB = kernel.DRIVERS["rtgfb"].entries


def elf(text_syms=(), data_syms=(), undef=(), text=64, data=16):
    """A big-endian ELF32 relocatable with .text, .data and a symbol table:
    the global symbols the kernel checks look at, nothing else."""
    shstr = b"\0.text\0.data\0.symtab\0.strtab\0.shstrtab\0"
    off = {n: shstr.index(n.encode()) for n in (".text", ".data", ".symtab", ".strtab", ".shstrtab")}
    strtab, syms = b"\0", [b"\0" * 16]
    for names, shndx in ((text_syms, 1), (data_syms, 2), (undef, 0)):
        for n in names:
            syms.append(struct.pack(">IIIBBH", len(strtab), 0, 0, 0x10, 0, shndx))
            strtab += n.encode() + b"\0"
    symtab = b"".join(syms)
    body = [b"\0" * text, b"\0" * data, symtab, strtab, shstr]
    pos, offs = 52, []
    for b in body:
        offs.append(pos)
        pos += len(b)
    shoff = pos
    hdr = (b"\x7fELF\x01\x02\x01" + b"\0" * 9 +
           struct.pack(">HHIIIIIHHHHHH", 1, 4, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 6, 5))
    sh = [b"\0" * 40]
    specs = [(".text", 1, 0), (".data", 1, 0), (".symtab", 2, 4), (".strtab", 3, 0),
             (".shstrtab", 3, 0)]
    for (n, typ, link), o, b in zip(specs, offs, body):
        sh.append(struct.pack(">IIIIIIIIII", off[n], typ, 0, 0, o, len(b), link, 0, 0,
                              16 if typ == 2 else 0))
    return hdr + b"".join(body) + b"".join(sh)


def base_kernel(**kw):
    t = [s for s in BASE if s not in ("cdevsw", "io_init")]
    return elf(text_syms=kw.get("text", t), data_syms=kw.get("data", ["cdevsw", "io_init"]))


def with_init(drv, init):
    return drv.with_info({"init": init})


class Checks(unittest.TestCase):
    def test_base_ok(self):
        kernel.check_base(base_kernel())

    def test_base_needs_the_boot_hook_when_a_driver_has_init(self):
        d = with_init(kernel.DRIVERS["rtgfb"], "rtgfbinit")
        t = [s for s in BASE if s not in ("cdevsw", "io_init", "parinit")]
        with self.assertRaisesRegex(kernel.KernelError, "parinit"):
            kernel.check_base(base_kernel(text=t), [d])
        with self.assertRaisesRegex(kernel.KernelError, "io_init"):
            kernel.check_base(base_kernel(data=["cdevsw"]), [d])
        # io_init must be the data table, not code
        with self.assertRaisesRegex(kernel.KernelError, "io_init"):
            kernel.check_base(base_kernel(text=[s for s in BASE if s != "cdevsw"] + ["io_init"],
                                          data=["cdevsw"]), [d])
        # a kit without a boot routine needs no hook (rtgfb-kit 0.1)
        kernel.check_base(base_kernel(data=["cdevsw"]), [kernel.DRIVERS["rtgfb"]])

    def test_base_refuses_port_and_relinked_kernels(self):
        t = [s for s in BASE if s not in ("cdevsw", "io_init")]
        # a port kernel takes 040-line drivers only
        with self.assertRaisesRegex(kernel.KernelError, "68040/060 port kernel; the Picasso II"):
            kernel.check_base(base_kernel(text=t + ["cputype"]))
        va = kernel.DRIVERS["va2000"].with_info({"line": "040"})
        kernel.check_base(base_kernel(text=t + ["cputype"]), [va])
        self.assertEqual(kernel.base_line(base_kernel(text=t + ["cputype"])), "040")
        self.assertEqual(kernel.base_line(base_kernel()), "030")
        with self.assertRaisesRegex(kernel.KernelError, "already"):
            kernel.check_base(base_kernel(text=t + ["rtgfbopen"]))
        with self.assertRaisesRegex(kernel.KernelError, "linked before"):
            kernel.check_base(base_kernel(text=t + [kernel.STUB]))

    def test_result_entries_init_and_stub(self):
        kernel.check_result(elf(text_syms=RTGFB))
        with self.assertRaisesRegex(kernel.KernelError, "rtgfbmmap"):
            kernel.check_result(elf(text_syms=RTGFB[:-1]))
        d = with_init(kernel.DRIVERS["rtgfb"], "rtgfbinit")
        kernel.check_result(elf(text_syms=RTGFB + ("rtgfbinit", kernel.STUB)), [d])
        with self.assertRaisesRegex(kernel.KernelError, kernel.STUB):
            kernel.check_result(elf(text_syms=RTGFB + ("rtgfbinit",)), [d])
        with self.assertRaisesRegex(kernel.KernelError, "unresolved"):
            kernel.check_result(elf(text_syms=RTGFB, undef=["rtgfb_missing"]))

    def test_kit_info(self):
        d = kernel.DRIVERS["hydra"].with_info(
            {"id": "hydra", "major": "47", "node": "/dev/hya0", "line": "030", "init": "hydrainit",
             "entries": "hydrainfo", "streams": "yes"})
        self.assertEqual((d.node, d.major, d.init, d.entries, d.streams),
                         ("hya0", 47, "hydrainit", ("hydrainfo",), True))
        with self.assertRaises(kernel.KernelError):
            kernel.DRIVERS["hydra"].with_info({"id": "zz9000"})
        self.assertIsNone(kernel.DRIVERS["rtgfb"].with_info(None).init)

    def test_int2(self):
        """the level-2 chain: aenintr first, sp@(4) passed on, hooked once"""
        src = kernel.int2_source(["hydraintr"])
        self.assertLess(src.index("jsr\taenintr"), src.index("jsr\thydraintr"))
        self.assertEqual(src.count("movel\t%sp@(4),%sp@-"), 2)
        self.assertEqual(src.count("addql\t&4,%sp"), 2)
        self.assertLess(src.index("rts"), src.index(".align\t4"))
        h = kernel.DRIVERS["hydra"].with_info({"intr": "hydraintr", "entries": "hydrainfo"})
        self.assertEqual(h.intr, "hydraintr")
        t = [x for x in BASE if x not in ("cdevsw", "io_init")]
        with self.assertRaisesRegex(kernel.KernelError, "int2_tbl"):
            kernel.check_base(base_kernel(text=t + ["aenintr"]), [h])
        with self.assertRaisesRegex(kernel.KernelError, "aenintr"):
            kernel.check_base(base_kernel(data=["cdevsw", "io_init", "int2_tbl"]), [h])
        kernel.check_base(base_kernel(text=t + ["aenintr"],
                                      data=["cdevsw", "io_init", "int2_tbl"]), [h])
        with self.assertRaisesRegex(kernel.KernelError, "linked before"):
            kernel.check_base(base_kernel(text=t + [kernel.INT2_STUB]))
        kernel.check_result(elf(text_syms=("hydrainfo", "hydraintr", kernel.INT2_STUB)), [h])
        with self.assertRaisesRegex(kernel.KernelError, kernel.INT2_STUB):
            kernel.check_result(elf(text_syms=("hydrainfo", "hydraintr")), [h])

    def test_stub(self):
        s = kernel.stub_source(["rtgfbinit", "hydrainit"])
        self.assertLess(s.index("jsr\tparinit"), s.index("jsr\trtgfbinit"))
        self.assertLess(s.index("jsr\trtgfbinit"), s.index("jsr\thydrainit"))
        self.assertIn(".globl\t%s" % kernel.STUB, s)
        self.assertLess(s.index("rts"), s.index(".align\t4"))


class Relink(unittest.TestCase):
    """The order of the steps, with a stand-in as, ld and kit scripts that log."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.log = os.path.join(self.d, "calls")
        self.result = os.path.join(self.d, "result.o")
        self.ld = self.tool("ld", "echo ld >> %s\ncp %s \"$3\"\n" % (self.log, self.result))
        self.asm = self.tool("as", "echo as >> %s\ncp %s \"$2\"\n"
                             % (self.log, os.path.join(self.d, "as.in")))
        with open(os.path.join(self.d, "as.in"), "w") as f:
            f.write("stub")

    def tool(self, name, body):
        p = os.path.join(self.d, name)
        with open(p, "w") as f:
            f.write("#!/bin/sh\n" + body)
        os.chmod(p, stat.S_IRWXU)
        return p

    def kit(self, drv, files, info=None):
        k = os.path.join(self.d, drv)
        os.mkdir(k)
        names = list(files) + (["KIT.info"] if info is not None else [])
        for name in files:
            with open(os.path.join(k, name), "w") as f:
                if name.endswith(".py"):
                    f.write("import sys\nopen(%r, 'a').write(%r + ' ' + ' '.join(sys.argv[2:]) "
                            "+ '\\n')\n" % (self.log, name))
                else:
                    f.write(drv + name)
        if info is not None:
            with open(os.path.join(k, "KIT.info"), "w") as f:
                f.write(info)
        with open(os.path.join(k, "KIT.sha256"), "w") as f:
            for name in names:
                with open(os.path.join(k, name), "rb") as g:
                    f.write("%s  %s\n" % (hashlib.sha256(g.read()).hexdigest(), name))
        return k

    def result_has(self, syms):
        with open(self.result, "wb") as f:
            f.write(elf(text_syms=syms))

    def calls(self):
        with open(self.log) as f:
            return [line.strip() for line in f]

    def tearDown(self):
        shutil.rmtree(self.d)

    def test_old_kit_no_boot_routine(self):
        """rtgfb-kit 0.1: no KIT.info, no stub, no io_init retarget."""
        k = self.kit("rtgfb", ["rtgfb_030.o", "rtgfb_pad.o", "patch_rtgfb_cdevsw.py",
                               "check_relink_relocs.py"])
        self.result_has(RTGFB)
        out = os.path.join(self.d, "unix.rtgfb")
        kernel.relink(base_kernel(), out, ["rtgfb"], {"rtgfb": k}, self.ld, asm=self.asm)
        self.assertEqual(self.calls(), ["ld", "patch_rtgfb_cdevsw.py", "check_relink_relocs.py"])
        self.assertTrue(os.path.isfile(out))

    def test_two_drivers_one_stub(self):
        g = self.kit("rtgfb", ["rtgfb_030.o", "patch_rtgfb_cdevsw.py", "patch_io_init.py",
                               "check_relink_relocs.py"],
                     "id=rtgfb\nmajor=54\nnode=/dev/rtgfb\nline=030\ninit=rtgfbinit\n"
                     "entries=%s\nstreams=no\n" % " ".join(RTGFB))
        n = self.kit("hydra", ["hydra_030.o", "patch_hydra_cdevsw.py", "patch_io_init.py",
                               "check_relink_relocs.py"],
                     "id=hydra\nmajor=47\nnode=/dev/hya0\nline=030\ninit=hydrainit\n"
                     "entries=hydrainfo\nstreams=yes\n")
        self.result_has(RTGFB + ("rtgfbinit", "hydrainfo", "hydrainit", kernel.STUB))
        out = os.path.join(self.d, "unix.rtgfb+hydra")
        # given network first: the stub still calls graphics first
        kernel.relink(base_kernel(), out, ["hydra", "rtgfb"], {"rtgfb": g, "hydra": n}, self.ld,
                      asm=self.asm)
        calls = self.calls()
        self.assertEqual(calls[:2], ["as", "ld"])
        self.assertEqual(calls.count("patch_io_init.py %s" % kernel.STUB), 1)
        self.assertLess(calls.index("patch_hydra_cdevsw.py"),
                        calls.index("patch_io_init.py %s" % kernel.STUB))
        self.assertEqual(calls[-1], "check_relink_relocs.py")
        self.assertEqual(kernel.stub_source(["rtgfbinit", "hydrainit"]).count("jsr"), 3)

    def test_int2_chain_order(self):
        """kit patches, then patch_int2.py once, then patch_io_init.py once"""
        n = self.kit("hydra", ["hydra_030.o", "patch_hydra_cdevsw.py", "patch_int2.py",
                               "patch_io_init.py", "check_relink_relocs.py"],
                     "id=hydra\nmajor=47\nnode=/dev/hya0\nline=030\ninit=hydrainit\n"
                     "intr=hydraintr\nentries=hydrainfo hydrainit hydraintr\nstreams=yes\n")
        self.result_has(("hydrainfo", "hydrainit", "hydraintr", kernel.STUB, kernel.INT2_STUB))
        t = [x for x in BASE if x not in ("cdevsw", "io_init")] + ["aenintr"]
        kernel.relink(base_kernel(text=t, data=["cdevsw", "io_init", "int2_tbl"]),
                      os.path.join(self.d, "u"), ["hydra"], {"hydra": n}, self.ld, asm=self.asm)
        calls = self.calls()
        self.assertEqual(calls[:3], ["as", "as", "ld"])
        self.assertEqual(calls[3:6], ["patch_hydra_cdevsw.py",
                                      "patch_int2.py %s" % kernel.INT2_STUB,
                                      "patch_io_init.py %s" % kernel.STUB])
        self.assertEqual(calls[-1], "check_relink_relocs.py")

    def test_missing_kit_file(self):
        k = self.kit("rtgfb", ["rtgfb_030.o", "check_relink_relocs.py"])
        os.remove(os.path.join(k, "rtgfb_030.o"))
        with self.assertRaisesRegex(kernel.KernelError, "lacks rtgfb_030.o"):
            kernel.relink(base_kernel(), os.path.join(self.d, "u"), ["rtgfb"], {"rtgfb": k},
                          self.ld)

    def test_no_kit_for_the_line(self):
        """Z3660 Ethernet exists for the 040 line only: say so, not "not installed"."""
        with self.assertRaisesRegex(kernel.KernelError, "built for the 68040/060 port kernel only"):
            kernel.relink(base_kernel(), os.path.join(self.d, "u"), ["z3660eth"], ld=self.ld)

    def test_listed_twice(self):
        k = self.kit("rtgfb", ["rtgfb_030.o", "check_relink_relocs.py"])
        with open(os.path.join(k, "KIT.sha256")) as f:
            first = f.readline()
        with open(os.path.join(k, "KIT.sha256"), "a") as f:
            f.write(first)
        with self.assertRaisesRegex(kernel.KernelError, "rtgfb_030.o more than once"):
            kernel.check_kit(k, "rtgfb")
        # kit-tools: the same refusal before a shared patch script runs
        with self.assertRaisesRegex(kernel.KernelError, "more than once"):
            kernel.check_tools(os.path.join(k, "check_relink_relocs.py"))
        os.remove(os.path.join(k, "KIT.sha256"))
        kernel.check_tools(os.path.join(k, "check_relink_relocs.py"))   # no listing: taken as is

    def test_040_kit_refused(self):
        k = self.kit("va2000", ["va2000_040.o", "check_relink_relocs.py"],
                     "id=va2000\nmajor=68\nnode=/dev/va2000\nline=040\n")
        with self.assertRaisesRegex(kernel.KernelError, "built for the 68040/060 port kernel"):
            kernel.relink(base_kernel(), os.path.join(self.d, "u"), ["va2000"], {"va2000": k},
                          self.ld)

    def test_kit_by_line(self):
        """va2000 (030) and va2000-040 (040): the base kernel's line picks."""
        share = os.path.join(self.d, "share")
        os.mkdir(share)
        k30 = self.kit("va2000", ["va2000_030.o", "check_relink_relocs.py"],
                       "id=va2000\nline=030\nentries=va2000open\n")
        k40 = self.kit("va2000-040", ["va2000_040.o", "check_relink_relocs.py"],
                       "id=va2000\nline=040\nentries=va2000open dev_kvmap\n")
        old = kernel._share_dir
        kernel._share_dir = lambda: share
        env = dict(os.environ)
        try:
            for k in (k30, k40):
                shutil.move(k, share)
            os.environ.pop("AMIX_VA2000_KIT", None)
            os.environ.pop("AMIX_VA2000_040_KIT", None)
            home = os.path.expanduser("~/Sources/usr-amix/amix-gate")
            if os.path.isdir(home):
                os.environ["HOME"] = self.d          # no amix-gate checkout in the way
            self.assertEqual(kernel.find_kit("va2000"), os.path.join(share, "va2000"))
            self.assertEqual(kernel.find_kit("va2000", "040"), os.path.join(share, "va2000-040"))
            with self.assertRaises(kernel.KernelError):
                kernel.find_kit("va2000", "060")
            # a port kernel: the 040 kit is linked, not the 030 one
            t = [s for s in BASE if s not in ("cdevsw", "io_init")] + ["cputype"]
            self.result_has(("va2000open", "dev_kvmap"))
            kernel.relink(base_kernel(text=t), os.path.join(self.d, "u"), ["va2000"],
                          ld=self.ld)
            with open(self.log) as f:
                self.assertEqual(f.read().split(), ["ld", "check_relink_relocs.py"])
        finally:
            kernel._share_dir = old
            os.environ.clear()
            os.environ.update(env)


if __name__ == "__main__":
    unittest.main()
