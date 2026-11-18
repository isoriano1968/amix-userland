"""What the installer offers, as data, and the rules between the choices.

No GTK here: the UI, the profiles, the tests and the backend all use it.

An option's "needs" says when it is available:
  None                      always
  ("a", "b")                one of the tags a or b is chosen
  (("a", "b"), ("c",))      (a or b) and c
Tags are the chosen options of the radio groups in TAG_GROUPS, plus the
derived tags of derived_tags(), plus the id of every hardware switch that is
on and fits (the groups of HW_GROUPS: hw_a2091, hw_zz9000, ...).

The Machine page lists what the Amiga has: its disk controllers, network
interfaces and graphics cards, as switches.  Later pages choose among them:
the controller AMIX's disks are on (diskon), the interface the install runs
over (net: the host name's address goes to it), the main display (gfx: the
console and X).  Every other ticked card gets its driver and device node;
its address, or its use beside the main display, is set on AMIX.  Changing
the Amiga sets these switches to what that machine comes with (PRESET_ON).
"""

TAG_GROUPS = ("src", "target", "mach", "accel", "net", "diskon", "layout", "kern", "boot", "gfx",
              "desk")
# the switch groups of hardware the machine has
HW_GROUPS = ("dctl", "netif", "gfxhw")

NOT_AVAILABLE = "Not for this machine or layout"
NOT_TICKED = "Not ticked on the Machine page"
# where the IPS client of doc/plans/IPS-INSTALLER-PLAN.md shows itself
PKG_CLIENT_PROBES = ("/var/pkg", "/usr/bin/pkg")


class Option(object):
    def __init__(self, id, title, sub, needs=None, badge=None, mb=0, default=False, probe=(),
                 blocked=None, nodriver=None):
        self.id = id
        # hardware the installer knows but no kernel of it drives yet: why
        self.nodriver = nodriver
        # {tag: why}: not available while the tag is chosen, for that reason
        # (a card that fits the machine, but its kit is not built yet)
        self.blocked = dict(blocked or {})
        self.probe = tuple(probe)   # paths that show it is installed on a source disk
        self.title = title
        self.sub = sub
        self.needs = _norm_needs(needs)
        self.badge = badge
        self.mb = mb
        self.default = default


class Group(object):
    def __init__(self, id, title, options, radio=False):
        self.id = id
        self.title = title
        self.options = options
        self.radio = radio

    def option(self, oid):
        for o in self.options:
            if o.id == oid:
                return o
        return None


class Step(object):
    def __init__(self, id, title, hint, intro, groups=(), summary=False):
        self.id = id
        self.title = title
        self.hint = hint
        self.intro = intro
        self.groups = list(groups)
        self.summary = summary


def _norm_needs(needs):
    if needs is None:
        return None
    needs = tuple(needs)
    if needs and isinstance(needs[0], str):
        return (needs,)
    return tuple(tuple(n) for n in needs)


O = Option
G = Group

# what the Zorro slots of each machine take
ZORRO2 = ("a3000", "a2500", "a4000")
ZORRO3 = ("a3000", "a4000")
# CPU cards the 68030 kernels (Commodore's, or it with kits) run on
STOCK_CPUS = ("none", "gvp", "a2630", "a2620", "b1230", "uae")
PORT_CPUS = ("z3660", "mercury", "gvp", "a3640", "dkb1240", "a1200_060", "uae")
NODRV = "no driver yet"


def _amigaux(drv):
    return ("amigaux 1.1 has a driver for it (%s, MIT); it is not ported to the kernels of "
            "this installer yet: tick it to record it, AMIX's disks cannot go on it" % drv)


MACHINE = Step("machine", "Machine", "Amiga, CPU card and hardware",
     "Tick what the Amiga has: the kernel, its drivers and the disk names follow it. Later "
     "pages choose among it (the install network, the main display, where AMIX's disks go); "
     "options the machine cannot use are greyed out.",
     [G("mach", "Amiga", radio=True, options=[
         O("a3000", "Amiga 3000", "AMIX’s own machine, onboard 68030"),
         O("a2500", "Amiga 2500 / 2000", "Zorro II with a CPU card in the CPU slot"),
         O("a4000", "Amiga 4000", "Experimental, untested: AMIX has never booted on an "
           "A4000. With a 68040 or 68060 card, or a 68030 card with MMU", badge="experimental"),
         # no Zorro: disks only on its IDE (gayle, not ported) or a card's SCSI
         O("a1200", "Amiga 1200", "Experimental, untested: with a trapdoor CPU card with MMU. "
           "No AMIX driver for its IDE or a card's SCSI yet: the disks are prepared now and "
           "boot once one exists", badge="experimental"),
         O("uae", "FS-UAE emulator", "A3000 configuration with A2065 network")]),
      G("accel", "CPU card", radio=True, options=[
          O("none", "None (bare metal)", "Onboard 68030 and its memory", needs=("a3000", "uae")),
          O("z3660", "Z3660", "68060 plus ARM: RTG, Ethernet, SD card, up to 528 MB",
            needs=("a3000", "a4000"), badge="fastest"),
          O("a3640", "A3640", "Commodore’s 68040 card of the A4000/040",
            needs=("a4000",)),
          O("mercury", "Mercury", "68060 in the A3000 CPU slot", needs=("a3000",)),
          O("gvp", "GVP G-Force", "68030 or 68040 cards with their own SCSI",
            needs=("a3000", "a2500")),
          O("a2630", "A2630", "Commodore’s 68030 card, 2 or 4 MB", needs=("a2500",)),
          O("a2620", "A2620", "Commodore’s 68020 card with MMU", needs=("a2500",)),
          O("b1230", "Blizzard 1230-III", "68030 with MMU in the trapdoor; its fast memory "
            "sits below 1 GiB", needs=("a1200",)),
          O("dkb1240", "DKB 1240", "68040 in the trapdoor", needs=("a1200",)),
          O("a1200_060", "68060 trapdoor card", "Untested: its fast memory must sit below "
            "1 GiB", needs=("a1200",), badge="untested"),
          # their fast memory is mapped above 1 GiB, where AMIX has no pages
          O("bliz_iv", "Blizzard 1230-IV, 1240, 1260", "Not yet: their fast memory sits "
            "above 1 GiB", needs=("a1200",),
            blocked={"a1200": "Their fast memory sits above 1 GiB, where AMIX has none"}),
          O("ec030", "68EC030 card", "No MMU: AMIX cannot run on it", needs=("a1200", "a4000"),
            blocked={"a1200": "A 68EC030 has no MMU, which AMIX needs",
                     "a4000": "A 68EC030 has no MMU, which AMIX needs"})]),
      G("memory", "Memory", options=[
          O("bigram", "BigRAM on the A2630", "More fast RAM on the card, used by the kernel",
            needs=("a2630",))]),
      # the controllers AMIX's dd/sd drives (the A3000's, the A2091, a GVP card's,
      # the Z3660's SD card) and the ones amigaux 1.1 has drivers for
      G("dctl", "Disk controllers", options=[
          O("hw_a3000scsi", "A3000 SCSI", "The A3000’s own SCSI: in AMIX since 2.1",
            needs=("a3000", "uae")),
          O("hw_a2091", "A2091 / A2090", "Commodore’s Zorro II SCSI card: in AMIX since 2.1",
            needs=(ZORRO2,)),
          O("hw_gvpscsi", "GVP SCSI", "The SCSI of the GVP G-Force card", needs=("gvp",)),
          O("hw_z3660sd", "Z3660 SD card", "Disk images on the board’s SD card: in the Z3660 "
            "kernel, a kit for the 68040/060 port", needs=("z3660",)),
          O("hw_a4091", "A4091", "Commodore’s Zorro III SCSI-2 card (53C710)", needs=(ZORRO3,),
            badge=NODRV, nodriver=_amigaux("a4091")),
          O("hw_a4092", "A4092", "The new Zorro III SCSI-2 card (53C710)", needs=(ZORRO3,),
            badge=NODRV, nodriver=_amigaux("a4091")),
          O("hw_a4kt710", "A4000T SCSI", "The A4000T’s own SCSI (53C710)", needs=("a4000",),
            badge=NODRV, nodriver=_amigaux("a4kt710")),
          O("hw_gayle", "IDE port", "The A4000’s or A1200’s own IDE (Gayle)",
            needs=("a4000", "a1200"), badge=NODRV, nodriver=_amigaux("gayle")),
          # amix-gayle-ide's Buddha driver (amigaux, MIT) as our buddha kit,
          # polled I/O, with the stock and 68040/060 port kernels
          O("hw_buddha", "Buddha IDE", "Zorro II, two IDE ports: its driver is linked into "
            "your kernel (polled I/O)", needs=(ZORRO2,)),
          O("hw_catweasel", "Catweasel Z-II IDE", "Zorro II, three IDE ports: the Buddha's "
            "driver, linked into your kernel (polled I/O)", needs=(ZORRO2,)),
          O("hw_blizscsi", "Blizzard SCSI kit", "The SCSI module of a Blizzard trapdoor card",
            needs=("a1200",), badge=NODRV,
            nodriver="No AMIX driver exists for it yet: tick it to record it, AMIX's disks "
            "cannot go on it")]),
      G("netif", "Network interfaces", options=[
          O("hw_a2065", "A2065", "Commodore’s Ethernet card, in every AMIX; FS-UAE emulates it",
            needs=(ZORRO2 + ("uae",),)),
          O("hw_zen", "Z3660 Ethernet", "The board’s own port", needs=("z3660",)),
          O("hw_hydra", "HydraNet", "Hydra Systems AmigaNet (Zorro II): a kit for the kernel",
            needs=(ZORRO2,)),
          O("hw_xsurf", "X-Surf 100", "Individual Computers X-Surf 100: a kit for the kernel",
            needs=(ZORRO2,)),
          O("hw_zz9000eth", "ZZ9000 Ethernet", "The MNT ZZ9000’s port: a kit for the kernel",
            needs=((ZORRO2), ("hw_zz9000",)))]),
      G("gfxhw", "Graphics cards", options=[
          O("hw_rtg", "Z3660 RTG", "The board’s HDMI output (RGB565)", needs=("z3660",)),
          O("hw_picasso2", "Picasso II", "Cirrus 8-bit RTG; FS-UAE emulates it",
            needs=(ZORRO2 + ("uae",),)),
          O("hw_zz9000", "ZZ9000", "MNT ZZ9000 RTG (Zorro II mode)", needs=(ZORRO2,)),
          O("hw_va2000", "VA2000", "MNT VA2000 RTG", needs=(ZORRO2,))]),
      G("net", "Network for the install", radio=True, options=[
          O("net2065", "A2065", "Commodore’s Ethernet card: the only network a stock "
            "AMIX has", needs=(("hw_a2065",), ("none", "gvp", "a2630", "a2620", "mercury",
                                               "a3640", "uae"))),
          O("netzen", "Z3660 Ethernet",
            "Only once a kernel from this installer runs", needs=("hw_zen",)),
          O("nethydra", "HydraNet", "Only once a kernel from this installer runs: its driver "
            "is linked into your kernel", needs=("hw_hydra",)),
          O("netxsurf", "X-Surf 100", "Only once a kernel from this installer runs: its "
            "driver is linked into your kernel", needs=("hw_xsurf",)),
          O("netzz9000", "ZZ9000 Ethernet", "Only once a kernel from this installer runs: its "
            "driver is linked into your kernel", needs=("hw_zz9000eth",)),
          O("nonet", "None", "Install from HDF files prepared in FS-UAE; the other "
            "interfaces get their driver, their address is set on AMIX")]),
      G("keymap", "Keyboard", radio=True, options=[
          O("us", "US", "Amiga keyboard with the US layout"),
          O("de", "German", "Amiga keyboard with the German layout"),
          O("depc", "German PC keyboard", "In an emulator such as FS-UAE", needs=("uae",))]),
      G("cpu", "Code for added packages", radio=True, options=[
          O("m020", "68020–68060", "Runs on every machine above", badge="default"),
          O("m060", "68060 tuned", "Fastest on 68060 cards", needs=("060",))])])


STEPS = [
    Step("base", "Base system", "Your AMIX core system",
         "The installer starts from a core AMIX 2.1c installation made from your own "
         "tapes or disks. Commodore’s system files are not shipped with it; "
         "everything added on top is open source.",
         [G("src", "Start from", radio=True, options=[
             O("hdf", "Core HDF", "An AMIGA_UNIX.hdf with the core packages installed"),
             O("tape", "AMIX 2.1c tape image",
               "Installs the core packages first, from your tape and root floppy"),
             O("run", "Running AMIX system",
               "Over its network: adds, updates or removes packages in place",
               badge="no copy needed", needs=("net2065", "netzen"))]),
          G("target", "Write to", radio=True, options=[
              O("sd", "Z3660 SD card", "HDF files for the board’s SD card",
                needs=(("on_z3660sd",), ("hdf", "tape"))),
              # a ZuluSCSI/BlueSCSI sits on a SCSI bus: the disks' controller's
              O("card", "ZuluSCSI or BlueSCSI SD card",
                "Disk images named by SCSI ID (HD60_512.hda …) to copy onto the card",
                needs=(("on_a3000scsi", "on_a2091", "on_gvpscsi", "on_a4091", "on_a4092",
                        "on_a4kt710", "on_blizscsi"), ("hdf", "tape"),
                       ("a3000", "a2500", "a4000", "a1200"))),
              O("file", "HDF files only", "For FS-UAE, or to copy later",
                needs=("hdf", "tape")),
              O("inplace", "The running system", "Its own disks, after a check of free space",
                needs=("run",))]),
          # what happens to files of the same name in the output folder
          G("existing", "Files that exist already", radio=True, options=[
              O("overwrite", "Overwrite",
                "Replaced when the new ones are written and checked; a file the installer "
                "did not write is named and asked about first", needs=("hdf", "tape")),
              O("backup", "Keep a backup",
                "Each one renamed to NAME.YYYYMMDD-HHMMSS.bak first", needs=("hdf", "tape"))]),
          G("pkgclient", "Package client", options=[
              O("pkg", "AMIX package client",
                "pkg and /var/pkg: installs, updates and removes packages, each file checked; "
                "every package below needs it", mb=2, probe=PKG_CLIENT_PROBES)])]),
    MACHINE,
    Step("disks", "Disks", "Core and data disk",
         "Keep the core system small and put what you add on a data disk: an update of one "
         "never fills the other, and a core disk that was made too small is no longer a problem.",
         [G("diskon", "AMIX disks on", radio=True, options=[
             # dd/sd's controllers first: a controller without a driver is never
             # picked while one with a driver is ticked
             O("on_a3000scsi", "A3000 SCSI", "Core disk at SCSI ID 6, pvmk at 5, data at 1",
               needs=("hw_a3000scsi",)),
             O("on_a2091", "A2091 / A2090", "Core disk at SCSI ID 6, pvmk at 5, data at 1",
               needs=("hw_a2091",)),
             O("on_gvpscsi", "GVP SCSI", "Core disk at SCSI ID 6, pvmk at 5, data at 1",
               needs=("hw_gvpscsi",)),
             O("on_z3660sd", "Z3660 SD card", "HDF files at units 6, 5 and 1 of the board",
               needs=("hw_z3660sd",)),
             O("on_a4091", "A4091", "No AMIX driver yet: the disks are written, AMIX boots "
               "from them once it has one", needs=("hw_a4091",), badge=NODRV),
             O("on_a4092", "A4092", "No AMIX driver yet: the disks are written, AMIX boots "
               "from them once it has one", needs=("hw_a4092",), badge=NODRV),
             O("on_a4kt710", "A4000T SCSI", "No AMIX driver yet: the disks are written, AMIX "
               "boots from them once it has one", needs=("hw_a4kt710",), badge=NODRV),
             O("on_gayle", "IDE port", "No AMIX driver yet: the disks are written, AMIX boots "
               "from them once it has one", needs=("hw_gayle",), badge=NODRV),
             # the driver gives each port's drives fixed units: port 0 6/5,
             # port 1 3/2 (plan.IDE_UNIT)
             O("on_buddha", "Buddha IDE", "Core disk on port 0's master, pvmk on its slave, "
               "data on port 1's master", needs=(("hw_buddha",), ("stock", "k040"))),
             O("on_catweasel", "Catweasel Z-II IDE", "Core disk on port 0's master, pvmk on "
               "its slave, data on port 1's master", needs=(("hw_catweasel",),
                                                             ("stock", "k040"))),
             O("on_blizscsi", "Blizzard SCSI kit", "No AMIX driver yet: the disks are "
               "written, AMIX boots from them once it has one", needs=("hw_blizscsi",),
               badge=NODRV)]),
          G("layout", "Layout", radio=True, options=[
             O("single", "Everything on the core disk",
               "Only while 100 MB stay free on it; otherwise additions go to a data disk"),
             O("split", "Core disk plus data disk",
               "/usr/local, /usr/gnu, /opt and the desktop on their own disk", badge="recommended"),
             O("grow", "Copy the core system to a larger disk",
               "For a core disk that was made too small", needs=("hdf", "tape"))]),
          G("dsize", "Data disk size", radio=True, options=[
              O("d1g", "1 GB", "Desktop and a few applications", needs=("split",)),
              O("d2g", "2 GB", "Room for development tools and games", needs=("split",))])]),
    Step("kernel", "Kernel", "Optimised or stock",
         "Kernels are built from the published AMIX kernel sources with my patches. The stock "
         "kernel always stays on the disk as a fallback boot entry.",
         [G("kern", "Kernel", radio=True, options=[
             O("z3opt", "Z3660 optimised", "68060, large memory, no drivers for absent hardware",
               needs=("z3660",), badge="fastest"),
             O("k040", "68040/060 port", "For 68040 and 68060 cards", needs=(PORT_CPUS,)),
             O("stock", "Stock AMIX 2.1c", "Commodore’s 68030 kernel, unchanged",
               needs=(STOCK_CPUS,))]),
          G("boot", "Boot", radio=True, options=[
              O("pvmk", "pvmk boot partition",
                "Boot menu and serial console; loads the kernel from UNIX_Root",
                badge="recommended", mb=1),
              O("amixboot", "AMIX boot partition only",
                "Commodore\u2019s boot, for the stock and 040/060 kernels",
                needs=("stock", "k040"))]),
          G("kopt", "Kernel options", options=[
              O("vt", "Virtual consoles", "Alt+F keys switch between consoles and X",
                needs=("z3opt", "k040")),
              O("mem", "Memory above 256 MB", "Uses all of the Z3660’s RAM",
                needs=(("z3660",), ("z3opt",))),
              O("syslog", "System log", "syslogd at boot, faults in /var/log/bus-errors")])]),
    Step("drivers", "Drivers", "Network, graphics, sound",
         "Only drivers for hardware the machine has are installed; each one is its own package "
         "and can be removed later.",
         [G("gfx", "Main display", radio=True, options=[
             # built into the Z3660 kernel; the 68040/060 port kernel links the
             # z3660rtg kit (/dev/z3660rtg, major 55)
             O("rtg", "Z3660 RTG", "8, 15 and 16-bit RTG modes for X (HDMI is RGB565: no "
               "24/32-bit)", needs=("hw_rtg",), mb=2),
             # Zorro II, /dev/rtgfb, major 54: a kit per kernel line (rtgfb, rtgfb-040)
             O("picasso2", "Picasso II", "Cirrus 8-bit RTG, 640\u00d7480 to 1024\u00d7768; FS-UAE "
               "emulates it. Its driver is linked into your kernel",
               needs=(("hw_picasso2",), ("stock", "k040"), ("pvmk",)), mb=2),
             # Zorro II product 3 (AMIX has no Zorro III): /dev/zz9000, major 49
             O("zz9000", "ZZ9000", "MNT ZZ9000 RTG, 16-bit, tested at 1920\u00d7800; its driver "
               "is linked into your kernel", needs=(("hw_zz9000",), ("stock", "k040")), mb=2),
             # Zorro II, /dev/va2000, major 68: a kit per kernel line (va2000,
             # va2000-040); X has no va2000 card driver yet: the console and
             # OPEN LOOK until it has
             O("va2000", "VA2000", "MNT VA2000 RTG; its driver is linked into your kernel, "
               "X support comes later", needs=(("hw_va2000",), ("stock", "k040")), mb=1),
             O("chipset", "Amiga chipset", "No graphics card: the console and OPEN LOOK")]),
          G("drv", "Hardware", options=[
             O("zen", "Z3660 Ethernet", "zen0 with interrupt-driven receive", needs=("hw_zen",),
               mb=1),
             O("paula", "Paula audio", "/dev/paula for players and games", mb=1),
             O("a2065", "A2065 Ethernet", "Commodore’s card, and FS-UAE’s network",
               needs=(("hw_a2065",), ("none", "gvp", "a2630", "a2620", "mercury", "a3640",
                                     "uae")), mb=1),
             O("random", "Random numbers", "/dev/random and /dev/urandom, needed by ssh", mb=1)]),
          G("core1", "Services on the ARM core", options=[
              O("gpu", "3D graphics", "OpenGL on the second ARM core", needs=("z3660",), mb=30),
              O("video", "Video decoding", "Hardware overlay for movies and streams",
                needs=("z3660",), mb=8),
              O("crypto", "Crypto offload", "Faster ssh logins", needs=("z3660",), mb=2)])]),
    Step("desktop", "Desktop", "X11, CDE and apps",
         "X11R6.3 with Motif comes with every desktop choice except OPEN LOOK and the console.",
         [G("desk", "Desktop", radio=True, options=[
             O("cde", "CDE", "Front panel, file manager, terminal, App Manager",
               badge="recommended", mb=140, probe=("/usr/dt/bin/dtwm",),
               needs=("rtg", "picasso2", "zz9000")),
             O("mwm", "Motif window manager", "mwm only, light and quick", mb=45,
               probe=("/opt/motif/bin/mwm", "/usr/motif/bin/mwm"),
               needs=("rtg", "picasso2", "zz9000")),
             O("ol", "OPEN LOOK", "AMIX’s own desktop, as shipped"),
             O("console", "Console only", "No X server")]),
          G("apps", "Applications", options=[
              O("netsurf", "NetSurf", "Web browser", needs=("x11",), mb=25,
                probe=("/usr/local/bin/netsurf-fb", "/usr/local/bin/netsurf")),
              O("media", "AmigaCast", "Movies and DLNA streams", needs=(("z3660",), ("x11",)), mb=30,
                probe=("/opt/amix/bin/amixcast", "/opt/vplay/amixcast")),
              O("grafx2", "Grafx2", "Paint program", needs=("x11",), mb=6,
                probe=("/usr/local/bin/grafx2",)),
              O("sysmon", "System monitor", "CPU, memory and network in the front panel",
                needs=("cde",), mb=1, probe=("/opt/amix/bin/amixsysmon",)),
              O("games", "Games", "Quake, Quake III and more on the ARM core",
                needs=(("z3660",), ("x11",)), mb=600, probe=("/opt/amix/bin/c1q3", "/opt/quake"))])]),
    Step("dev", "Development", "Compilers and tools",
         "Development files of every installed package come with the toolchains, so programs "
         "build on the Amiga itself.",
         [G("tc", "Toolchains", options=[
             O("gcc14", "GCC 14", "C and C++, current standards", mb=180),
             O("gcc27", "GCC 2.7.2.3", "The compiler generation AMIX was built with", mb=25,
               probe=("/usr/local/gcc-2.7.2.3",)),
             O("headers", "Development files", "Headers, pkg-config files and link stubs", mb=40),
             O("pkgconf", "pkg-config", "Finds installed libraries for builds", mb=1,
               probe=("/usr/local/bin/pkg-config",))]),
          G("tools", "Tools", options=[
              O("gnu", "GNU tools", "bash, grep, sed, gawk, find and tar", mb=30,
               probe=("/usr/gnu/bin/bash",)),
              O("python", "Python 3", "With pip for user installs", mb=90,
               probe=("/usr/local/bin/python3",)),
              O("vim", "vim", "Editor", mb=10, probe=("/usr/local/bin/vim",))])]),
    Step("summary", "Summary", "Check and create",
         "Check your choices, then create the disks. A profile keeps them for the next machine.",
         summary=True),
]

# readable names of tags, for "needs ..." tooltips
TAG_NAMES = {"060": "a 68060 card", "x11": "an X11 desktop"}

# radio groups whose fallback is "no card", not the first card that fits
FALLBACK = {"gfx": "chipset"}

DEFAULT_SEL = {"src": "hdf", "target": "sd", "mach": "a3000", "accel": "z3660", "cpu": "m020",
               "layout": "split", "dsize": "d2g", "kern": "z3opt", "boot": "pvmk", "gfx": "rtg",
               "desk": "cde", "diskon": "on_z3660sd",
               "keymap": "us", "net": "netzen", "existing": "overwrite"}

DEFAULT_ON = {"pkg": True, "bigram": True, "vt": True, "syslog": True, "mem": True,
              "hw_a3000scsi": True, "hw_a2091": False, "hw_gvpscsi": True, "hw_z3660sd": True,
              "hw_a4091": False, "hw_a4092": False, "hw_a4kt710": False, "hw_gayle": False,
              "hw_buddha": False, "hw_catweasel": False, "hw_blizscsi": False,
              "hw_a2065": True, "hw_zen": True, "hw_hydra": False, "hw_xsurf": False,
              "hw_zz9000eth": False,
              "hw_rtg": True, "hw_picasso2": False, "hw_zz9000": False, "hw_va2000": False,
              "zen": True, "paula": True, "a2065": False, "random": True,
              "gpu": True, "video": True, "crypto": True, "netsurf": True, "media": True,
              "grafx2": False, "sysmon": True, "games": False, "gcc14": True, "gcc27": False,
              "headers": True, "pkgconf": True, "gnu": True, "python": False, "vim": True}

# what each Amiga comes with: set when it is chosen (A3000UX and A2500UX
# shipped with an A2065; the A2500's disks are on its A2091)
PRESET_ON = {
    "a3000": {"hw_a3000scsi": True, "hw_a2065": True},
    "a2500": {"hw_a2091": True, "hw_a2065": True},
    "a4000": {"hw_gayle": True, "hw_a2065": False},
    "a1200": {"hw_gayle": True},
    "uae": {"hw_a3000scsi": True, "hw_a2065": True},
}
# the radio choices of the later pages -> the hardware they are
GFX_HW = {"rtg": "hw_rtg", "picasso2": "hw_picasso2", "zz9000": "hw_zz9000",
          "va2000": "hw_va2000"}
NET_HW = {"net2065": "hw_a2065", "netzen": "hw_zen", "nethydra": "hw_hydra",
          "netxsurf": "hw_xsurf", "netzz9000": "hw_zz9000eth"}
DISK_HW = {"on_" + h[3:]: h for h in ("hw_a3000scsi", "hw_a2091", "hw_gvpscsi", "hw_z3660sd",
                                      "hw_a4091", "hw_a4092", "hw_a4kt710", "hw_gayle",
                                      "hw_buddha", "hw_catweasel", "hw_blizscsi")}


def implied_on(sel):
    """The hardware switches a selection implies: what its Amiga comes with,
    the CPU card's own controller, and the cards its later choices use (for a
    profile of an installer before 0.6, and a selection given as a dict)."""
    on = dict(PRESET_ON.get(sel.get("mach"), {}))
    for m, key in ((GFX_HW, "gfx"), (NET_HW, "net"), (DISK_HW, "diskon")):
        if sel.get(key) in m:
            on[m[sel[key]]] = True
    if sel.get("target") == "sd":
        on["hw_z3660sd"] = True
    if sel.get("net") == "netzz9000":
        on["hw_zz9000"] = True
    return on


# example sizes until the source image is read (version 0.2)
CORE_DISK_MB = 600
CORE_USED_MB = 310
GROWN_CORE_MB = 2000
# what every file system keeps free (pkg --min-free); with a data disk the new
# packages go there: the core only gets the package client, /var/pkg and the
# bootstrap
RESERVE_MB = 100
ROOT_RESERVE_MB = SPLIT_RESERVE_MB = RESERVE_MB
CORE_TOOLS_MB = 5
# AMIX is not tested with disks above 2 GB (the core disks stop just below 2^31 bytes)
DATA_DISK_MB = {"d1g": 1000, "d2g": 2000}


def all_probes():
    out = []
    for _, g in all_groups():
        for o in g.options:
            out.extend(o.probe)
    return out


def all_groups():
    for s in STEPS:
        for g in s.groups:
            yield s, g


def find_group(gid):
    for _, g in all_groups():
        if g.id == gid:
            return g
    raise KeyError(gid)


def step_index(sid):
    for i, s in enumerate(STEPS):
        if s.id == sid:
            return i
    raise KeyError(sid)


def tag_title(tag):
    if tag in TAG_NAMES:
        return TAG_NAMES[tag]
    for _, g in all_groups():
        o = g.option(tag)
        if o is not None and (g.radio or g.id in HW_GROUPS):
            return o.title
    return tag


class Config(object):
    """A selection: one id per radio group, on/off per switch."""

    def __init__(self, sel=None, on=None, source=None):
        self.source = source        # hdf.SourceInfo of the core image, or None
        self.sel = dict(DEFAULT_SEL)
        self.on = dict(DEFAULT_ON)
        if sel:
            self.sel.update(sel)
            self.on.update(implied_on(sel))
        if on:
            self.on.update(on)
        self.normalize()

    # -- rules -------------------------------------------------------------
    def tags(self):
        t = set(self.sel.get(g) for g in TAG_GROUPS)
        t.discard(None)
        t |= derived_tags(self.sel)
        # the hardware switches that are on and fit: twice, for a switch that
        # needs another one (the ZZ9000's Ethernet needs the ZZ9000)
        for _ in range(2):
            base = set(t)
            for gid in HW_GROUPS:
                for o in find_group(gid).options:
                    if self.on.get(o.id) and self._fits(o, base):
                        t.add(o.id)
        return t

    def _fits(self, opt, tags):
        if any(t in tags for t in opt.blocked):
            return False
        if opt.needs is None:
            return True
        return all(any(n in tags for n in alt) for alt in opt.needs)

    def available(self, opt, tags=None):
        return self._fits(opt, self.tags() if tags is None else tags)

    def why_not(self, opt):
        """Why an option is greyed out: its own reason, or the general one."""
        tags = self.tags()
        for t, why in opt.blocked.items():
            if t in tags:
                return why
        # hardware this machine can have, not ticked on the Machine page
        miss = self.missing(opt)
        hw = dict((o.id, o) for gid in HW_GROUPS for o in find_group(gid).options)
        if miss and all(any(t in hw and self.available(hw[t]) for t in alt) for alt in miss):
            return NOT_TICKED
        return NOT_AVAILABLE

    def hardware(self, gid):
        """The ids of the ticked hardware of a HW_GROUPS group that fits."""
        t = self.tags()
        return [o.id for o in find_group(gid).options if o.id in t]

    def further(self, gid):
        """The other cards beside the main one, as the radio group's option
        ids: "gfx" -> the graphics cards beside the main display, "net" -> the
        network interfaces beside the install one; only cards a radio option
        stands for (those with a driver for this kernel line)."""
        hw = {"gfx": ("gfxhw", GFX_HW), "net": ("netif", NET_HW)}[gid]
        have = self.hardware(hw[0])
        return [oid for oid, h in hw[1].items() if h in have and oid != self.sel.get(gid)]

    def missing(self, opt):
        """The alternatives the option still needs, for a tooltip."""
        if opt.needs is None:
            return []
        tags = self.tags()
        return [alt for alt in opt.needs if not any(n in tags for n in alt)]

    def chosen(self, group, opt):
        if not self.available(opt):
            return False
        if group.radio:
            return self.sel.get(group.id) == opt.id
        return bool(self.on.get(opt.id))

    def normalize(self):
        """Move every radio group off an option that is no longer available.
        Returns the groups that changed."""
        changed = []
        for _ in range(len(STEPS) + 1):
            moved = False
            for _, g in all_groups():
                if not g.radio:
                    continue
                cur = g.option(self.sel.get(g.id))
                if cur is not None and self.available(cur):
                    continue
                # a card the machine may not have is never picked for the user:
                # those groups fall back to their "none" choice first
                safe = g.option(FALLBACK.get(g.id))
                pick = safe if safe is not None and self.available(safe) else \
                    next((o for o in g.options if self.available(o)), None)
                if pick is not None and self.sel.get(g.id) != pick.id:
                    self.sel[g.id] = pick.id
                    changed.append(g.id)
                    moved = True
            if not moved:
                break
        # everything on a core disk that would keep under RESERVE_MB free: what
        # is added goes to a data disk instead of the installer refusing
        self.layout_moved = False
        split = find_group("layout").option("split")
        if self.sel.get("layout") == "single" and self.available(split) and \
                not self.space()[0][0]["fits"]:
            self.sel["layout"] = "split"
            self.layout_moved = True
            changed.append("layout")
            self.normalize_after_layout(changed)
        return changed

    def normalize_after_layout(self, changed):
        """The data disk size once the layout moved to split."""
        g = find_group("dsize")
        if g.option(self.sel.get("dsize")) is None or not self.available(
                g.option(self.sel.get("dsize"))):
            pick = next((o for o in g.options if self.available(o)), None)
            if pick is not None:
                self.sel["dsize"] = pick.id
                changed.append("dsize")

    # -- changes -----------------------------------------------------------
    def select(self, gid, oid):
        g = find_group(gid)
        o = g.option(oid)
        if o is None or not g.radio:
            raise KeyError("%s/%s" % (gid, oid))
        if not self.available(o):
            return False
        if gid == "mach" and self.sel.get("mach") != oid:
            # another Amiga: its own hardware, the cards ticked for the old
            # one off
            preset = set(k for p in PRESET_ON.values() for k in p)
            for hg in HW_GROUPS:
                for h in find_group(hg).options:
                    self.on[h.id] = False if h.id in preset else DEFAULT_ON[h.id]
            self.on.update(PRESET_ON.get(oid, {}))
        self.sel[gid] = oid
        self.normalize()
        return True

    def set_on(self, oid, value):
        for _, g in all_groups():
            o = g.option(oid)
            if o is not None and not g.radio:
                if not self.available(o):
                    return False
                self.on[oid] = bool(value)
                return True
        raise KeyError(oid)

    # -- what follows from it ---------------------------------------------
    def chosen_options(self, step):
        out = []
        for g in step.groups:
            for o in g.options:
                if self.chosen(g, o):
                    out.append(o)
        return out

    def installed(self, opt):
        """True if the source disk already has it (a probe path exists)."""
        return bool(self.source is not None and opt.probe and
                    any(p in self.source.found for p in opt.probe))

    def pkg_client_missing(self):
        """The destination has no package client and it is switched off."""
        return not self.on.get("pkg") and not self.installed(find_group("pkgclient").option("pkg"))

    def added_mb(self):
        return sum(o.mb for s in STEPS for o in self.chosen_options(s) if not self.installed(o))

    def in_place(self):
        return self.sel["src"] == "run"

    def space(self):
        """Disk use in MB: a list of dicts, and whether it fits.  From the
        source image when it has been read, else example sizes."""
        add = self.added_mb()
        layout = self.sel["layout"]
        src = self.source
        mb = lambda b: int(round(b / 1048576.0))  # noqa: E731
        if src is not None:
            core_cap, core_used, movable = mb(src.root_used + src.root_avail), mb(src.root_used), \
                mb(src.movable_bytes())
        else:
            core_cap, core_used, movable = CORE_DISK_MB, CORE_USED_MB, 0
        if layout == "grow":
            core_total = max(GROWN_CORE_MB, core_cap)
            name = "Core disk (new, %s)" % _size_text(core_total)
        else:
            core_total = core_cap
            name = "Core disk (%s)" % _size_text(core_total)
        split = layout == "split"
        if split:
            core_used -= movable
        core_add = CORE_TOOLS_MB if split else add
        reserve = SPLIT_RESERVE_MB if split else ROOT_RESERVE_MB
        core_fits = core_used + core_add + reserve <= core_total
        disks = [{"name": name, "total": core_total, "used": core_used, "add": core_add,
                  "reserve": reserve, "fits": core_fits}]
        fits = core_fits
        if split:
            data_total = DATA_DISK_MB[self.sel["dsize"]]
            data_fits = movable + add <= data_total
            fits = fits and data_fits
            disks.append({"name": "Data disk (%d GB)" % (data_total // 1000),
                          "total": data_total, "used": movable, "add": add, "reserve": 0,
                          "fits": data_fits})
        return disks, fits

    def apply_label(self):
        return "Apply changes" if self.in_place() else "Create disks"

    def summary(self):
        out = []
        for s in STEPS:
            if s.summary:
                continue
            items = [o.title for o in self.chosen_options(s)]
            out.append((s, items or ["Nothing selected"]))
        return out


def _size_text(mb):
    return "%.1f GB" % (mb / 1000.0) if mb >= 1000 else "%d MB" % mb


def derived_tags(sel):
    t = set()
    if sel.get("accel") in ("z3660", "mercury", "a1200_060"):
        t.add("060")
    if sel.get("desk") in ("cde", "mwm"):
        t.add("x11")
    return t
