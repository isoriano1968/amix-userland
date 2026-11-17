"""ZuluSCSI and BlueSCSI SD cards: what is on a card already, and the
configuration entries for the AMIX disks.

Both cards read an ini file in the card's root (zuluscsi.ini, bluescsi.ini;
BlueSCSI v2 is a fork of the ZuluSCSI firmware and reads the same sections):
[SCSI] for every device, [SCSI<id>] for one SCSI ID.  Disk images are found
by name, HD<id>[<lun>][_<blocksize>]..., in the card's root or in the folders
the ini names (ZuluSCSI Dir, Dir1..Dir9; BlueSCSI ImgDir), or are named per ID
(IMG0..IMG9, BlueSCSI Img0..).

read_card() tells which SCSI IDs a card uses; ini_with_amix() returns the ini
with AMIX's sections for its IDs, everything else as it was.  The installer
never changes another ID's images or settings.

The Z3660's SD card (its second partition, exFAT, the firmware's drive 1:)
has z3660cfg.txt in its root and the disk images in hdf/: slots
"hdf<N> <path>" (N 0-19, or iso<N>; paths relative to the partition) and
units "scsi<K> <N>" (K 0-6: SCSI unit K is slot N); "scsiboot YES" loads
z3660_scsi.rom, so the Amiga boots from those images.  read_z3660() and
z3660_with_amix() do the same for it.
"""
import os
import re

CONFIGS = (("zuluscsi.ini", "ZuluSCSI"), ("bluescsi.ini", "BlueSCSI"))
# the A3000's and A2091's SCSI bus is narrow: IDs 0-7, 7 is the host adapter
HOST_ID = 7
# image file prefixes: hard disk, CD, floppy, magneto-optical, removable,
# tape, Zip
PREFIXES = ("HD", "CD", "FD", "MO", "RE", "TP", "ZP")
_IMAGE = re.compile(r"^(%s)([0-9A-Fa-f])([0-9])?" % "|".join(PREFIXES), re.I)
_NOT_IMAGES = (".ini", ".txt", ".log", ".md", ".json", ".part", ".bak")
MARK = "# written by the AMIX Installer"
# what AMIX's disks are called in the card's INQUIRY answer
ROLE_PRODUCT = {"core": "AMIX CORE DISK", "pvmk": "AMIX PVMK BOOT", "data": "AMIX DATA DISK"}


class Ini(object):
    """An ini file as the cards' minIni reads it: [section], key = value,
    comments from # or ;.  Names are case-insensitive.  The text is kept for
    rewriting."""

    def __init__(self, text):
        self.text = text
        self.sections = []           # [(name, {key: value}, first line, end line)]
        lines = text.splitlines()
        cur, start, vals = None, 0, {}
        for i, raw in enumerate(lines):
            s = raw.strip()
            m = re.match(r"^\[([^\]]+)\]", s)
            if m:
                if cur is not None:
                    self.sections.append((cur, vals, start, i))
                cur, start, vals = m.group(1).strip(), i, {}
                continue
            if not s or s[0] in "#;" or "=" not in s or cur is None:
                continue
            k, v = s.split("=", 1)
            v = v.strip()
            if v.startswith('"'):
                v = v[1:].split('"', 1)[0]
            else:
                v = re.split(r"\s[#;]", v, 1)[0].strip()
            vals[k.strip().lower()] = v
        if cur is not None:
            self.sections.append((cur, vals, start, len(lines)))

    def section(self, name):
        for n, vals, _, _ in self.sections:
            if n.lower() == name.lower():
                return vals
        return None

    def id_section(self, sid):
        """[SCSI<id>]: decimal, or a hexadecimal digit for 10-15."""
        return self.section("SCSI%d" % sid) or \
            (self.section("SCSI%X" % sid) if sid > 9 else None)


class Card(object):
    """What a folder that may be a card's root holds."""

    def __init__(self, root):
        self.root = root
        self.kind = None             # "ZuluSCSI", "BlueSCSI" or None
        self.config = None           # its ini file name, or None
        self.ini = None
        self.dirs = ["."]            # where the card looks for images
        self.used = {}               # SCSI ID -> [what is there]
        self.problems = []

    def image_dir(self):
        """Where AMIX's images go: the first folder the card reads."""
        return self.dirs[0]

    def summary(self):
        if self.kind is None and not self.used:
            return "no card configuration and no disk images here"
        ids = ", ".join("%d" % i for i in sorted(self.used)) or "none"
        what = "%s card (%s)" % (self.kind, self.config) if self.kind else \
            "no zuluscsi.ini or bluescsi.ini"
        return "%s; SCSI IDs in use: %s" % (what, ids)


def _listdir(path):
    try:
        return sorted(os.listdir(path))
    except OSError:
        return []


def read_card(root):
    """Read the card (or folder) root: its ini, its image folders and the
    SCSI IDs its images and sections use."""
    c = Card(root)
    names = _listdir(root)
    lower = dict((n.lower(), n) for n in names)
    found = [(lower[f], k) for f, k in CONFIGS if f in lower]
    if len(found) > 1:
        c.problems.append("%s has both %s: keep the one of your card"
                          % (root, " and ".join(f for f, _ in found)))
    if found:
        c.config, c.kind = found[0]
        try:
            with open(os.path.join(root, c.config), errors="replace") as f:
                c.ini = Ini(f.read())
        except OSError as e:
            c.problems.append("%s cannot be read: %s" % (c.config, e))
    if c.ini is not None:
        glob = c.ini.section("SCSI") or {}
        if c.kind == "BlueSCSI":
            dirs = [glob.get("imgdir")]
        else:
            dirs = [glob.get("dir")] + [glob.get("dir%d" % i) for i in range(1, 10)]
        dirs = [d.strip("/") or "." for d in dirs if d]
        if dirs:
            c.dirs = dirs
    for d in c.dirs:
        for n in _listdir(os.path.join(root, d)):
            if n.startswith(".") or n.lower().endswith(_NOT_IMAGES):
                continue
            m = _IMAGE.match(n)
            if m and os.path.isfile(os.path.join(root, d, n)):
                c.used.setdefault(int(m.group(2), 16), []).append(os.path.normpath(
                    os.path.join(d, n)))
    if c.ini is not None:
        for sid in range(16):
            sec = c.ini.id_section(sid)
            if not sec:
                continue
            imgs = [v for k, v in sorted(sec.items()) if re.match(r"^img[0-9]$", k)]
            if imgs:
                c.used.setdefault(sid, []).extend("%s (in [SCSI%d])" % (i, sid) for i in imgs)
    return c


def conflicts(card, ids, names):
    """Why AMIX's disks cannot go on this card: an ID of ours used by an image
    that is not ours (names: role -> file name), the host adapter's ID."""
    out = list(card.problems)
    ours = set(os.path.normpath(os.path.join(card.image_dir(), n)) for n in names.values())
    for role, sid in sorted(ids.items(), key=lambda kv: kv[1]):
        if role not in names:
            continue
        if sid == HOST_ID:
            out.append("SCSI ID %d is the Amiga's host adapter" % sid)
        other = [u for u in card.used.get(sid, []) if u not in ours]
        if other:
            out.append("SCSI ID %d (the AMIX %s disk) is taken on the card by %s: move it to "
                       "a free ID, or take it off the card" % (sid, role, ", ".join(other)))
    if card.used.get(HOST_ID):
        out.append("the card has an image at SCSI ID %d, the Amiga's host adapter: %s"
                   % (HOST_ID, ", ".join(card.used[HOST_ID])))
    return out


def amix_section(role, sid, name):
    """[SCSI<id>] for one AMIX disk: a fixed disk, 512-byte blocks (also in
    its file name), no host quirks whatever the card's [SCSI] preset says."""
    return ["[SCSI%d]" % sid,
            "%s: the AMIX %s disk, %s" % (MARK, role, name),
            "Type = 0",
            "Quirks = 0",
            'Vendor = "AMIX"',
            'Product = "%s"' % ROLE_PRODUCT[role]]


def ini_with_amix(card, ids, names):
    """The card's ini text with AMIX's [SCSI<id>] sections: earlier ones of
    those IDs (ours from a run before, or settings without an image) are
    replaced, every other line stays."""
    text = card.ini.text if card.ini is not None else ""
    lines = text.splitlines()
    drop = set()
    mine = set(ids.values())
    for name, _, start, end in (card.ini.sections if card.ini is not None else []):
        m = re.match(r"^SCSI([0-9]+|[A-Fa-f])$", name, re.I)
        if not m:
            continue
        sid = int(m.group(1), 16) if len(m.group(1)) == 1 and m.group(1).isalpha() \
            else int(m.group(1))
        if sid in mine:
            drop.update(range(start, end))
    kept = [ln for i, ln in enumerate(lines) if i not in drop]
    while kept and not kept[-1].strip():
        kept.pop()
    out = kept + [""] if kept else []
    for role, sid in sorted(ids.items(), key=lambda kv: kv[1]):
        if role in names:
            out += amix_section(role, sid, names[role]) + [""]
    return "\n".join(out)


def replaced_sections(card, ids):
    """[SCSI<id>] sections of ours that the card has and AMIX's replace."""
    if card.ini is None:
        return []
    return ["SCSI%d" % sid for sid in sorted(ids.values()) if card.ini.id_section(sid)]


# -- the Z3660 SD card ---------------------------------------------------------
Z3660_CONFIG = "z3660cfg.txt"
Z3660_HDF_DIR = "hdf"
Z3660_SLOTS = 20
Z3660_UNITS = 7


class Z3660Card(object):
    """The Z3660 SD card's data partition, as z3660cfg.txt describes it."""

    def __init__(self, root):
        self.root = root             # the partition's root (z3660cfg.txt), or None
        self.text = None
        self.slots = {}              # N -> path of hdf<N> or iso<N>
        self.units = {}              # SCSI unit -> slot
        self.scsiboot = None         # True, False or None (not in the file)
        self.settings = {}           # every other key: its first value
        self.problems = []

    def used(self):
        """SCSI unit -> [the image there]."""
        return dict((k, [self.slots.get(n, "slot %d (empty)" % n)])
                    for k, n in self.units.items())

    def summary(self):
        if self.text is None:
            return "no z3660cfg.txt here or one folder up"
        ids = ", ".join("%d" % k for k in sorted(self.units)) or "none"
        return "Z3660 SD card (z3660cfg.txt); SCSI units in use: %s; scsiboot %s" % (
            ids, {True: "YES", False: "NO", None: "not set"}[self.scsiboot])


def z3660_root(folder):
    """The data partition's root for a chosen folder: the folder itself if it
    has z3660cfg.txt, else its parent (the hdf folder was chosen), else None."""
    folder = os.path.normpath(os.path.abspath(folder))
    for d in (folder, os.path.dirname(folder)):
        if any(n.lower() == Z3660_CONFIG for n in _listdir(d)):
            return d
    return None


def _z3660_words(line):
    s = line.strip()
    if len(s) <= 2 or s[0] in "#/":
        return None
    w = re.findall(r'"[^"]*"|\S+', s)
    return [x.strip('"') for x in w]


def read_z3660(folder):
    root = z3660_root(folder)
    c = Z3660Card(root)
    if root is None:
        return c
    name = next(n for n in _listdir(root) if n.lower() == Z3660_CONFIG)
    try:
        with open(os.path.join(root, name), errors="replace") as f:
            c.text = f.read()
    except OSError as e:
        c.problems.append("%s cannot be read: %s" % (name, e))
        return c
    for line in c.text.splitlines():
        w = _z3660_words(line)
        if not w or len(w) < 2:
            continue
        m = re.match(r"^(hdf|iso)([0-9]+)$", w[0])
        if m and int(m.group(2)) < Z3660_SLOTS:
            c.slots[int(m.group(2))] = w[1]
            continue
        m = re.match(r"^scsi([0-6])$", w[0])
        if m and re.match(r"^-?[0-9]+$", w[1]):
            n = int(w[1])
            if n >= 0:
                c.units[int(m.group(1))] = n
            else:
                c.units.pop(int(m.group(1)), None)
            continue
        if w[0] == "scsiboot":
            c.scsiboot = w[1].upper() == "YES"
            continue
        c.settings[w[0]] = w[1]
    return c


def z3660_conflicts(card, ids, names):
    """Units of ours that the card gives to an image that is not ours."""
    out = list(card.problems)
    ours = set("%s/%s" % (Z3660_HDF_DIR, n) for n in names.values())
    for role, sid in sorted(ids.items(), key=lambda kv: kv[1]):
        if role not in names:
            continue
        if sid >= Z3660_UNITS:
            out.append("the Z3660 has SCSI units 0-%d only, not %d" % (Z3660_UNITS - 1, sid))
            continue
        n = card.units.get(sid)
        if n is not None and card.slots.get(n) not in ours and n in card.slots:
            out.append("SCSI unit %d (the AMIX %s disk) is %s on the Z3660 card (scsi%d %d): "
                       "move it to a free unit, or take it out of z3660cfg.txt"
                       % (sid, role, card.slots[n], sid, n))
    return out


def z3660_with_amix(card, ids, names, scsiboot=True):
    """z3660cfg.txt with AMIX's disks: an hdf<N> line per disk (its slot if the
    card has one for it already, else a free one) and scsi<K> <N> per unit;
    scsiboot YES when asked (pvmk boots from the SCSI ROM).  The lines for
    those slots and units are replaced, every other line stays.  Returns
    (text, slots used, whether scsiboot was changed)."""
    want = dict((role, "%s/%s" % (Z3660_HDF_DIR, names[role])) for role in names)
    slot = {}
    for role, path in want.items():
        have = [n for n, p in card.slots.items() if p == path]
        if have:
            slot[role] = have[0]
    free = [n for n in range(Z3660_SLOTS) if n not in card.slots and n not in slot.values()]
    for role in sorted(want, key=lambda r: ids[r]):
        if role not in slot:
            if not free:
                raise ValueError("z3660cfg.txt has no free hdf slot (hdf0-hdf%d)"
                                 % (Z3660_SLOTS - 1))
            slot[role] = free.pop(0)
    mine_slots = set(slot.values())
    mine_units = set(ids[r] for r in want)
    out, boot_changed, boot_seen = [], False, False
    for line in (card.text or "").splitlines():
        if line.startswith(MARK):
            continue                 # the comment of an earlier run
        w = _z3660_words(line)
        if w:
            m = re.match(r"^(hdf|iso)([0-9]+)$", w[0])
            if m and int(m.group(2)) in mine_slots:
                continue
            m = re.match(r"^scsi([0-6])$", w[0])
            if m and int(m.group(1)) in mine_units:
                continue
            if w[0] == "scsiboot" and scsiboot:
                boot_seen = True
                if len(w) > 1 and w[1].upper() != "YES":
                    out.append("scsiboot YES")
                    boot_changed = True
                    continue
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    out += ["", "%s: the AMIX disks (pvmk boots from SCSI unit %d)" % (MARK, ids.get("pvmk", 5))]
    if scsiboot and not boot_seen:
        out.append("scsiboot YES")
        boot_changed = True
    for role in sorted(want, key=lambda r: ids[r]):
        out.append("hdf%d %s" % (slot[role], want[role]))
    for role in sorted(want, key=lambda r: ids[r]):
        out.append("scsi%d %d" % (ids[role], slot[role]))
    return "\n".join(out) + "\n", slot, boot_changed
