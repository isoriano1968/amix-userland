import json
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
        # the 68040/060 kernel tools as a stand-in: tests never depend on what
        # the build machine has (amix-kernel-040, m68k ELF binutils)
        from amix_installer import kernel
        k040 = os.path.join(self.d, "kernel-040")
        os.mkdir(k040)
        with open(os.path.join(k040, kernel.K040_TOOL), "w") as f:
            f.write("#!/bin/sh\n# stand-in: takes --root-card --root-unit --root-stock\nexit 1\n")
        self._k040 = (kernel.find_kernel040, kernel.find_elf_tools)
        kernel.find_kernel040 = lambda: k040
        kernel.find_elf_tools = lambda kit=None: "/bin"
        self.src = os.path.join(self.d, "core.hdf")
        with open(self.src, "wb") as f:
            f.write(os.urandom(3 << 20) + b"\0" * (1 << 20))
        self.out = os.path.join(self.d, "out")
        os.mkdir(self.out)

    def tearDown(self):
        from amix_installer import kernel
        kernel.find_kernel040, kernel.find_elf_tools = self._k040
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
        repo = os.path.join(self.d, "repo")
        good_repo(repo)
        pl = plan.make_plan(model.Config(sel={"src": "run", "mach": "a2500", "accel": "gvp"}),
                            {"repo": repo})
        self.assertEqual([a.id for a in pl.actions], ["install"])
        self.assertTrue(pl.actions[0].available)
        self.assertEqual(pl.problems, [])
        old = os.environ.get("AMIX_REPO")
        os.environ["AMIX_REPO"] = ""
        try:
            nr = plan.make_plan(model.Config(sel={"src": "run", "mach": "a2500", "accel": "gvp"}),
                                {}, fsuae.FsUae())
            if plan.default_repo() is None:
                self.assertIn(plan.REPO_PKG, nr.actions[0].missing)
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

    def test_existing_output_listed(self):
        open(os.path.join(self.out, "core-core.hdf"), "w").close()
        pl = plan.make_plan(self.cfg(), self.paths())
        self.assertIn(("core-core.hdf", False), pl.existing)
        # a hand-made file of that name: asked about first, never silently
        self.assertTrue(any("not written by the installer" in p for p in pl.problems))

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
        # a journal of 0.4.6 also names finished files: only .part files count
        self.assertEqual(plan.leftover(self.out), ["core-core.hdf.part"])
        plan.clean_leftover(self.out)
        self.assertEqual(os.listdir(self.out), [])

    def write_old(self, *names, ours=False):
        for n in names:
            with open(os.path.join(self.out, n), "w") as f:
                f.write("old " + n)
        if ours:
            with open(os.path.join(self.out, "amix-installer.json"), "w") as f:
                json.dump({"files": dict((n, "0" * 64) for n in names)}, f)

    def read(self, n):
        with open(os.path.join(self.out, n)) as f:
            return f.read()

    def test_overwrite_replaces_at_the_end(self):
        self.write_old("core-core.hdf", ours=True)
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core", "sums")
        self.assertEqual(pl.problems, [])
        self.assertIn(("core-core.hdf", True), pl.existing)
        plan.execute(pl)
        self.assertEqual(plan.sha256_file(os.path.join(self.out, "core-core.hdf")),
                         plan.sha256_file(self.src))
        self.assertFalse([n for n in os.listdir(self.out) if n.endswith((".bak", ".part"))])

    def test_backup_keeps_the_old_files(self):
        self.write_old("core-core.hdf", "README-amix-installer.txt")
        pl = self.only(plan.make_plan(self.cfg(existing="backup"), self.paths()),
                       "copy-core", "sums")
        self.assertEqual(pl.problems, [])
        plan.execute(pl)
        baks = sorted(n for n in os.listdir(self.out) if n.endswith(".bak"))
        self.assertEqual(len(baks), 2)
        self.assertTrue(baks[0].startswith("README-amix-installer.txt."))
        self.assertEqual(self.read(baks[1]), "old core-core.hdf")
        self.assertRegex(baks[1], r"^core-core\.hdf\.[0-9]{8}-[0-9]{6}\.bak$")

    def test_foreign_file_needs_confirmation(self):
        self.write_old("core-core.hdf")              # a hand-made disk of that name
        pl = plan.make_plan(self.cfg(), self.paths())
        self.assertTrue(any("core-core.hdf" in p and "not written by the installer" in p
                            for p in pl.problems), pl.problems)
        ok = plan.make_plan(self.cfg(), dict(self.paths(), overwrite_confirmed=True))
        self.assertEqual(ok.problems, [])
        self.assertEqual(plan.make_plan(self.cfg(existing="backup"), self.paths()).problems, [])

    def test_failed_run_leaves_old_files(self):
        self.write_old("core-core.hdf", ours=True)

        class Boom(plan.Action):
            def __init__(self):
                super(Boom, self).__init__("boom", "fails", "", 1, [])

            def run(self, ctx):
                raise IOError("disk full")
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "copy-core")
        pl.actions.append(Boom())
        with self.assertRaises(IOError):
            plan.execute(pl)
        self.assertEqual(self.read("core-core.hdf"), "old core-core.hdf")
        self.assertEqual(sorted(os.listdir(self.out)), ["amix-installer.json", "core-core.hdf"])

    def test_source_is_never_written(self):
        pl = plan.make_plan(self.cfg(existing="backup"), dict(self.paths(), out_dir=self.d))
        self.assertNotIn(self.src, [os.path.join(self.d, n) for a in pl.actions
                                    for n in a.outputs])
        os.symlink(self.src, os.path.join(self.out, "core-core.hdf"))
        pl = plan.make_plan(self.cfg(existing="backup"), self.paths())
        self.assertTrue(any("is the source image" in p for p in pl.problems), pl.problems)

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



def good_repo(d, leave_out=()):
    """A package repository with the foundation packages (but leave_out)."""
    import urllib.parse
    from amix_installer import netinstall
    os.makedirs(os.path.join(d, "publisher", "amix", "pkg"))
    with open(os.path.join(d, "pkg5.repository"), "w") as f:
        f.write("[publisher]\nprefix = amix\n")
    for stem in sorted(set(netinstall.BOOT_FILES) | set(netinstall.FOUNDATION_PKGS)):
        if stem in leave_out:
            continue
        pd = os.path.join(d, "publisher", "amix", "pkg", urllib.parse.quote(stem, safe=""))
        os.makedirs(pd)
        with open(os.path.join(pd, "1%2C2.1-1%3A20261118T000000Z"), "w") as f:
            f.write("set name=pkg.fmri value=pkg://amix/%s@1,2.1-1:20261118T000000Z\n" % stem)


class Repository(unittest.TestCase):
    """Where the package repository comes from, and what makes one usable."""

    def test_problem(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIn("not a package repository", plan.repo_problem(d))
            good = os.path.join(d, "good")
            good_repo(good)
            self.assertIsNone(plan.repo_problem(good))
            old = os.path.join(d, "old")
            good_repo(old, leave_out=("sysutils/setclk",))
            self.assertIn("older than this installer: it lacks sysutils/setclk",
                          plan.repo_problem(old))

    def test_installed_package_found(self):
        """A pkg-installed installer finds the repository the package
        desktop/amix-installer/repository put next to it, past unusable ones."""
        with tempfile.TemporaryDirectory() as d:
            old, share = os.path.join(d, "old"), os.path.join(d, "share")
            good_repo(old, leave_out=("sysutils/setclk",))
            good_repo(share)
            saved, env = plan.repo_candidates, os.environ.pop("AMIX_REPO", None)
            plan.repo_candidates = lambda: [os.path.join(d, "none"), old, share]
            try:
                self.assertEqual(plan.default_repo(), share)
                plan.repo_candidates = lambda: [old]
                self.assertIsNone(plan.default_repo())
            finally:
                plan.repo_candidates = saved
                if env is not None:
                    os.environ["AMIX_REPO"] = env

    def test_candidates_end_at_the_prefix_share(self):
        self.assertTrue(plan.repo_candidates()[-1].endswith(
            os.path.join("share", "amix-installer", "repo")))

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
        # ZZ9000 Ethernet as the install interface says the ZZ9000 is there; with
        # the card unticked its port is not offered
        self.assertEqual(self.cfg(gfx="picasso2", net="netzz9000").sel["net"], "netzz9000")
        n = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "picasso2", "net": "netzz9000"}, on={"hw_zz9000": False})
        self.assertNotEqual(n.sel["net"], "netzz9000")
        self.assertEqual(plan.kernel_drivers(z.sel), ["zz9000", "zz9000eth"])
        # every card of a machine with several: graphics first, no driver twice
        m = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "zz9000", "net": "nethydra"},
                         on={"hw_picasso2": True, "hw_xsurf": True, "hw_zz9000eth": True,
                             "hw_hydra": True})
        self.assertEqual(plan.kernel_drivers(m), ["zz9000", "rtgfb", "hydra", "xsurf100",
                                                  "zz9000eth"])
        # the VA2000 has a kit for both kernel lines, and X has no va2000
        # driver yet: no X desktop with it
        v = self.cfg(accel="mercury", kern="k040", gfx="va2000", desk="cde")
        self.assertEqual(v.sel["gfx"], "va2000")
        self.assertNotIn(v.sel["desk"], ("cde", "mwm"))
        self.assertEqual(self.cfg(gfx="va2000").sel["gfx"], "va2000")
        z = self.cfg(accel="z3660", kern="z3opt", gfx="va2000")
        self.assertEqual(z.sel["kern"], "z3opt")
        self.assertNotEqual(z.sel["gfx"], "va2000")

    def test_scsi_controllers_and_devices(self):
        a3 = {"mach": "a3000", "accel": "z3660", "target": "sd"}
        self.assertEqual(plan.scsi_controllers(a3), ["the A3000's own SCSI",
                                                     "the Z3660's SD card"])
        # the Z3660 is controller 1: unit u is c(8+u), written in hex (c8-ce)
        self.assertEqual(plan.disk_device(a3, "core", 1), "/dev/rdsk/ced0s1")
        self.assertEqual(plan.disk_device(a3, "data", 1, raw=False), "/dev/dsk/c9d0s1")
        old = dict(plan.SCSI_ID)
        try:
            plan.SCSI_ID["core"] = 0
            self.assertEqual(plan.disk_device(a3, "core", 1), "/dev/rdsk/c8d0s1")
        finally:
            plan.SCSI_ID.clear()
            plan.SCSI_ID.update(old)
        # every name AMIX has: c0-c7 for card 0, c8-cf for card 1, never c1x
        for card in (0, 1):
            for unit in range(7):
                n = "c%xd0s1" % ((card << 3) | unit)
                self.assertRegex(n, r"^c[0-9a-f]d0s1$")
        # in FS-UAE every disk is on the emulated A3000 SCSI
        self.assertEqual(plan.disk_device(a3, "data", 1, fs_uae=True), "/dev/rdsk/c1d0s1")
        # a ZuluSCSI on the Amiga's own bus, or HDF files: controller 0
        for t in ("card", "file"):
            self.assertEqual(plan.disk_device(dict(a3, target=t), "core", 1),
                             "/dev/rdsk/c6d0s1")
        self.assertEqual(plan.scsi_controllers({"mach": "a2500", "accel": "gvp"}),
                         ["the A2091", "the GVP card's SCSI"])

    def test_amiga_4000(self):
        """no stock kernel, no ZuluSCSI; the Z3660 is controller 0"""
        c = model.Config(sel={"mach": "a4000", "accel": "z3660", "kern": "stock", "target": "card",
                              "net": "nethydra"})
        self.assertEqual(c.sel["mach"], "a4000")
        self.assertNotEqual(c.sel["kern"], "stock")
        self.assertNotEqual(c.sel["target"], "card")
        self.assertEqual(c.sel["net"], "nethydra")               # hydra-040 kit
        c = model.Config(sel=dict(c.sel, kern="k040"))
        # Zorro II cards in the A4000: ticked, then main cards or beside others
        c.set_on("hw_picasso2", True)
        c.set_on("hw_xsurf", True)
        for gid, oid in (("net", "nethydra"), ("gfx", "picasso2"), ("netif", "hw_hydra"),
                         ("gfxhw", "hw_picasso2")):
            self.assertTrue(c.available(model.find_group(gid).option(oid)), oid)
        self.assertEqual(c.further("net"), ["netzen", "netxsurf"])     # and the Z3660's port
        self.assertEqual(c.why_not(model.find_group("target").option("card")),
                         model.NOT_AVAILABLE)
        self.assertTrue(c.available(model.find_group("net").option("netxsurf")))
        a = model.Config(sel={"mach": "a4000", "accel": "a3640", "kern": "k040"})
        self.assertEqual((a.sel["accel"], a.sel["kern"]), ("a3640", "k040"))
        # an A4000 comes without an A2065: ticked, the install runs over it
        self.assertEqual(a.sel["net"], "nonet")
        self.assertEqual(a.sel["diskon"], "on_gayle")             # its IDE: no driver yet
        a.set_on("hw_a2065", True)
        self.assertTrue(a.select("net", "net2065"))
        sd = {"mach": "a4000", "accel": "z3660", "target": "sd"}
        self.assertEqual(plan.scsi_controllers(sd), ["the Z3660's SD card"])
        self.assertEqual(plan.disk_device(sd, "core", 1), "/dev/rdsk/c6d0s1")   # stock rootdev
        self.assertEqual(plan.disk_device(sd, "data", 1), "/dev/rdsk/c1d0s1")

    def test_040_kernels(self):
        """our 68040/060 kernels are made here; the port kernel takes the Z3660's
        SD card, RTG and Ethernet from kits, the Z3660 kernel has them built in"""
        k = {"mach": "a3000", "accel": "z3660", "kern": "k040", "target": "sd",
             "net": "netzen", "gfx": "rtg"}
        self.assertEqual(plan.kernel_drivers(k), ["z3660scsi", "z3660rtg", "z3660eth"])
        # the ZZ9000 beside it, from its 040 kit
        self.assertEqual(plan.kernel_drivers(dict(k, gfx="zz9000")),
                         ["z3660scsi", "zz9000", "z3660eth"])
        self.assertTrue(model.Config(sel=dict(k, gfx="zz9000")).sel["gfx"] == "zz9000")
        self.assertTrue(plan.kernel_built(k))
        z = dict(k, kern="z3opt")
        self.assertEqual(plan.kernel_drivers(z), [])
        self.assertTrue(plan.kernel_built(z))
        self.assertFalse(plan.kernel_built({"kern": "stock", "gfx": "chipset"}))
        pl = plan.make_plan(self.cfg(**k), self.paths())
        dk = [a for a in pl.actions if a.id == "kernel"][0]
        self.assertEqual((dk.line, dk.variant, dk.root), ("040", "generic", (1, 6)))
        self.assertIn("root on ced0s1", dk.detail)
        self.assertTrue(dk.name.startswith("unix.k040+z3660scsi"))

    def test_pvmk_stock_entry(self):
        """a 68040/060 kernel gets no "AMIX stock" entry: the stock 68030
        kernel does not run there"""
        k = model.Config(sel={"mach": "a3000", "accel": "z3660", "kern": "k040", "boot": "pvmk"})
        self.assertNotIn("AMIX stock", plan.pvmk_conf(k))
        self.assertIn("z3reloc=yes", plan.pvmk_conf(k))
        s = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "picasso2"})
        self.assertIn("entry=AMIX stock,UNIX_Root:/stand/unix.stock,native", plan.pvmk_conf(s))

    def test_third_controller_refused(self):
        """dd/sd takes two controllers: the disks on the third one stop the
        plan, a third one beside them is only noted"""
        c = self.cfg(accel="gvp", target="file")
        c.set_on("hw_a2091", True)
        self.assertEqual(plan.scsi_controllers(c), ["the A3000's own SCSI", "the A2091",
                                                    "the GVP card's SCSI"])
        self.assertTrue(c.select("diskon", "on_gvpscsi"))
        pl = plan.make_plan(c, self.paths())
        self.assertTrue(any("takes 2 controllers, this machine has 3" in p and
                            "the GVP card's SCSI, where AMIX's disks are, is dropped" in p
                            for p in pl.problems), pl.problems)
        c.select("diskon", "on_a3000scsi")
        pl = plan.make_plan(c, self.paths())
        self.assertFalse(any("controllers" in p for p in pl.problems), pl.problems)
        d = [a for a in pl.actions if a.id == "dropped-controller"]
        self.assertEqual(len(d), 1)
        self.assertFalse(d[0].available)

    def test_disks_without_a_driver(self):
        """AMIX's disks on a controller only amigaux 1.1 drives: written, with a
        step in the plan that says AMIX does not boot from them yet"""
        c = Base.cfg(self, mach="a4000", accel="a3640", kern="k040", boot="pvmk")
        self.assertEqual(c.sel["diskon"], "on_gayle")
        pl = plan.make_plan(c, self.paths())
        self.assertEqual(pl.problems, [])
        d = [a for a in pl.actions if a.id == "disk-driver"]
        self.assertEqual(len(d), 1)
        self.assertFalse(d[0].available)
        self.assertIn("gayle", d[0].missing)
        self.assertIn("copy-core", [a.id for a in pl.actions])
        # with an A2091 ticked the disks go there, with its driver
        c.set_on("hw_a2091", True)
        c.select("diskon", "on_a2091")
        pl = plan.make_plan(c, self.paths())
        self.assertFalse([a for a in pl.actions if a.id == "disk-driver"])
        self.assertEqual(plan.disk_device(c, "core", 1), "/dev/rdsk/c6d0s1")

    def test_other_cards_need_a_kit_kernel(self):
        """the Z3660 kernel takes no kits: cards beside the main ones get their
        drivers with the stock and 68040/060 port kernels only"""
        c = model.Config(sel={"mach": "a3000", "accel": "z3660", "kern": "z3opt"},
                         on={"hw_xsurf": True, "hw_zz9000": True})
        self.assertEqual(plan.kernel_drivers(c), [])
        c.select("kern", "k040")
        self.assertEqual(plan.kernel_drivers(c), ["z3660scsi", "z3660rtg", "zz9000", "xsurf100",
                                                  "z3660eth"])

    def test_buddha(self):
        """the Buddha and Catweasel Z-II: the buddha kit with the stock and port
        kernels, the disks at the driver's fixed units (port 0 master 6,
        slave 5; port 1 master 3)"""
        from amix_installer import kernel
        old, kernel.find_stamp = kernel.find_stamp, lambda: "/bin/true"   # kit-tools' stand-in
        self.addCleanup(setattr, kernel, "find_stamp", old)
        # the kit package (or an amix-gate checkout) need not be here: a stand-in
        oldkit, real = kernel.find_kit, kernel.find_kit
        kernel.find_kit = lambda drv="rtgfb", line="030": (
            "/nonexistent/buddha-kit" if getattr(drv, "id", drv) == "buddha" else real(drv, line))
        self.addCleanup(setattr, kernel, "find_kit", oldkit)
        c = Base.cfg(self, mach="a3000", accel="none", kern="stock", boot="pvmk")
        c.set_on("hw_buddha", True)
        self.assertTrue(c.select("diskon", "on_buddha"))
        self.assertEqual(plan.scsi_controllers(c), ["the A3000's own SCSI", "the Buddha"])
        self.assertEqual(plan.kernel_drivers(c), ["buddha"])
        self.assertEqual(plan.disk_device(c, "core", 1), "/dev/rdsk/ced0s1")
        self.assertEqual(plan.disk_device(c, "data", 1), "/dev/rdsk/cbd0s1")
        self.assertEqual(plan.disk_device(c, "data", 1, fs_uae=True), "/dev/rdsk/c1d0s1")
        pl = plan.make_plan(c, self.paths())
        self.assertEqual(pl.problems, [])
        self.assertFalse([a for a in pl.actions if a.id == "disk-driver"])
        dk = [a for a in pl.actions if a.id == "kernel"][0]
        self.assertEqual((dk.line, dk.root), ("030", (1, 6)))
        text = plan.readme(c, {"core": "a-core.hdf", "pvmk": "pvmk.hdf", "data": "a-data.hdf"})
        self.assertIn("IDE port 1 master", text)
        # the Z3660 kernel takes no kits: no disks on the Buddha there
        z = model.Config(sel={"mach": "a3000", "accel": "z3660", "kern": "z3opt"},
                         on={"hw_buddha": True})
        self.assertFalse(z.available(model.find_group("diskon").option("on_buddha")))
        self.assertNotIn("buddha", plan.kernel_drivers(z))
        # with the Z3660 SD card's kit in the port kernel: refused (two tables)
        k = Base.cfg(self, mach="a3000", accel="z3660", kern="k040", boot="pvmk")
        k.set_on("hw_buddha", True)
        self.assertTrue({"z3660scsi", "buddha"} <= set(plan.kernel_drivers(k)))
        pl = plan.make_plan(k, self.paths())
        self.assertTrue(any("cannot be linked into one kernel" in p for p in pl.problems),
                        pl.problems)

    def test_a4091(self):
        """the A4091 and A4092: the a4091 kit with the stock and port kernels,
        the disks at their SCSI IDs on the card's place among dd/sd's controllers"""
        from amix_installer import kernel
        old, kernel.find_stamp = kernel.find_stamp, lambda: "/bin/true"   # kit-tools' stand-in
        self.addCleanup(setattr, kernel, "find_stamp", old)
        oldkit, real = kernel.find_kit, kernel.find_kit
        kernel.find_kit = lambda drv="rtgfb", line="030": (
            "/nonexistent/a4091-kit" if getattr(drv, "id", drv) == "a4091" else real(drv, line))
        self.addCleanup(setattr, kernel, "find_kit", oldkit)
        # an A3000: its own SCSI is card 0, the A4091 card 1
        c = Base.cfg(self, mach="a3000", accel="none", kern="stock", boot="pvmk")
        c.set_on("hw_a4091", True)
        self.assertTrue(c.select("diskon", "on_a4091"))
        self.assertEqual(plan.scsi_controllers(c), ["the A3000's own SCSI", "the A4091"])
        self.assertEqual(plan.kernel_drivers(c), ["a4091"])
        self.assertEqual(plan.disk_device(c, "core", 1), "/dev/rdsk/ced0s1")
        self.assertEqual(plan.disk_device(c, "data", 1), "/dev/rdsk/c9d0s1")
        pl = plan.make_plan(c, self.paths())
        self.assertFalse([p for p in pl.problems if "a4091" in p.lower()], pl.problems)
        self.assertFalse([a for a in pl.actions if a.id == "disk-driver"])
        dk = [a for a in pl.actions if a.id == "kernel"][0]
        self.assertEqual((dk.line, dk.root), ("030", (1, 6)))
        # an A4000 with an A4092 and the port kernel: card 0
        k = Base.cfg(self, mach="a4000", accel="a3640", kern="k040", boot="pvmk")
        k.set_on("hw_a4092", True)
        self.assertTrue(k.select("diskon", "on_a4092"))
        self.assertEqual(plan.kernel_drivers(k)[0], "a4091")
        self.assertEqual(plan.disk_device(k, "core", 1), "/dev/rdsk/c6d0s1")
        # the Z3660 kernel takes no kits: no disks on the A4091 there
        z = model.Config(sel={"mach": "a3000", "accel": "z3660", "kern": "z3opt"},
                         on={"hw_a4091": True})
        self.assertFalse(z.available(model.find_group("diskon").option("on_a4091")))
        self.assertNotIn("a4091", plan.kernel_drivers(z))
        # with the Buddha's kit too: refused (two controller tables)
        b = Base.cfg(self, mach="a4000", accel="a3640", kern="k040", boot="pvmk")
        b.set_on("hw_a4091", True)
        b.set_on("hw_buddha", True)
        self.assertTrue({"a4091", "buddha"} <= set(plan.kernel_drivers(b)))
        pl = plan.make_plan(b, self.paths())
        self.assertTrue(any("the Buddha's and the A4091's drivers cannot be linked" in p
                            for p in pl.problems), pl.problems)

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
                self.assertIn("kernel kit for ZZ9000 and HydraNet", a.missing)
                self.assertIn("developer/amix/zz9000-kit", a.missing)
        finally:
            for k, v in old.items():
                if v:
                    os.environ[k] = v
        # the port kernel: the VA2000's 040 kit; what is missing is named
        a = plan.DriverKernel(self.src, ["va2000"], kern="k040")
        self.assertEqual(a.line, "040")
        self.assertTrue(a.name.startswith("unix.k040+va2000"))
        self.assertTrue(a.missing is None or "stock 68030 kernel only" not in a.missing)
        # HydraNet and the Picasso II have 040 kits too
        h = plan.DriverKernel(self.src, ["rtgfb", "hydra"], kern="k040")
        self.assertTrue(h.name.startswith("unix.k040+"))
        self.assertTrue(h.missing is None or "68030 kernel only" not in h.missing)
        # the Z3660 Ethernet has no 030 kit: refused by line
        z = plan.DriverKernel(self.src, ["z3660eth"], kern="stock")
        self.assertIn("68040/060", z.missing)


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


class SlirpRedir(unittest.TestCase):
    """telnet and ftp into AMIX from this computer: our FS-UAE's slirp
    forwards (uae_slirp_redir, its hostfwd patch of 2026-11-15)."""

    def test_default_and_choices(self):
        from amix_installer import fsuae
        self.assertEqual(fsuae.slirp_redir(), "tcp:2323:23::tcp:2121:21:")
        self.assertEqual(fsuae.slirp_redir(telnet=2424, ftp=0), "tcp:2424:23:")
        self.assertIsNone(fsuae.slirp_redir(0, 0))
        t = plan.FsUaeConfig({"core": "c.hdf"}, "/k.rom", "/out").text()
        self.assertIn("uae_slirp_redir = tcp:2323:23::tcp:2121:21:\n", t)
        self.assertIn("127.0.0.1:2323 (telnet) and :2121 (ftp)", t)
        t = plan.FsUaeConfig({"core": "c.hdf"}, "/k.rom", "/out", telnet=0, ftp=0).text()
        self.assertNotIn("slirp", t)
