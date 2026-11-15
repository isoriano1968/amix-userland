import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from amix_installer import model, profile, safety  # noqa: E402


def opt(gid, oid):
    g = model.find_group(gid)
    return g, g.option(oid)


class Rules(unittest.TestCase):
    def test_defaults_valid(self):
        c = model.Config()
        self.assertEqual(c.normalize(), [])
        for _, g in model.all_groups():
            if g.radio:
                self.assertTrue(c.available(g.option(c.sel[g.id])), g.id)

    def test_ids_unique(self):
        seen = set()
        for _, g in model.all_groups():
            for o in g.options:
                key = (g.radio, o.id) if g.radio else o.id
                self.assertNotIn(key, seen, o.id)
                seen.add(key)
        switch_ids = [o.id for _, g in model.all_groups() if not g.radio for o in g.options]
        self.assertEqual(sorted(switch_ids), sorted(model.DEFAULT_ON))

    def test_needs_tags_exist(self):
        known = set(model.derived_tags({"accel": "z3660", "desk": "cde"}))
        for _, g in model.all_groups():
            if g.id in model.TAG_GROUPS:
                known |= set(o.id for o in g.options)
        for _, g in model.all_groups():
            for o in g.options:
                for alt in o.needs or ():
                    for t in alt:
                        self.assertIn(t, known, "%s needs unknown tag %s" % (o.id, t))

    def test_a2500_moves_card_and_kernel(self):
        c = model.Config()
        c.select("mach", "a2500")
        self.assertEqual(c.sel["accel"], "gvp")
        self.assertEqual(c.sel["kern"], "k040")
        self.assertEqual(c.sel["target"], "scsi")     # no Z3660 SD card
        c.select("accel", "a2630")
        self.assertEqual(c.sel["kern"], "stock")
        self.assertTrue(c.chosen(*opt("accopt", "bigram")))
        self.assertFalse(c.chosen(*opt("drv", "zen")))
        self.assertFalse(c.chosen(*opt("kopt", "vt")))
        self.assertTrue(c.available(opt("drv", "a2065")[1]))
        self.assertEqual(c.sel["cpu"], "m020")

    def test_unavailable_select_refused(self):
        c = model.Config(sel={"mach": "a2500"})
        self.assertFalse(c.select("accel", "z3660"))
        self.assertFalse(c.set_on("gpu", True))

    def test_switch_value_kept_while_unavailable(self):
        c = model.Config()
        c.select("mach", "uae")
        self.assertFalse(c.chosen(*opt("core1", "gpu")))
        c.select("mach", "a3000")
        c.select("accel", "z3660")
        self.assertTrue(c.chosen(*opt("core1", "gpu")))

    def test_running_system_in_place_only(self):
        c = model.Config(sel={"src": "run"})
        self.assertEqual(c.sel["target"], "inplace")
        self.assertNotEqual(c.sel["layout"], "grow")
        self.assertEqual(c.apply_label(), "Apply changes")
        c.select("src", "hdf")
        self.assertEqual(c.sel["target"], "sd")
        self.assertEqual(c.apply_label(), "Create disks")

    def test_console_drops_x_apps(self):
        c = model.Config(sel={"desk": "console"})
        for oid in ("netsurf", "media", "grafx2", "sysmon", "games"):
            self.assertFalse(c.chosen(*opt("apps", oid)), oid)
        c.select("desk", "mwm")
        self.assertTrue(c.chosen(*opt("apps", "netsurf")))
        self.assertFalse(c.chosen(*opt("apps", "sysmon")))

    def test_missing_text(self):
        c = model.Config(sel={"mach": "a2500"})
        self.assertEqual(c.missing(opt("kopt", "mem")[1]), [("z3660",), ("z3opt",)])


class Space(unittest.TestCase):
    def test_split_fits(self):
        disks, fits = model.Config().space()
        self.assertTrue(fits)
        self.assertEqual(len(disks), 2)
        self.assertEqual(disks[0]["add"], 0)

    def test_single_does_not_fit(self):
        c = model.Config(sel={"layout": "single"})
        disks, fits = c.space()
        self.assertFalse(fits)
        self.assertEqual(len(disks), 1)

    def test_grow_fits(self):
        _, fits = model.Config(sel={"layout": "grow"}).space()
        self.assertTrue(fits)

    def test_games_overflow_1g(self):
        c = model.Config(sel={"dsize": "d1g"}, on={"games": True})
        _, fits = c.space()
        self.assertFalse(fits)


class Profiles(unittest.TestCase):
    def test_round_trip(self):
        c = model.Config(sel={"mach": "a2500", "accel": "a2630"}, on={"python": True})
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "p.json")
            profile.save(c, p)
            c2, msgs = profile.load(p)
        self.assertEqual(msgs, [])
        self.assertEqual(c2.sel, c.sel)
        self.assertEqual(c2.on, c.on)

    def test_no_paths_in_profile(self):
        data = profile.dump(model.Config())
        self.assertEqual(set(data), {"format", "version", "choose", "switch"})

    def test_unknown_and_unavailable(self):
        data = profile.dump(model.Config())
        data["choose"]["mach"] = "a2500"
        data["choose"]["accel"] = "z3660"
        data["choose"]["nosuch"] = "x"
        data["switch"]["warp"] = True
        c, msgs = profile.load_data(json.loads(json.dumps(data)))
        self.assertEqual(c.sel["accel"], "gvp")
        self.assertTrue(any("nosuch" in m for m in msgs))
        self.assertTrue(any("warp" in m for m in msgs))
        self.assertTrue(any(m.startswith("accel:") for m in msgs))

    def test_not_a_profile(self):
        with self.assertRaises(ValueError):
            profile.load_data({"format": "other"})


class Safety(unittest.TestCase):
    def test_target_is_source(self):
        with tempfile.TemporaryDirectory() as d:
            src = os.path.join(d, "core.hdf")
            open(src, "w").close()
            link = os.path.join(d, "link.hdf")
            os.symlink(src, link)
            for t in (src, link):
                with self.assertRaises(safety.Refused):
                    safety.check_image_target(src, t)
            safety.check_image_target(src, os.path.join(d, "new.hdf"))
            with self.assertRaises(safety.Refused):
                safety.check_image_target(src, os.path.join(d, "nodir", "new.hdf"))


if __name__ == "__main__":
    unittest.main()
