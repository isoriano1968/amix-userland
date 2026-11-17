import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from amix_installer import fsuae, hdf, model, plan, safety  # noqa: E402


def fake_source(path, found=(), pvmk=None, movable=0):
    s = hdf.SourceInfo()
    s.path = path
    s.root_used = 300 << 20
    s.root_avail = 900 << 20
    s.found = set(found)
    s.pvmk = pvmk
    s.movable = {"/usr/local": movable}
    s.packages = [("core", "Amiga Unix essential components", "")]
    s.patch = "2.1p2a"
    return s


class Base(unittest.TestCase):
    def setUp(self):
        plan._fs_cache[:] = [fsuae.FsUae()]      # no FS-UAE: tests never see the host's
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        self.src = os.path.join(self.d, "core.hdf")
        with open(self.src, "wb") as f:
            f.write(os.urandom(3 << 20) + b"\0" * (1 << 20))
        self.out = os.path.join(self.d, "out")
        os.mkdir(self.out)

    def tearDown(self):
        self.tmp.cleanup()

    def cfg(self, **sel):
        sel.setdefault("target", "file")
        c = model.Config(sel=sel, source=fake_source(self.src))
        return c

    def paths(self):
        return {"core_hdf": self.src, "out_dir": self.out}


class MakePlan(Base):
    def test_order_and_missing(self):
        pl = plan.make_plan(self.cfg(), self.paths())
        self.assertEqual(pl.problems, [])
        ids = [a.id for a in pl.actions]
        self.assertEqual(ids[0], "copy-core")
        self.assertEqual(ids[-1], "sums")
        self.assertIn("data-disk", ids)
        self.assertFalse(pl.complete)
        byid = dict((a.id, a) for a in pl.actions)
        self.assertTrue(byid["data-disk"].available)
        self.assertTrue(byid["install"].missing)

    def test_problems(self):
        pl = plan.make_plan(self.cfg(), {"core_hdf": self.src})
        self.assertTrue(any("folder" in p for p in pl.problems))
        # a running system: only the network install, no disks
        pl = plan.make_plan(model.Config(sel={"src": "run", "mach": "a2500", "accel": "gvp"}),
                            {"repo": self.d})
        self.assertEqual([a.id for a in pl.actions], ["install"])
        self.assertTrue(pl.actions[0].available)
        self.assertEqual(pl.problems, [])
        old = os.environ.get("AMIX_REPO")
        os.environ["AMIX_REPO"] = ""
        try:
            nr = plan.make_plan(model.Config(sel={"src": "run", "mach": "a2500", "accel": "gvp"}),
                                {}, fsuae.FsUae())
            if plan.default_repo() is None:
                self.assertIn("make repo", nr.actions[0].missing)
        finally:
            if old is None:
                del os.environ["AMIX_REPO"]
            else:
                os.environ["AMIX_REPO"] = old
        c = self.cfg()
        c.source = None
        self.assertTrue(any("not been read" in p for p in plan.make_plan(c, self.paths()).problems))

    def test_card_names_and_ids(self):
        pl = plan.make_plan(self.cfg(target="card", mach="a2500", accel="gvp"), self.paths())
        self.assertEqual(pl.problems, [])
        outs = [n for a in pl.actions for n in a.outputs]
        for n in ("HD60_512.hda", "HD50_512.hda", "HD10_512.hda"):
            self.assertIn(n, outs)

    def test_fs_uae_config(self):
        pl = plan.make_plan(self.cfg(), dict(self.paths(), kickstart="/k/A3000.rom"))
        fa = [a for a in pl.actions if a.id == "fs-uae"][0]
        self.assertTrue(fa.available)
        t = fa.text()
        self.assertIn("network_card = a2065", t)
        self.assertIn("kickstart_file = /k/A3000.rom", t)
        self.assertIn("hard_drive_1 = %s" % os.path.join(self.out, "core-core.hdf"), t)
        self.assertIn("hard_drive_1_controller = scsi6", t)
        self.assertIn("hard_drive_0_controller = scsi5", t)
        self.assertIn("hard_drive_2_controller = scsi1", t)
        nokick = plan.make_plan(self.cfg(), self.paths())
        self.assertFalse([a for a in nokick.actions if a.id == "fs-uae"][0].available)

    def test_install_step_lists_what_amix_does(self):
        pl = plan.make_plan(self.cfg(), self.paths())
        inst = [a for a in pl.actions if a.id == "install"][0]
        self.assertIn("package client", inst.detail)
        self.assertIn("kernel last", inst.detail)
        self.assertFalse(inst.available)

    def test_existing_output_refused(self):
        open(os.path.join(self.out, "core-core.hdf"), "w").close()
        pl = plan.make_plan(self.cfg(), self.paths())
        self.assertTrue(any("exists" in p for p in pl.problems))

    def test_out_dir_is_source_dir_ok_but_never_source(self):
        pl = plan.make_plan(self.cfg(), {"core_hdf": self.src, "out_dir": self.d})
        self.assertEqual(pl.problems, [])
        self.assertNotIn(self.src, [os.path.join(self.d, n) for a in pl.actions for n in a.outputs])

    def test_sd_gets_fragment_check(self):
        ids = [a.id for a in plan.make_plan(self.cfg(target="sd"), self.paths()).actions]
        self.assertIn("frag", ids)

    def test_installed_not_planned(self):
        c = self.cfg()
        c.source = fake_source(self.src, found=model.PKG_CLIENT_PROBES)
        ids = [a.id for a in plan.make_plan(c, self.paths()).actions]
        self.assertNotIn("pkg-client", ids)

    def test_pvmk_conf(self):
        c = self.cfg(keymap="de")
        conf = plan.pvmk_conf(c)
        self.assertIn("entry=AMIX,UNIX_Root:/stand/unix,native", conf)
        self.assertIn("keymap=de", conf)
        self.assertIn("z3reloc=yes", conf)
        c = self.cfg(mach="uae", keymap="depc")
        conf = plan.pvmk_conf(c)
        self.assertIn("z3reloc=no", conf)
        self.assertIn("keymap=de-pc", conf)


def fake_fs(base, installed=True, config=False, roms=()):
    f = fsuae.FsUae()
    f.binary = "/usr/bin/fs-uae" if installed else None
    f.version = "3.2.35" if installed else None
    f.base = base
    os.makedirs(os.path.join(base, "Configurations"), exist_ok=True)
    os.makedirs(os.path.join(base, "Kickstarts"), exist_ok=True)
    for r in roms:
        open(os.path.join(base, "Kickstarts", r), "w").close()
    if config:
        open(f.config_path(), "w").close()
    return f


class FsUae(Base):
    def ids(self, fs, **paths):
        p = dict(self.paths(), **paths)
        return [a.id for a in plan.make_plan(self.cfg(), p, fs).actions]

    def test_suggests_and_writes_the_config(self):
        fs = fake_fs(os.path.join(self.d, "FS-UAE"), roms=("A3000.47.111.rom", "A3000.47.115.rom",
                                                            "A1200.47.115.rom"))
        self.assertEqual(fs.a3000_kickstarts()[0].endswith("A3000.47.115.rom"), True)
        pl = plan.make_plan(self.cfg(), self.paths(), fs)
        fa = [a for a in pl.actions if a.id == "fs-uae"][0]
        self.assertTrue(fa.available)                       # Kickstart from FS-UAE's folder
        self.assertIn("A3000.47.115.rom", fa.text())
        self.assertEqual(pl.actions[-1].id, "fs-uae-list")  # the last step
        pl.actions = [a for a in pl.actions if a.id in ("fs-uae", "fs-uae-list")]
        plan.execute(pl)
        with open(fs.config_path()) as f:
            self.assertIn("network_card = a2065", f.read())
        self.assertTrue(fs.has_config())

    def test_existing_config_is_kept(self):
        fs = fake_fs(os.path.join(self.d, "FS-UAE"), config=True, roms=("A3000.47.115.rom",))
        self.assertNotIn("fs-uae-list", self.ids(fs))

    def test_switched_off(self):
        fs = fake_fs(os.path.join(self.d, "FS-UAE"), roms=("A3000.47.115.rom",))
        self.assertNotIn("fs-uae-list", self.ids(fs, fsuae_add=False))

    def test_not_installed(self):
        fs = fake_fs(os.path.join(self.d, "FS-UAE"), installed=False, roms=("A3000.47.115.rom",))
        pl = plan.make_plan(self.cfg(), self.paths(), fs)
        self.assertNotIn("fs-uae-list", [a.id for a in pl.actions])
        inst = [a for a in pl.actions if a.id == "install"][0]
        self.assertIn("FS-UAE is not installed", inst.missing)

    def test_no_kickstart_anywhere(self):
        fs = fake_fs(os.path.join(self.d, "FS-UAE"))
        pl = plan.make_plan(self.cfg(), self.paths(), fs)
        self.assertFalse([a for a in pl.actions if a.id == "fs-uae"][0].available)
        self.assertNotIn("fs-uae-list", [a.id for a in pl.actions])

    def test_base_dir_from_environment(self):
        old = os.environ.get("FS_UAE_BASE_DIR")
        os.environ["FS_UAE_BASE_DIR"] = self.d
        try:
            self.assertEqual(fsuae.base_dir(), self.d)
        finally:
            if old is None:
                del os.environ["FS_UAE_BASE_DIR"]
            else:
                os.environ["FS_UAE_BASE_DIR"] = old

    def test_network_of_a_configuration(self):
        fs = fake_fs(self.d)
        def conf(n, t):
            with open(fs.config_path(n), "w") as f:
                f.write(t)
            return fs.config_path(n)
        self.assertEqual(fs.network_of(conf("net", "[fs-uae]\n network_card = A2065\n")),
                         "a2065")
        self.assertIsNone(fs.network_of(conf("plain", "[fs-uae]\namiga_model = A3000\n")))
        self.assertIsNone(fs.network_of(os.path.join(self.d, "none.fs-uae")))
        # the one the installer writes has the A2065
        pl = plan.make_plan(self.cfg(), dict(self.paths(), kickstart="/k.rom"), fs)
        fsc = [a for a in pl.actions if a.id == "fs-uae"][0]
        self.assertEqual(fs.network_of(conf("own", fsc.text())), "a2065")


class Execute(Base):
    def only(self, pl, *ids):
        pl.actions = [a for a in pl.actions if a.id in ids]
        return pl

    def test_copy_and_sums(self):
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core", "sums")
        before = plan.sha256_file(self.src)
        res = plan.execute(pl)
        self.assertEqual(res["core-core.hdf"], before)
        self.assertEqual(plan.sha256_file(os.path.join(self.out, "core-core.hdf")), before)
        self.assertEqual(plan.sha256_file(self.src), before)
        names = sorted(os.listdir(self.out))
        self.assertEqual(names, ["README-amix-installer.txt", "SHA256SUMS", "amix-installer.json",
                                 "core-core.hdf"])

    def test_failure_removes_everything_of_the_run(self):
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core", "sums")

        class Boom(plan.Action):
            def run(self, ctx):
                raise IOError("boom")
        pl.actions.insert(1, Boom("boom", "Boom", "", 1, ["boom.hdf"]))
        with self.assertRaises(IOError):
            plan.execute(pl)
        self.assertEqual(os.listdir(self.out), [])

    def test_cancel(self):
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core", "sums")
        with self.assertRaises(plan.Cancelled):
            plan.execute(pl, cancel=lambda: True)
        self.assertEqual(os.listdir(self.out), [])

    def test_interrupted_run_is_reported(self):
        with open(os.path.join(self.out, plan.JOURNAL), "w") as f:
            f.write("core-core.hdf.part\ncore-core.hdf\n")
        open(os.path.join(self.out, "core-core.hdf.part"), "w").close()
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core", "sums")
        with self.assertRaises(safety.Refused):
            plan.execute(pl)
        self.assertEqual(plan.leftover(self.out), ["core-core.hdf.part", "core-core.hdf"])
        plan.clean_leftover(self.out)
        self.assertEqual(os.listdir(self.out), [])

    @unittest.skipUnless(os.path.exists(os.path.expanduser(
        "~/Documents/FS-UAE/Hard Drives/AMIGA_UNIX.hdf")), "no AMIX core HDF here")
    def test_data_disk_from_core_rdb(self):
        core = os.path.expanduser("~/Documents/FS-UAE/Hard Drives/AMIGA_UNIX.hdf")
        a = plan.DataDisk(core, "data.hdf", 1000 << 20)
        pl = plan.Plan(self.cfg(), self.paths())
        pl.actions = [a]
        plan.execute(pl)
        with hdf.Image(os.path.join(self.out, "data.hdf")) as im:
            parts = im.read_rdb()
            self.assertEqual(im.rdb_block, 2)
        self.assertEqual([(p.name, p.kind, p.bootable) for p in parts],
                         [("UNIX_Data", "amix-root", False)])
        self.assertGreater(parts[0].size, 990 << 20)

    @unittest.skipUnless(plan.find_pvmk(), "no pvmk build here")
    def test_pvmk_disk(self):
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "pvmk")
        plan.execute(pl)
        with hdf.Image(os.path.join(self.out, "pvmk.hdf")) as im:
            parts = im.read_rdb()
        self.assertEqual([(p.name, p.kind, p.start) for p in parts], [("PVMK", "pvmk", 64)])


if __name__ == "__main__":
    unittest.main()


class Picasso(Base):
    def cfg(self, **sel):
        base = {"mach": "uae", "accel": "none", "kern": "stock", "boot": "pvmk",
                "gfx": "picasso2", "desk": "cde"}
        base.update(sel)
        return Base.cfg(self, **base)

    def test_fs_uae_config_has_the_card(self):
        pl = plan.make_plan(self.cfg(), dict(self.paths(), kickstart="/k.rom"))
        fsc = [a for a in pl.actions if a.id == "fs-uae"][0]
        self.assertIn("graphics_card = picasso-ii", fsc.text())
        pl = plan.make_plan(self.cfg(gfx="chipset"), dict(self.paths(), kickstart="/k.rom"))
        self.assertNotIn("graphics_card", [a for a in pl.actions if a.id == "fs-uae"][0].text())

    def test_kernel_action_and_pvmk_entries(self):
        pl = plan.make_plan(self.cfg(), self.paths())
        self.assertIn("kernel", [a.id for a in pl.actions])
        conf = plan.pvmk_conf(self.cfg())
        self.assertIn("entry=AMIX,UNIX_Root:/stand/unix,native", conf)
        self.assertIn("entry=AMIX stock,UNIX_Root:/stand/unix.stock,native", conf)
        self.assertNotIn("unix.stock", plan.pvmk_conf(self.cfg(gfx="chipset")))
        self.assertNotIn("kernel",
                         [a.id for a in plan.make_plan(self.cfg(gfx="chipset"), self.paths()).actions])


class Cards(Base):
    """HydraNet, ZZ9000, VA2000: offered, their drivers linked into the kernel
    here; a missing kit is named, never skipped silently."""

    def cfg(self, **sel):
        base = {"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk"}
        base.update(sel)
        return Base.cfg(self, **base)

    def test_choices(self):
        c = self.cfg(gfx="zz9000", net="nethydra", desk="cde")
        self.assertEqual((c.sel["gfx"], c.sel["net"], c.sel["desk"]), ("zz9000", "nethydra", "cde"))
        # FS-UAE has neither card; the ZZ9000 kit is for the stock kernel line
        self.assertNotEqual(self.cfg(mach="uae", gfx="zz9000").sel["gfx"], "zz9000")
        self.assertNotEqual(self.cfg(mach="uae", net="nethydra").sel["net"], "nethydra")
        # the X-Surf 100: Zorro machines, either kernel line
        self.assertEqual(self.cfg(net="netxsurf").sel["net"], "netxsurf")
        self.assertEqual(self.cfg(mach="a2500", accel="a2630", net="netxsurf").sel["net"],
                         "netxsurf")
        self.assertNotEqual(self.cfg(mach="uae", net="netxsurf").sel["net"], "netxsurf")
        self.assertEqual(plan.kernel_drivers({"gfx": "rtg", "net": "netxsurf"}), ["xsurf100"])
        # ZZ9000 Ethernet: only with the ZZ9000 card, both drivers linked
        z = self.cfg(gfx="zz9000", net="netzz9000")
        self.assertEqual((z.sel["gfx"], z.sel["net"]), ("zz9000", "netzz9000"))
        self.assertNotEqual(self.cfg(gfx="picasso2", net="netzz9000").sel["net"], "netzz9000")
        self.assertEqual(plan.kernel_drivers(z.sel), ["zz9000", "zz9000eth"])
        # the VA2000 has a kit for both kernel lines, and X has no va2000
        # driver yet: no X desktop with it
        v = self.cfg(accel="mercury", kern="k040", gfx="va2000", desk="cde")
        self.assertEqual(v.sel["gfx"], "va2000")
        self.assertNotIn(v.sel["desk"], ("cde", "mwm"))
        self.assertEqual(self.cfg(gfx="va2000").sel["gfx"], "va2000")
        z = self.cfg(accel="z3660", kern="z3opt", gfx="va2000")
        self.assertEqual(z.sel["kern"], "z3opt")
        self.assertNotEqual(z.sel["gfx"], "va2000")

    def test_kernel_drivers(self):
        self.assertEqual(plan.kernel_drivers({"gfx": "zz9000", "net": "nethydra"}),
                         ["zz9000", "hydra"])
        self.assertEqual(plan.kernel_drivers({"gfx": "chipset", "net": "net2065"}), [])
        a = plan.DriverKernel(self.src, ["zz9000", "hydra"])
        self.assertEqual(a.name, "unix.zz9000+hydra")
        self.assertIn("/dev/zz9000 (major 49), /dev/hya0 (major 47)", a.detail)
        old = {k: os.environ.pop(k, None) for k in ("AMIX_ZZ9000_KIT", "AMIX_HYDRA_KIT")}
        try:
            from amix_installer import kernel
            if not (kernel.find_kit("zz9000") or kernel.find_kit("hydra")):
                self.assertIn("no kernel kit for ZZ9000 and HydraNet", a.missing)
                self.assertIn("developer/amix/zz9000-kit", a.missing)
        finally:
            for k, v in old.items():
                if v:
                    os.environ[k] = v
        a = plan.DriverKernel(self.src, ["va2000"], kern="k040")
        self.assertIn("stock 68030 kernel only", a.missing)


class PatchLevel(Base):
    def test_unpatched_core_needs_the_patch_disk(self):
        c = self.cfg()
        c.source.patch = None
        pl = plan.make_plan(c, self.paths())
        self.assertIn("Patch Disk 1-2a", " ".join(pl.problems))
        bad = os.path.join(self.d, "bad.adf")
        with open(bad, "wb") as f:
            f.write(b"\0" * 2048)
        pl = plan.make_plan(c, dict(self.paths(), patch_disk=bad))
        self.assertIn("is not the AmigaUNIX 2.1 patch disk", " ".join(pl.problems))
        good = os.path.join(self.d, "patch.adf")
        with open(good, "wb") as f:
            f.write(b'#!/sbin/sh\necho "This contains Patch Disks 1 and 2 ..."\n'
                    b'exec /var/patch/apply\n'.ljust(1024, b"#") + b"\0" * 1024)
        pl = plan.make_plan(c, dict(self.paths(), patch_disk=good))
        self.assertFalse([p for p in pl.problems if "Patch Disk" in p])

    def test_picasso_kernel_needs_the_patched_core(self):
        c = self.cfg(mach="uae", accel="none", kern="stock", boot="pvmk", gfx="picasso2")
        c.source.patch = None
        a = plan.DriverKernel(self.src, ["rtgfb"], patch_missing=True)
        self.assertIn("Patch Disk 1-2a", a.missing)
