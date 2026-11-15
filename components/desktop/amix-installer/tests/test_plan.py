import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from amix_installer import hdf, model, plan, safety  # noqa: E402


def fake_source(path, found=(), pvmk=None, movable=0):
    s = hdf.SourceInfo()
    s.path = path
    s.root_used = 300 << 20
    s.root_avail = 900 << 20
    s.found = set(found)
    s.pvmk = pvmk
    s.movable = {"/usr/local": movable}
    s.packages = [("core", "Amiga Unix essential components", "")]
    return s


class Base(unittest.TestCase):
    def setUp(self):
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
        self.assertTrue(all(a.missing for a in pl.actions if a.id in ("data-disk", "packages")))

    def test_problems(self):
        pl = plan.make_plan(self.cfg(), {"core_hdf": self.src})
        self.assertTrue(any("folder" in p for p in pl.problems))
        pl = plan.make_plan(self.cfg(target="scsi", mach="a3000", accel="gvp"), self.paths())
        self.assertTrue(any("0.5" in p for p in pl.problems))
        pl = plan.make_plan(model.Config(sel={"src": "run"}), {})
        self.assertTrue(pl.problems)
        c = self.cfg()
        c.source = None
        self.assertTrue(any("not been read" in p for p in plan.make_plan(c, self.paths()).problems))

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
        self.assertEqual(names, ["SHA256SUMS", "amix-installer.json", "core-core.hdf"])

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

    @unittest.skipUnless(plan.find_pvmk(), "no pvmk build here")
    def test_pvmk_disk(self):
        pl = self.only(plan.make_plan(self.cfg(), self.paths()), "pvmk")
        plan.execute(pl)
        with hdf.Image(os.path.join(self.out, "pvmk.hdf")) as im:
            parts = im.read_rdb()
        self.assertEqual([(p.name, p.kind, p.start) for p in parts], [("PVMK", "pvmk", 64)])


if __name__ == "__main__":
    unittest.main()
