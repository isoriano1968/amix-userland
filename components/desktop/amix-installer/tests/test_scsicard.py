import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from amix_installer import model, plan, scsicard  # noqa: E402
from test_plan import Base  # noqa: E402
import hoststubs  # noqa: E402

hoststubs.use()

IDS = {"core": 6, "pvmk": 5, "data": 1}
NAMES = {"core": "HD60_512.hda", "pvmk": "HD50_512.hda", "data": "HD10_512.hda"}

# as a user's card has it: the global section with a preset, a CD at ID 3
ZULU_INI = """# See http://zuluscsi.com/manual
[SCSI]
System = "Mac"   # a preset with Apple quirks
#Dir = "/images"
EnableSCSI2 = 1

[SCSI3]
Product = "CD-ROM Drive"
Type = 2
IMG0 = FirstCD.iso

[scsi6]
Product = "old settings, no image"
"""

# as the firmware's sample: an AmigaOS card with units 0, 1, 3 and 4
Z3660_CFG = """## Z3660 config file ##
bootmode CPU
cpufreq 50
kickstart5 kicks/A3kOS322.rom
kickstart 5
scsiboot NO
hdf0 hdf/a4000.hdf
hdf1 hdf/Work.hdf
hdf2 "hdf/My Programs.hdf"
iso6 iso/AmigaOS39.iso
scsi0 0
scsi1 1
scsi3 2
scsi4 6
autoconfig_rtg NO
"""


def touch(*parts):
    p = os.path.join(*parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write("x")


class Zulu(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def card(self, ini=ZULU_INI, name="zuluscsi.ini"):
        if ini is not None:
            with open(os.path.join(self.d, name), "w") as f:
                f.write(ini)
        return scsicard.read_card(self.d)

    def test_ini(self):
        i = scsicard.Ini(ZULU_INI)
        self.assertEqual(i.section("scsi")["system"], "Mac")
        self.assertEqual(i.section("SCSI3")["img0"], "FirstCD.iso")
        self.assertEqual(i.id_section(6)["product"], "old settings, no image")
        self.assertIsNone(i.section("SCSI5"))

    def test_ids_in_use(self):
        touch(self.d, "HD00_512.hda")
        touch(self.d, "HD2 Workbench.hda")
        touch(self.d, "zululog.txt")
        c = self.card()
        self.assertEqual(c.kind, "ZuluSCSI")
        self.assertEqual(sorted(c.used), [0, 2, 3])
        self.assertIn("SCSI IDs in use: 0, 2, 3", c.summary())
        self.assertEqual(scsicard.conflicts(c, IDS, NAMES), [])

    def test_taken_ids_stop(self):
        touch(self.d, "HD6 Amiga.hda")          # someone else's disk at our core's ID
        touch(self.d, "HD7_512.hda")            # at the host adapter's ID
        p = scsicard.conflicts(self.card(), IDS, NAMES)
        self.assertTrue(any("SCSI ID 6 (the AMIX core disk) is taken" in x for x in p), p)
        self.assertTrue(any("ID 7, the Amiga's host adapter" in x for x in p), p)
        # our own image from an earlier run is not a conflict
        os.remove(os.path.join(self.d, "HD6 Amiga.hda"))
        touch(self.d, "HD60_512.hda")
        p = scsicard.conflicts(self.card(), IDS, NAMES)
        self.assertFalse(any("ID 6" in x for x in p), p)

    def test_image_folders(self):
        c = self.card('[SCSI]\nDir = "/images"\n')
        self.assertEqual(c.image_dir(), "images")
        b = self.card('[SCSI]\nImgDir = "amiga"\n', "bluescsi.ini")
        self.assertIn("both", " ".join(b.problems))        # the zulu one is still there
        os.remove(os.path.join(self.d, "zuluscsi.ini"))
        b = scsicard.read_card(self.d)
        self.assertEqual((b.kind, b.image_dir()), ("BlueSCSI", "amiga"))

    def test_ini_with_amix(self):
        c = self.card()
        new = scsicard.ini_with_amix(c, IDS, NAMES)
        n = scsicard.Ini(new)
        # the user's lines stay: the preset, the CD
        self.assertIn('System = "Mac"   # a preset with Apple quirks', new)
        self.assertEqual(n.section("SCSI3")["img0"], "FirstCD.iso")
        # ours: a fixed disk without the preset's quirks; the old [scsi6] replaced
        self.assertEqual(n.id_section(6)["product"], "AMIX CORE DISK")
        self.assertEqual((n.id_section(6)["type"], n.id_section(6)["quirks"]), ("0", "0"))
        self.assertEqual(n.id_section(5)["product"], "AMIX PVMK BOOT")
        self.assertEqual(new.lower().count("[scsi6]"), 1)
        self.assertEqual(scsicard.replaced_sections(c, IDS), ["SCSI6"])
        # a second run gives the same file
        with open(os.path.join(self.d, "zuluscsi.ini"), "w") as f:
            f.write(new)
        self.assertEqual(scsicard.ini_with_amix(scsicard.read_card(self.d), IDS, NAMES), new)


class Z3660(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        os.mkdir(os.path.join(self.d, "hdf"))

    def tearDown(self):
        self.tmp.cleanup()

    def card(self, text=Z3660_CFG, folder="hdf"):
        with open(os.path.join(self.d, "z3660cfg.txt"), "w") as f:
            f.write(text)
        return scsicard.read_z3660(os.path.join(self.d, folder))

    def names(self):
        return {"core": "amix-core.hdf", "pvmk": "pvmk.hdf", "data": "amix-data.hdf"}

    def test_read(self):
        z = self.card()
        self.assertEqual(z.root, self.d)                    # found one folder up
        self.assertEqual(z.slots[2], "hdf/My Programs.hdf")
        self.assertEqual(z.units, {0: 0, 1: 1, 3: 2, 4: 6})
        self.assertIs(z.scsiboot, False)
        self.assertEqual(self.card(folder=".").root, self.d)   # or the root itself

    def test_unit_taken(self):
        # AmigaOS's Work.hdf is unit 1, where AMIX's data disk goes
        p = scsicard.z3660_conflicts(self.card(), IDS, self.names())
        self.assertEqual(len(p), 1)
        self.assertIn("SCSI unit 1 (the AMIX data disk) is hdf/Work.hdf", p[0])
        # without a data disk it fits
        n = self.names()
        n.pop("data")
        self.assertEqual(scsicard.z3660_conflicts(self.card(), IDS, n), [])

    def test_with_amix(self):
        cfg = Z3660_CFG.replace("scsi1 1\n", "")
        z = self.card(cfg)
        text, slots, boot = scsicard.z3660_with_amix(z, IDS, self.names())
        # free slots: 3, 4, 5 (0-2 and iso6 are the user's)
        self.assertEqual(slots, {"data": 3, "pvmk": 4, "core": 5})
        for line in ("hdf3 hdf/amix-data.hdf", "hdf5 hdf/amix-core.hdf", "scsi6 5", "scsi5 4",
                     "scsi1 3", "scsiboot YES", "iso6 iso/AmigaOS39.iso", "scsi4 6",
                     'hdf2 "hdf/My Programs.hdf"', "autoconfig_rtg NO"):
            self.assertIn(line, text.splitlines())
        self.assertTrue(boot)
        self.assertNotIn("scsiboot NO", text)
        # a second run keeps the same slots and gives the same file
        with open(os.path.join(self.d, "z3660cfg.txt"), "w") as f:
            f.write(text)
        again = scsicard.z3660_with_amix(scsicard.read_z3660(self.d), IDS, self.names())
        self.assertEqual(again[0], text)
        self.assertFalse(again[2])


class Plans(Base):
    """make_plan with a card as the output folder."""

    @hoststubs.needs_kernel

    def test_zulu_card(self):
        with open(os.path.join(self.out, "zuluscsi.ini"), "w") as f:
            f.write(ZULU_INI)
        pl = plan.make_plan(self.cfg(target="card", mach="a2500", accel="gvp"), self.paths())
        self.assertEqual(pl.problems, [])
        cc = [a for a in pl.actions if a.id == "card-config"]
        self.assertEqual(len(cc), 1)
        self.assertIn("[SCSI1], [SCSI5], [SCSI6]", cc[0].detail)
        self.assertIn("replaces [SCSI6]", cc[0].detail)
        ids = [a.id for a in pl.actions]
        self.assertLess(ids.index("copy-core"), ids.index("card-config"))
        # a disk of someone else's at ID 6 stops the plan before anything is written
        touch(self.out, "HD6_512 MacOS.hda")
        pl = plan.make_plan(self.cfg(target="card", mach="a2500", accel="gvp"), self.paths())
        self.assertTrue(any("SCSI ID 6" in p for p in pl.problems), pl.problems)

    def test_card_config_writes(self):
        p = os.path.join(self.out, "zuluscsi.ini")
        with open(p, "w") as f:
            f.write(ZULU_INI)
        card = scsicard.read_card(self.out)
        new = scsicard.ini_with_amix(card, IDS, NAMES)
        a = plan.CardConfig(p, ZULU_INI, new, "t", "d")
        ctx = plan.Context(self.out)
        a.run(ctx)
        with open(p) as f:
            self.assertEqual(f.read(), ZULU_INI)     # not in place before the commit
        plan.commit(ctx.pending, "backup")          # its own copy is the backup
        with open(p) as f:
            self.assertEqual(f.read(), new)
        with open(p + ".amix-installer") as f:
            self.assertEqual(f.read(), ZULU_INI)
        # changed under it: refused, nothing written
        b = plan.CardConfig(p, ZULU_INI, "x", "t", "d")
        with self.assertRaises(Exception):
            b.run(plan.Context(self.out))
        with open(p) as f:
            self.assertEqual(f.read(), new)

    def test_z3660_card_needs_autoconfig(self):
        os.mkdir(os.path.join(self.out, "hdf"))
        cfgtext = Z3660_CFG.replace("scsi1 1\n", "")
        with open(os.path.join(self.out, "z3660cfg.txt"), "w") as f:
            f.write(cfgtext)
        hdfdir = dict(self.paths(), out_dir=os.path.join(self.out, "hdf"))
        # Commodore's boot, autoconfig_rtg NO: AMIX would not find the SD card
        pl = plan.make_plan(self.cfg(target="sd", boot="amixboot", kern="k040"), hdfdir)
        self.assertTrue(any("autoconfig_rtg YES" in p for p in pl.problems), pl.problems)
        with open(os.path.join(self.out, "z3660cfg.txt"), "w") as f:
            f.write(cfgtext.replace("autoconfig_rtg NO", "autoconfig_rtg YES"))
        pl = plan.make_plan(self.cfg(target="sd", boot="amixboot", kern="k040"), hdfdir)
        self.assertFalse(any("autoconfig_rtg" in p for p in pl.problems), pl.problems)

    @hoststubs.needs_kernel

    def test_z3660_card(self):
        os.mkdir(os.path.join(self.out, "hdf"))
        with open(os.path.join(self.out, "z3660cfg.txt"), "w") as f:
            f.write(Z3660_CFG.replace("scsi1 1\n", ""))
        pl = plan.make_plan(self.cfg(target="sd"), dict(self.paths(), out_dir=os.path.join(
            self.out, "hdf")))
        self.assertEqual(pl.problems, [])
        self.assertEqual(pl.paths["out_dir"], os.path.join(self.out, "hdf"))
        cc = [a for a in pl.actions if a.id == "card-config"][0]
        self.assertIn("unit 6 = hdf", cc.detail)
        self.assertIn("scsiboot YES", cc.detail)


if __name__ == "__main__":
    unittest.main()
