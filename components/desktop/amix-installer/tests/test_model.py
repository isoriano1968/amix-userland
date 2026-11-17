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
        known |= set(o.id for gid in model.HW_GROUPS for o in model.find_group(gid).options)
        for _, g in model.all_groups():
            for o in g.options:
                for alt in o.needs or ():
                    for t in alt:
                        self.assertIn(t, known, "%s needs unknown tag %s" % (o.id, t))

    def test_further_cards(self):
        """The Machine page ticks every card the Amiga has; the main display
        and the install interface are chosen among them, the others are
        beside them."""
        c = model.Config(sel={"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                              "gfx": "picasso2", "net": "net2065", "desk": "cde"},
                         on={"hw_zz9000": True, "hw_va2000": True, "hw_hydra": True,
                             "hw_xsurf": True})
        self.assertEqual(c.further("gfx"), ["zz9000", "va2000"])
        self.assertEqual(c.further("net"), ["nethydra", "netxsurf"])
        self.assertEqual(c.hardware("gfxhw"), ["hw_picasso2", "hw_zz9000", "hw_va2000"])
        # a ticked ZZ9000 makes its Ethernet port available
        self.assertTrue(c.available(model.find_group("netif").option("hw_zz9000eth")))
        c.set_on("hw_zz9000", False)
        self.assertFalse(c.available(model.find_group("netif").option("hw_zz9000eth")))
        # a card not ticked cannot be the main display
        self.assertFalse(c.available(model.find_group("gfx").option("zz9000")))
        # FS-UAE has no Zorro cards of these: none beside the main ones
        u = model.Config(sel={"mach": "uae"}, on={"hw_hydra": True, "hw_zz9000": True})
        self.assertEqual((u.further("gfx"), u.further("net")), ([], []))
        # the loadable-modules switch is gone: no kernel of ours has a loader
        self.assertIsNone(model.find_group("kopt").option("dlm"))

    def test_a2500_moves_card_and_kernel(self):
        c = model.Config()
        c.select("mach", "a2500")
        self.assertEqual(c.sel["accel"], "gvp")
        self.assertEqual(c.sel["kern"], "k040")
        self.assertEqual(c.sel["target"], "card")     # no Z3660 SD card: ZuluSCSI/BlueSCSI
        self.assertEqual(c.sel["net"], "net2065")
        c.select("accel", "a2630")
        self.assertEqual(c.sel["kern"], "stock")
        self.assertTrue(c.chosen(*opt("memory", "bigram")))
        # its disks on its A2091: what an A2500 comes with
        self.assertEqual(c.sel["diskon"], "on_a2091")
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

    def test_running_system_needs_a_network(self):
        c = model.Config(sel={"mach": "a2500", "accel": "a2630", "net": "nonet"})
        self.assertFalse(c.select("src", "run"))
        c.select("net", "net2065")
        self.assertTrue(c.select("src", "run"))
        self.assertEqual(c.sel["target"], "inplace")
        c = model.Config(sel={"mach": "uae"})
        self.assertEqual(c.sel["net"], "net2065")      # FS-UAE emulates an A2065
        self.assertTrue(c.chosen(*opt("drv", "a2065")) or not c.on["a2065"])
        self.assertTrue(c.available(opt("drv", "a2065")[1]))

    def test_missing_text(self):
        c = model.Config(sel={"mach": "a2500"})
        self.assertEqual(c.missing(opt("kopt", "mem")[1]), [("z3660",), ("z3opt",)])


    def test_machine_page_groups(self):
        """the Machine page: the Amiga, its CPU card and memory, then what it
        has: disk controllers, network interfaces, graphics cards"""
        ids = [g.id for g in model.STEPS[model.step_index("machine")].groups]
        self.assertEqual(ids[:7], ["mach", "accel", "memory", "dctl", "netif", "gfxhw", "net"])
        self.assertEqual(model.find_group("diskon").radio, True)
        for gid in model.HW_GROUPS:
            self.assertFalse(model.find_group(gid).radio)
        # every disk choice stands for a controller switch, and back
        self.assertEqual(sorted(model.DISK_HW.values()),
                         sorted(o.id for o in model.find_group("dctl").options))
        for oid, h in model.DISK_HW.items():
            self.assertEqual(model.find_group("diskon").option(oid).needs, ((h,),))
        # greyed out: not ticked, or not in this machine at all
        c = model.Config()
        on = model.find_group("diskon")
        self.assertEqual(c.why_not(on.option("on_a2091")), model.NOT_TICKED)
        c.select("mach", "a1200")
        self.assertEqual(c.why_not(on.option("on_a2091")), model.NOT_AVAILABLE)
        # a controller without a driver says why, and is marked
        for o in model.find_group("dctl").options:
            self.assertEqual(bool(o.nodriver), o.badge == model.NODRV, o.id)

    def test_new_amiga_new_hardware(self):
        """another Amiga: what it comes with is ticked, the cards of the old
        one are off"""
        c = model.Config()
        c.set_on("hw_xsurf", True)
        c.select("mach", "a2500")
        self.assertEqual(c.hardware("dctl"), ["hw_a2091", "hw_gvpscsi"])
        self.assertFalse(c.on["hw_xsurf"])
        self.assertFalse(c.on["hw_a3000scsi"])
        c.select("mach", "a3000")
        self.assertTrue(c.on["hw_a3000scsi"])
        self.assertFalse(c.on["hw_a2091"])

    def test_amiga_1200(self):
        """experimental: trapdoor cards with an MMU; its IDE and a Blizzard's
        SCSI have no driver yet, the disks are written all the same"""
        c = model.Config()
        self.assertTrue(c.select("mach", "a1200"))
        self.assertEqual(c.sel["accel"], "b1230")
        self.assertEqual(c.sel["kern"], "stock")
        self.assertEqual((c.sel["diskon"], c.sel["target"], c.sel["net"], c.sel["gfx"]),
                         ("on_gayle", "file", "nonet", "chipset"))
        self.assertTrue(c.select("accel", "dkb1240"))
        self.assertEqual(c.sel["kern"], "k040")
        self.assertTrue(c.select("accel", "a1200_060"))
        self.assertIn("060", c.tags())
        for oid in ("bliz_iv", "ec030"):
            o = model.find_group("accel").option(oid)
            self.assertFalse(c.select("accel", oid), oid)
            self.assertIn("1 GiB" if oid == "bliz_iv" else "MMU", c.why_not(o))
        # a Blizzard SCSI kit: the disks can go on it (a ZuluSCSI there too)
        c.set_on("hw_blizscsi", True)
        self.assertTrue(c.select("diskon", "on_blizscsi"))
        self.assertTrue(c.available(model.find_group("target").option("card")))
        # no Zorro slots
        for oid in ("hw_a2091", "hw_a2065", "hw_zz9000", "hw_buddha"):
            self.assertFalse(c.available(model.find_group("dctl").option(oid) or
                                         model.find_group("netif").option(oid) or
                                         model.find_group("gfxhw").option(oid)), oid)
        self.assertFalse(c.available(model.find_group("accel").option("none")))


class Space(unittest.TestCase):
    def test_split_fits(self):
        disks, fits = model.Config().space()
        self.assertTrue(fits)
        self.assertEqual(len(disks), 2)
        self.assertEqual(disks[0]["add"], model.CORE_TOOLS_MB)
        self.assertEqual(disks[0]["reserve"], model.RESERVE_MB)

    def test_stock_core_with_data_disk(self):
        # the stock 469 MB root, 300 MB used: fits with a data disk, not alone
        class Src(object):
            root_used, root_avail = 300 << 20, 159 << 20
            found = set()

            def movable_bytes(self):
                return 0
        _, fits = model.Config(source=Src()).space()
        self.assertTrue(fits)
        # alone it would keep under 100 MB free: the additions go to a data disk
        c = model.Config(sel={"layout": "single"}, on={}, source=Src())
        self.assertEqual(c.sel["layout"], "split")
        self.assertTrue(c.layout_moved)
        self.assertTrue(c.space()[1])

    def test_single_moves_to_a_data_disk(self):
        c = model.Config(sel={"layout": "single"})
        self.assertEqual((c.sel["layout"], c.layout_moved), ("split", True))
        self.assertIn(c.sel["dsize"], ("d1g", "d2g"))
        # chosen again by hand: still on the data disk
        c.select("layout", "single")
        self.assertEqual(c.sel["layout"], "split")

    def test_single_stays_with_room(self):
        class Big(object):
            root_used, root_avail = 300 << 20, 1500 << 20
            found = set()

            def movable_bytes(self):
                return 0
        c = model.Config(sel={"layout": "single"}, source=Big())
        self.assertEqual((c.sel["layout"], c.layout_moved), ("single", False))
        disks, fits = c.space()
        self.assertTrue(fits)
        self.assertEqual(disks[0]["reserve"], 100)

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

    def test_profile_before_the_machine_page(self):
        """an 0.5 profile: its main cards and further cards become ticks"""
        data = {"format": "amix-installer-profile", "version": 1,
                "choose": {"mach": "a3000", "accel": "none", "kern": "stock", "boot": "pvmk",
                           "gfx": "picasso2", "net": "nethydra", "target": "card"},
                "switch": {"more_xsurf": True, "more_zz9000": True, "more_va2000": False}}
        c, msgs = profile.load_data(data)
        self.assertEqual(msgs, [])
        self.assertEqual((c.sel["gfx"], c.sel["net"], c.sel["diskon"]),
                         ("picasso2", "nethydra", "on_a3000scsi"))
        self.assertEqual(c.hardware("gfxhw"), ["hw_picasso2", "hw_zz9000"])
        self.assertEqual(c.hardware("netif"), ["hw_a2065", "hw_hydra", "hw_xsurf"])
        self.assertEqual(c.further("net"), ["net2065", "netxsurf"])
        # the Z3660 SD card target ticks the card and puts the disks there
        c, _ = profile.load_data(dict(data, choose={"mach": "a3000", "accel": "z3660",
                                                    "target": "sd"}, switch={}))
        self.assertEqual(c.sel["diskon"], "on_z3660sd")

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


class Version(unittest.TestCase):
    def test_one_version(self):
        """__version__, the Makefile and the manifest name the same release"""
        import re
        import amix_installer
        top = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        with open(os.path.join(top, "Makefile")) as f:
            mk = re.search(r"^COMPONENT_VERSION = (\S+)", f.read(), re.M).group(1)
        with open(os.path.join(top, "amix-installer.manifest")) as f:
            mf = re.search(r"^version\s+(\S+)", f.read(), re.M).group(1)
        self.assertEqual((amix_installer.__version__, mk), (mf, mf))


class Texts(unittest.TestCase):
    def test_no_markup_in_option_texts(self):
        """Adw rows parse titles and subtitles as markup: a "<" hides the text"""
        bad = [(o.id, t) for _, g in model.all_groups() for o in g.options
               for t in (o.title, o.sub) if t and any(c in t for c in "<>&")]
        self.assertEqual(bad, [])
