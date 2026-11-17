"""The GTK 4 / libadwaita UI.  It reads the model and writes nothing to disks.

Targets libadwaita >= 1.4 and GTK >= 4.12 (OpenIndiana 2025: 1.6 / 4.16).
"""
import math
import os
import subprocess
import sys
import threading
import time

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, Gio, GLib, Gtk  # noqa: E402

from . import APP_ID, __version__, fsuae, hdf, model, plan, profile, safety  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def data_dirs():
    env = os.environ.get("AMIX_INSTALLER_DATADIR")
    dirs = [env] if env else []
    comp = os.path.dirname(HERE)
    prefix = os.path.dirname(os.path.dirname(comp))   # installed: <prefix>/lib/amix-installer
    dirs += [os.path.join(prefix, "share", "amix-installer"),
             os.path.join(sys.prefix, "share", "amix-installer"),
             os.path.join(comp, "data"), os.path.join(comp, "build", "data")]
    return [d for d in dirs if d and os.path.isdir(d)]


def find_data(name):
    for d in data_dirs():
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    # source tree: the logo lives in amix-userland's doc/assets
    if name == "amix-logo.png":
        p = os.path.join(os.path.dirname(HERE), "..", "..", "..", "doc", "assets", name)
        if os.path.exists(p):
            return os.path.normpath(p)
    return None


def label(text, css=(), xalign=0.0, wrap=True):
    lb = Gtk.Label(label=text, xalign=xalign)
    lb.set_wrap(wrap)
    if wrap:
        lb.set_natural_wrap_mode(Gtk.NaturalWrapMode.WORD)
    for c in css:
        lb.add_css_class(c)
    return lb


def needs_text(cfg, opt):
    parts = []
    for alt in cfg.missing(opt):
        names = [model.tag_title(t) for t in alt]
        parts.append(" or ".join(names))
    return "Needs " + ", and ".join(parts) if parts else ""


def _mb(b):
    return "%d MB" % int(round(b / 1048576.0))


def source_rows(info):
    """Rows that show what the source image holds."""
    rows = []
    cyl, heads, secs = info.geometry
    r = Adw.ActionRow(title=os.path.basename(info.path),
                      subtitle="%s, %d partitions; opened read-only, never written"
                      % (_mb(info.size), len(info.partitions)))
    r.add_prefix(Gtk.Image.new_from_icon_name("drive-harddisk-symbolic"))
    rows.append(r)

    parts = Adw.ExpanderRow(title="Partitions",
                            subtitle=", ".join(p.name for p in info.partitions))
    for p in info.partitions:
        pr = Adw.ActionRow(title=p.name, subtitle=p.describe() + (", bootable" if p.bootable else ""))
        pr.add_suffix(label(_mb(p.size), ("caption", "dim-label", "numeric"), wrap=False))
        parts.add_row(pr)
    rows.append(parts)

    root = Adw.ActionRow(title="Root file system",
                         subtitle="%s used, %s free on %s" % (_mb(info.root_used), _mb(info.root_avail),
                                                              info.root.name))
    if info.root_avail < model.ROOT_RESERVE_MB * 1048576:
        root.add_suffix(label("below %d MB free: needs a data disk" % model.ROOT_RESERVE_MB,
                              ("caption", "warning"), wrap=False))
    rows.append(root)

    pk = Adw.ExpanderRow(title="AMIX packages",
                         subtitle="%d SVR4 packages%s" % (len(info.packages),
                                                         "" if info.has_core() else ", no core package"))
    for name, desc, ver in info.packages:
        pk.add_row(Adw.ActionRow(title=name, subtitle=(desc + ("  " + ver if ver else "")).strip()))
    rows.append(pk)

    kern = Adw.ExpanderRow(title="Kernels in /stand",
                           subtitle="/unix \u2192 %s, %d kernels" % (info.unix_link or "?", len(info.kernels)))
    for name, size in info.kernels:
        kern.add_row(Adw.ActionRow(title=name, subtitle=_mb(size)))
    rows.append(kern)

    mv = Adw.ActionRow(title="Added software",
                       subtitle=", ".join("%s %s" % (d, _mb(b)) for d, b in sorted(info.movable.items())))
    rows.append(mv)

    pc = [p for p in model.PKG_CLIENT_PROBES if p in info.found]
    rows.append(Adw.ActionRow(title="AMIX package client",
                              subtitle=("Installed (%s)" % ", ".join(pc)) if pc else
                              "Not installed; it is needed to add packages"))

    pv = Adw.ActionRow(title="pvmk boot partition",
                       subtitle=("%s on this disk; it is kept" % info.pvmk.name) if info.pvmk else
                       "Not on this disk; the installer writes one")
    rows.append(pv)
    for n in info.notes:
        w = Adw.ActionRow(title="Note", subtitle=n)
        w.add_prefix(Gtk.Image.new_from_icon_name("dialog-warning-symbolic"))
        rows.append(w)
    return rows


class SpaceBar(Gtk.DrawingArea):
    """Core system in grey, the selection in the accent colour, orange if it does not fit."""

    def __init__(self):
        super().__init__()
        self.set_content_height(14)
        self.set_hexpand(True)
        self.disk = None
        self.set_draw_func(self._draw)

    def set_disk(self, disk):
        self.disk = disk
        self.queue_draw()

    def _draw(self, area, cr, w, h):
        if not self.disk:
            return
        d = self.disk
        r = h / 2.0
        cr.new_sub_path()
        cr.arc(r, r, r, math.pi / 2, 3 * math.pi / 2)
        cr.arc(w - r, r, r, -math.pi / 2, math.pi / 2)
        cr.close_path()
        cr.clip_preserve()
        dark = Adw.StyleManager.get_default().get_dark()
        cr.set_source_rgba(1, 1, 1, 0.10) if dark else cr.set_source_rgba(0, 0, 0, 0.07)
        cr.fill()
        x = 0.0
        for mb, rgb in ((d["used"], (0.604, 0.600, 0.588)),
                        (d["add"], (0.208, 0.518, 0.894) if d["fits"] else (0.902, 0.380, 0.0))):
            ww = min(w - x, w * mb / float(d["total"]))
            if ww > 0:
                cr.set_source_rgb(*rgb)
                cr.rectangle(x, 0, ww, h)
                cr.fill()
                x += ww


class Window(Adw.ApplicationWindow):
    def __init__(self, app, cfg=None):
        super().__init__(application=app, title="AMIX Installer")
        self.set_default_size(1132, 772)
        self.set_size_request(360, 480)
        self.cfg = cfg or model.Config()
        self.paths = {}
        self.step = 0
        self._syncing = False
        self._rows = []        # (group, option, ActionRow, control)
        self._extra = []       # callables run by _sync()

        self.split = Adw.NavigationSplitView()
        self.split.set_min_sidebar_width(240)
        self.split.set_max_sidebar_width(300)
        self.split.set_sidebar(Adw.NavigationPage.new(self._build_sidebar(), "AMIX Installer"))
        self.content_page = Adw.NavigationPage.new(self._build_content(), "Base system")
        self.split.set_content(self.content_page)

        self.toasts = Adw.ToastOverlay()
        self.toasts.set_child(self.split)
        self.set_content(self.toasts)

        bp = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 640sp"))
        bp.add_setter(self.split, "collapsed", True)
        self.add_breakpoint(bp)

        self._actions()
        self.net = None
        self.connect("close-request", self._on_close)
        self.show_step(0)

    def _on_close(self, _w):
        if self.net is not None:          # the network install's servers end with the window
            self.net.stop()
            self.net = None
        return False

    # -- frame -------------------------------------------------------------
    def _build_sidebar(self):
        tv = Adw.ToolbarView()
        hb = Adw.HeaderBar()
        tbox = Gtk.Box(spacing=10)
        icon = Gtk.Image.new_from_icon_name(APP_ID)
        icon.set_pixel_size(28)
        tbox.append(icon)
        tbox.append(label("AMIX Installer", ("heading",), wrap=False))
        hb.set_title_widget(tbox)
        tv.add_top_bar(hb)

        self.steplist = Gtk.ListBox()
        self.steplist.add_css_class("navigation-sidebar")
        self.steplist.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.step_nums = []
        for i, s in enumerate(model.STEPS):
            row = Gtk.Box(spacing=12)
            row.set_margin_top(4)
            row.set_margin_bottom(4)
            num = label(str(i + 1), ("step-num",), xalign=0.5, wrap=False)
            num.set_size_request(24, 24)
            num.set_valign(Gtk.Align.CENTER)
            self.step_nums.append(num)
            row.append(num)
            col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=1)
            col.append(label(s.title, ("step-title",), wrap=False))
            col.append(label(s.hint, ("caption", "dim-label"), wrap=False))
            row.append(col)
            self.steplist.append(row)
        self.steplist.connect("row-activated", lambda lb, r: self.show_step(r.get_index()))

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        sw = Gtk.ScrolledWindow(vexpand=True)
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.set_child(self.steplist)
        box.append(sw)
        foot = label("The AMIX system files stay Commodore’s: the installer starts from "
                     "your own core image and adds only open packages.", ("caption", "dim-label"))
        foot.set_margin_start(16)
        foot.set_margin_end(16)
        foot.set_margin_bottom(16)
        foot.set_margin_top(8)
        box.append(foot)
        tv.set_content(box)
        return tv

    def _build_content(self):
        tv = Adw.ToolbarView()
        hb = Adw.HeaderBar()
        self.back_btn = Gtk.Button(label="Back")
        self.back_btn.connect("clicked", lambda b: self.show_step(self.step - 1))
        hb.pack_start(self.back_btn)
        menu = Gio.Menu()
        menu.append("Open Profile…", "win.open-profile")
        menu.append("Save Profile…", "win.save-profile")
        menu.append("Start Over", "win.reset")
        menu.append("About AMIX Installer", "win.about")
        mb = Gtk.MenuButton(icon_name="open-menu-symbolic", menu_model=menu)
        mb.set_tooltip_text("Main Menu")
        hb.pack_end(mb)
        self.next_btn = Gtk.Button(label="Next")
        self.next_btn.add_css_class("suggested-action")
        self.next_btn.connect("clicked", self._on_next)
        hb.pack_end(self.next_btn)
        self.wtitle = Adw.WindowTitle(title="", subtitle="")
        hb.set_title_widget(self.wtitle)
        tv.add_top_bar(hb)

        self.scroller = Gtk.ScrolledWindow(vexpand=True)
        self.scroller.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.clamp = Adw.Clamp(maximum_size=680, tightening_threshold=520)
        self.scroller.set_child(self.clamp)
        tv.set_content(self.scroller)
        return tv

    def _actions(self):
        for name, cb in (("open-profile", self._open_profile), ("save-profile", self._save_profile),
                         ("reset", self._reset), ("about", self._about), ("apply", self._apply)):
            a = Gio.SimpleAction.new(name, None)
            a.connect("activate", lambda a, p, cb=cb: cb())
            self.add_action(a)

    # -- pages -------------------------------------------------------------
    def show_step(self, i):
        i = max(0, min(len(model.STEPS) - 1, i))
        self.step = i
        s = model.STEPS[i]
        self._rows = []
        self._extra = []

        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=26)
        page.set_margin_top(28)
        page.set_margin_bottom(32)
        page.set_margin_start(16)
        page.set_margin_end(16)

        if i == 0:
            page.append(self._hero())
        head = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        head.append(label(s.title, ("title-1",)))
        head.append(label(s.intro, ("dim-label",)))
        page.append(head)

        if s.id == "base":
            page.append(self._running_note())
        if s.id == "disks":
            page.append(self._space_card())
        for g in s.groups:
            page.append(self._group(g))
        if s.id == "base":
            page.append(self._source_group())
            page.append(self._fsuae_group())
            page.append(self._source_view())
        if s.summary:
            page.append(self._summary())

        self.clamp.set_child(page)
        self.scroller.get_vadjustment().set_value(0)
        self.content_page.set_title(s.title)
        self.wtitle.set_title(s.title)
        self.wtitle.set_subtitle("Step %d of %d" % (i + 1, len(model.STEPS)))
        self.steplist.select_row(self.steplist.get_row_at_index(i))
        self.split.set_show_content(True)
        self._sync()

    def _hero(self):
        box = Gtk.Box(spacing=22)
        box.add_css_class("amix-hero")
        logo = find_data("amix-logo.png")
        if logo:
            pic = Gtk.Picture.new_for_filename(logo)
            pic.set_size_request(112, 112)
            pic.set_can_shrink(True)
            pic.set_content_fit(Gtk.ContentFit.COVER)
            pic.add_css_class("amix-hero-logo")
            pic.set_overflow(Gtk.Overflow.HIDDEN)
            pic.set_valign(Gtk.Align.CENTER)
            box.append(pic)
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6, valign=Gtk.Align.CENTER)
        col.set_hexpand(True)
        col.append(label("Amiga UNIX, made to measure", ("title-2",)))
        col.append(label("Start from your own AMIX 2.1c core system and add the kernel, drivers, "
                         "desktop and tools that fit your machine."))
        box.append(col)
        return box

    def _group(self, g):
        pg = Adw.PreferencesGroup(title=g.title)
        if g.radio:
            hs = label("Choose one", ("caption", "dim-label"), wrap=False)
            pg.set_header_suffix(hs)
        first = None
        for o in g.options:
            row = Adw.ActionRow(title=o.title, subtitle=o.sub)
            row.set_title_lines(1)
            if g.radio:
                ctl = Gtk.CheckButton(valign=Gtk.Align.CENTER)
                if first is None:
                    first = ctl
                else:
                    ctl.set_group(first)
                row.add_prefix(ctl)
                ctl.connect("toggled", self._on_radio, g, o)
            else:
                ctl = Gtk.Switch(valign=Gtk.Align.CENTER)
                ctl.connect("notify::active", self._on_switch, o)
            if o.badge:
                b = label(o.badge, ("amix-badge",), wrap=False)
                b.set_valign(Gtk.Align.CENTER)
                row.add_suffix(b)
            size = None
            if o.mb:
                size = label("≈ %d MB" % o.mb, ("caption", "dim-label", "numeric"), wrap=False)
                size.set_valign(Gtk.Align.CENTER)
                row.add_suffix(size)
            inst = None
            if o.probe:
                inst = label("installed", ("caption", "success"), wrap=False)
                inst.set_valign(Gtk.Align.CENTER)
                inst.set_tooltip_text("Found on the source disk: kept, not counted again")
                row.add_suffix(inst)
            if not g.radio:
                row.add_suffix(ctl)
            row.set_activatable_widget(ctl)
            pg.add(row)
            self._rows.append((g, o, row, ctl, size, inst))
        return pg

    def _running_note(self):
        box = Gtk.Box(spacing=14)
        box.add_css_class("amix-info")
        img = Gtk.Image.new_from_icon_name("network-server-symbolic")
        img.set_pixel_size(22)
        img.set_valign(Gtk.Align.START)
        box.append(img)
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        col.set_hexpand(True)
        col.append(label("A running system is changed in place", ("heading",)))
        col.append(label("The installer reads what is installed first and offers only changes; "
                         "nothing is removed unless you switch it off. Working over the network "
                         "comes in version 0.4 of the installer."))
        box.append(col)
        self._extra.append(lambda: box.set_visible(self.cfg.in_place()))
        return box

    def _source_group(self):
        pg = Adw.PreferencesGroup(title="Files", description="Chosen paths are not saved in profiles.")

        def file_row(key, title, patterns, folder=False):
            row = Adw.ActionRow(title=title)
            btn = Gtk.Button(label="Choose…", valign=Gtk.Align.CENTER)
            row.add_suffix(btn)
            row.set_activatable_widget(btn)
            btn.connect("clicked", lambda b: self._choose(key, title, patterns, folder))

            def upd():
                p = self.paths.get(key)
                if not p and key == "repo":
                    d = plan.default_repo()
                    row.set_subtitle(d if d else "None yet: \u201cmake repo\u201d in "
                                     "components/desktop/amix-installer publishes one")
                    return
                if not p and key == "kickstart" and plan.fs_uae().a3000_kickstarts():
                    row.set_subtitle("From FS-UAE\u2019s Kickstarts: %s" % os.path.basename(
                        plan.fs_uae().a3000_kickstarts()[0]))
                    return
                if p and key == "out_dir" and self.cfg.sel["target"] in ("card", "sd"):
                    # what the card holds: its configuration, the SCSI IDs in use
                    from . import scsicard
                    info = scsicard.read_card(p) if self.cfg.sel["target"] == "card" \
                        else scsicard.read_z3660(p)
                    row.set_subtitle("%s\n%s" % (p, info.summary()))
                    return
                row.set_subtitle(p if p else ("No folder chosen" if folder else "No file chosen"))
            self._extra.append(upd)
            return row

        hdf = file_row("core_hdf", "Core HDF", ["*.hdf", "*.HDF", "*.img"])
        tape = file_row("tape", "Tape image", ["*.tap", "*.img", "*.tar", "*.bin"])
        flop = file_row("floppy", "Root floppy", ["*.adf", "*.img", "*.ADF"])
        outd = file_row("out_dir", "Folder for the new HDF files", [], folder=True)
        kick = file_row("kickstart", "A3000 Kickstart ROM for FS-UAE", ["*.rom", "*.ROM"])
        patchd = file_row("patch_disk", "AmigaUNIX patch disk (Patch Disks 1 and 2)",
                          ["*.adf", "*.ADF", "*.img"])
        repo = file_row("repo", "Package repository", [], folder=True)
        host = Adw.EntryRow(title="Address of the AMIX system")
        host.connect("changed", lambda e: self.paths.__setitem__("host", e.get_text().strip()))
        for r in (hdf, tape, flop, host, outd, kick, patchd, repo):
            pg.add(r)

        def vis():
            src = self.cfg.sel["src"]
            tgt = self.cfg.sel["target"]
            hdf.set_visible(src == "hdf")
            tape.set_visible(src == "tape")
            flop.set_visible(src == "tape")
            host.set_visible(src == "run")
            outd.set_visible(tgt in ("file", "sd", "card"))
            outd.set_title({"sd": "The hdf folder on the Z3660 SD card",
                            "card": "Folder for the card images (or the card itself)"}.get(
                                tgt, "Folder for the new HDF files"))
            kick.set_visible(src in ("hdf", "tape"))
        self._extra.append(vis)
        return pg

    def _fsuae_group(self):
        """FS-UAE on this host and the "AMIX Installer" configuration in its list."""
        fs = plan.fs_uae()
        pg = Adw.PreferencesGroup(title="FS-UAE",
                                  description="The disks are filled in FS-UAE, over its A2065.")
        st = Adw.ActionRow(title="FS-UAE")
        if fs.installed:
            st.set_subtitle("%s, %s" % (fs.version or "installed", fs.binary))
            ic = Gtk.Image.new_from_icon_name("object-select-symbolic")
            ic.add_css_class("success")
        else:
            st.set_subtitle("Not installed. Install it with: " + fsuae.install_hint())
            ic = Gtk.Image.new_from_icon_name("dialog-warning-symbolic")
            ic.add_css_class("warning")
        st.add_prefix(ic)
        pg.add(st)
        cf = Adw.ActionRow(title="\u201c%s\u201d configuration" % fsuae.CONFIG_NAME)
        if fs.has_config():
            cf.set_subtitle("In FS-UAE\u2019s list: %s" % fs.config_path())
            pg.add(cf)
        elif fs.installed:
            sw = Gtk.Switch(valign=Gtk.Align.CENTER, active=self.paths.get("fsuae_add", True))
            sw.connect("notify::active",
                       lambda w, _p: self.paths.__setitem__("fsuae_add", w.get_active()))
            cf.set_subtitle("Not there yet: create it with the disks, so FS-UAE\u2019s launcher "
                            "lists it (%s)" % os.path.join(os.path.basename(fs.folder(
                                "Configurations")), fsuae.CONFIG_NAME + ".fs-uae"))
            cf.add_suffix(sw)
            cf.set_activatable_widget(sw)
            pg.add(cf)
        if fs.configurations():
            pg.add(Adw.ActionRow(title="Configurations there",
                                 subtitle=", ".join(fs.configurations())))
        self._extra.append(lambda: pg.set_visible(
            self.cfg.sel["src"] in ("hdf", "tape") and
            self.cfg.sel["target"] in ("sd", "card", "file")))
        return pg

    # -- the source image ---------------------------------------------------
    def read_source(self, path):
        """Read the core HDF in a thread; the image is opened read-only."""
        self.cfg.source = None
        self.source_state = ("reading", path)
        self._sync()

        def work():
            try:
                info = hdf.inspect(path, probes=model.all_probes())
                GLib.idle_add(self._source_done, path, info, None)
            except (OSError, IOError, hdf.NotAnImage, hdf.UfsError) as e:
                GLib.idle_add(self._source_done, path, None, str(e))
        threading.Thread(target=work, daemon=True).start()

    def _source_done(self, path, info, err):
        if self.paths.get("core_hdf") != path:
            return False                    # another file was chosen meanwhile
        if err:
            self.source_state = ("error", err)
        else:
            self.cfg.source = info
            self.source_state = ("done", info)
            self.cfg.normalize()
        if self.step == 0:
            self._rebuild_source_view()     # keep the scroll position
            self._sync()
        else:
            self.show_step(self.step)
        return False

    def _source_view(self):
        self.source_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self._rebuild_source_view()
        self._extra.append(lambda: self.source_box.set_visible(self.cfg.sel["src"] == "hdf"))
        return self.source_box

    def _rebuild_source_view(self):
        box = getattr(self, "source_box", None)
        if box is None:
            return
        c = box.get_first_child()
        while c is not None:
            box.remove(c)
            c = box.get_first_child()
        state = getattr(self, "source_state", None)
        if state is None:
            box.set_visible(False)
            return
        pg = Adw.PreferencesGroup(title="On this disk")
        box.append(pg)
        kind, what = state
        if kind == "reading":
            row = Adw.ActionRow(title="Reading the disk\u2026", subtitle=os.path.basename(what))
            sp = Gtk.Spinner(spinning=True, valign=Gtk.Align.CENTER)
            row.add_prefix(sp)
            pg.add(row)
        elif kind == "error":
            row = Adw.ActionRow(title="Not a disk the installer can use", subtitle=what)
            row.add_prefix(Gtk.Image.new_from_icon_name("dialog-warning-symbolic"))
            pg.add(row)
        else:
            for r in source_rows(what):
                pg.add(r)

    def _space_card(self):
        pg = Adw.PreferencesGroup(title="Space")
        src = self.cfg.source
        pg.set_header_suffix(label("from %s" % os.path.basename(src.path) if src else
                                   "example sizes, until a core HDF is read",
                                   ("caption", "dim-label"), wrap=False))
        card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        card.add_css_class("card")
        card.add_css_class("amix-card-pad")
        bars = []
        for _ in range(2):
            b = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            top = Gtk.Box()
            name = label("", ("heading",), wrap=False)
            name.set_hexpand(True)
            text = label("", ("caption", "dim-label", "numeric"), xalign=1.0, wrap=False)
            top.append(name)
            top.append(text)
            bar = SpaceBar()
            b.append(top)
            b.append(bar)
            card.append(b)
            bars.append((b, name, text, bar))
        legend = Gtk.Box(spacing=16)
        for css, t in (("amix-key-core", "Already on the disk"), ("amix-key-sel", "Your selection"),
                       ("amix-key-bad", "Does not fit")):
            k = Gtk.Box(spacing=6)
            sw = Gtk.Box(valign=Gtk.Align.CENTER)
            sw.set_size_request(10, 10)
            sw.add_css_class("amix-key")
            sw.add_css_class(css)
            k.append(sw)
            k.append(label(t, ("caption", "dim-label"), wrap=False))
            legend.append(k)
        card.append(legend)
        warn = label("", ("amix-warning",))
        card.append(warn)
        pg.add(card)

        def upd():
            disks, fits = self.cfg.space()
            for i, (b, name, text, bar) in enumerate(bars):
                b.set_visible(i < len(disks))
                if i < len(disks):
                    d = disks[i]
                    used = d["used"] + d["add"]
                    name.set_text(d["name"])
                    text.set_text("%d MB used, %d MB free" % (used, max(0, d["total"] - used)))
                    bar.set_disk(d)
            warn.set_visible(not fits)
            core = disks[0]
            if not core["fits"] and self.cfg.sel["layout"] == "split":
                warn.set_text("The core disk keeps less than %d MB free even with a data disk "
                              "(%d MB free). Let the installer copy the core system to a larger "
                              "disk." % (core["reserve"], max(0, core["total"] - core["used"])))
            elif not core["fits"]:
                warn.set_text("Your selection does not fit on the core disk with %d MB left free. "
                              "Add a data disk, or let the installer copy the core system to a "
                              "larger disk." % core["reserve"])
            elif not fits:
                warn.set_text("Your selection does not fit on the data disk. Choose a larger one.")
        self._extra.append(upd)
        return pg

    def _summary(self):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        grid = Gtk.Grid(column_spacing=14, row_spacing=14, column_homogeneous=True)
        for n, (s, items) in enumerate(self.cfg.summary()):
            card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
            card.add_css_class("card")
            card.add_css_class("amix-card-pad")
            top = Gtk.Box()
            t = label(s.title, ("heading",), wrap=False)
            t.set_hexpand(True)
            top.append(t)
            ch = Gtk.Button(label="Change")
            ch.add_css_class("flat")
            ch.add_css_class("amix-link")
            ch.set_tooltip_text("Back to %s" % s.title)
            idx = model.step_index(s.id)
            ch.connect("clicked", lambda b, idx=idx: self.show_step(idx))
            top.append(ch)
            card.append(top)
            for it in items:
                r = Gtk.Box(spacing=8)
                ic = Gtk.Image.new_from_icon_name("object-select-symbolic")
                ic.add_css_class("success")
                ic.set_valign(Gtk.Align.START)
                r.append(ic)
                r.append(label(it))
                card.append(r)
            card.set_valign(Gtk.Align.FILL)
            grid.attach(card, n % 2, n // 2, 1, 1)
        box.append(grid)

        nxt = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        nxt.add_css_class("amix-next")
        nxt.append(label("What happens", ("heading",)))
        pl = plan.make_plan(self.cfg, self.paths)
        if pl.existing:
            nxt.append(self._existing_box(pl))
        for p in pl.problems:
            if pl.existing and "not written by the installer" in p:
                continue                # the box above says it, Create disks asks
            r = Gtk.Box(spacing=8)
            ic = Gtk.Image.new_from_icon_name("dialog-warning-symbolic")
            ic.add_css_class("warning")
            ic.set_valign(Gtk.Align.START)
            r.append(ic)
            r.append(label(p[0].upper() + p[1:] + "."))
            nxt.append(r)
        for n, a in enumerate(pl.actions, 1):
            r = Gtk.Box(spacing=8)
            num = label("%d." % n, ("numeric",), wrap=False)
            num.set_valign(Gtk.Align.START)
            r.append(num)
            col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=1)
            col.set_hexpand(True)
            col.append(label(a.title, () if a.available else ("dim-label",)))
            col.append(label(a.detail, ("caption", "dim-label")))
            if a.missing:
                col.append(label(a.missing[0].upper() + a.missing[1:], ("caption", "dim-label")))
            r.append(col)
            nxt.append(r)
        if not pl.actions and not pl.problems:
            nxt.append(label("Nothing to do."))
        btns = Gtk.Box(spacing=10, halign=Gtk.Align.END)
        btns.set_margin_top(4)
        sp = Gtk.Button(label="Save as Profile…")
        sp.set_action_name("win.save-profile")
        btns.append(sp)
        ap = Gtk.Button(label=self.cfg.apply_label())
        ap.add_css_class("suggested-action")
        ap.set_action_name("win.apply")
        btns.append(ap)
        nxt.append(btns)
        box.append(nxt)
        return box

    def _existing_box(self, pl):
        """The files of the same name in the output folder, and what becomes
        of them: overwritten (asked first for a file the installer did not
        write) or kept as .bak; or another folder."""
        out = pl.paths["out_dir"]
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        box.add_css_class("card")
        for m in ("top", "bottom", "start", "end"):
            getattr(box, "set_margin_" + m)(0)
        inner = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        for m in ("top", "bottom", "start", "end"):
            getattr(inner, "set_margin_" + m)(12)
        box.append(inner)
        inner.append(label("Files that exist already in %s" % out, ("heading",)))
        for n, mine in pl.existing:
            r = Gtk.Box(spacing=8)
            ic = Gtk.Image.new_from_icon_name("document-save-symbolic" if mine else
                                              "dialog-warning-symbolic")
            if not mine:
                ic.add_css_class("warning")
            r.append(ic)
            r.append(label(n + ("" if mine else "  \u2014 not written by the installer"),
                           wrap=False))
            inner.append(r)
        backup = self.cfg.sel.get("existing") == "backup"
        mb = pl.existing_bytes() >> 20
        inner.append(label(
            ("Each is renamed to NAME.YYYYMMDD-HHMMSS.bak when the new files are written; "
             "the backups keep %d MB." % mb) if backup else
            ("They are replaced only when every new file is written and checked; a failed "
             "run leaves them as they are."), ("caption", "dim-label")))
        row = Gtk.Box(spacing=8, halign=Gtk.Align.END)
        ow = Gtk.ToggleButton(label="Overwrite", active=not backup)
        bk = Gtk.ToggleButton(label="Keep a Backup", active=backup, group=ow)
        seg = Gtk.Box(css_classes=["linked"])
        seg.append(ow)
        seg.append(bk)

        def pick(btn, choice):
            if btn.get_active() and self.cfg.sel.get("existing") != choice:
                self.cfg.select("existing", choice)
                GLib.idle_add(self._refresh_summary)
        ow.connect("toggled", pick, "overwrite")
        bk.connect("toggled", pick, "backup")
        other = Gtk.Button(label="Choose Another Folder\u2026")
        other.connect("clicked", lambda b: self._choose("out_dir", "Folder for the new disks",
                                                        [], True))
        row.append(other)
        row.append(seg)
        inner.append(row)
        return box

    def _refresh_summary(self):
        adj = self.scroller.get_vadjustment()
        pos = adj.get_value()
        self.show_step(self.step)
        GLib.idle_add(lambda: adj.set_value(pos) and False)
        return False

    # -- state -------------------------------------------------------------
    def _sync(self):
        self._syncing = True
        try:
            for g, o, row, ctl, size, inst in self._rows:
                ok = self.cfg.available(o)
                have = self.cfg.installed(o)
                row.set_sensitive(ok)
                row.set_subtitle(o.sub if ok else model.NOT_AVAILABLE)
                row.set_tooltip_text(None if ok else needs_text(self.cfg, o))
                if size is not None:
                    size.set_visible(ok and not have)
                if inst is not None:
                    inst.set_visible(ok and have)
                on = self.cfg.chosen(g, o)
                if ctl.get_active() != on:
                    ctl.set_active(on)
            for f in self._extra:
                f()
        finally:
            self._syncing = False
        for i, num in enumerate(self.step_nums):
            for c in ("done", "current"):
                num.remove_css_class(c)
            if i < self.step:
                num.add_css_class("done")
            elif i == self.step:
                num.add_css_class("current")
        last = len(model.STEPS) - 1
        self.back_btn.set_sensitive(self.step > 0)
        self.next_btn.set_visible(self.step < last)
        self.next_btn.set_label("Review" if self.step == last - 1 else "Next")

    def _on_radio(self, btn, g, o):
        if self._syncing or not btn.get_active():
            return
        self.cfg.select(g.id, o.id)
        self._sync()

    def _on_switch(self, sw, _pspec, o):
        if self._syncing:
            return
        self.cfg.set_on(o.id, sw.get_active())
        self._sync()

    def _on_next(self, _b):
        self.show_step(self.step + 1)

    # -- dialogs -----------------------------------------------------------
    def toast(self, text):
        self.toasts.add_toast(Adw.Toast.new(text))

    def _filters(self, name, patterns):
        store = Gio.ListStore.new(Gtk.FileFilter)
        f = Gtk.FileFilter()
        f.set_name(name)
        for p in patterns:
            f.add_pattern(p)
        store.append(f)
        a = Gtk.FileFilter()
        a.set_name("All files")
        a.add_pattern("*")
        store.append(a)
        return store

    def _choose(self, key, title, patterns, folder):
        dlg = Gtk.FileDialog(title=title, modal=True)
        if patterns:
            dlg.set_filters(self._filters(title, patterns))

        def done(d, res):
            try:
                f = d.select_folder_finish(res) if folder else d.open_finish(res)
            except GLib.Error:
                return
            self.paths[key] = f.get_path()
            if key == "out_dir":
                self.paths.pop("overwrite_confirmed", None)
            if key == "core_hdf":
                self.read_source(f.get_path())
            self._sync()
            if model.STEPS[self.step].summary:
                self._refresh_summary()
        if folder:
            dlg.select_folder(self, None, done)
        else:
            dlg.open(self, None, done)

    def _save_profile(self):
        dlg = Gtk.FileDialog(title="Save Profile", modal=True, initial_name="amix-profile.json")
        dlg.set_filters(self._filters("AMIX Installer profiles", ["*.json"]))

        def done(d, res):
            try:
                f = d.save_finish(res)
            except GLib.Error:
                return
            try:
                profile.save(self.cfg, f.get_path())
            except OSError as e:
                self.toast("Could not save the profile: %s" % e.strerror)
                return
            self.toast("Profile saved")
        dlg.save(self, None, done)

    def _open_profile(self):
        dlg = Gtk.FileDialog(title="Open Profile", modal=True)
        dlg.set_filters(self._filters("AMIX Installer profiles", ["*.json"]))

        def done(d, res):
            try:
                f = d.open_finish(res)
            except GLib.Error:
                return
            self.load_profile(f.get_path())
        dlg.open(self, None, done)

    def load_profile(self, path):
        try:
            cfg, msgs = profile.load(path)
        except (OSError, ValueError) as e:
            self.toast("Not a profile: %s" % e)
            return False
        cfg.source = self.cfg.source
        cfg.normalize()
        self.cfg = cfg
        self.show_step(self.step)
        self.toast("Profile loaded" + (" with %d notes" % len(msgs) if msgs else ""))
        for m in msgs:
            print("profile: %s" % m, file=sys.stderr)
        return True

    def _reset(self):
        self.cfg = model.Config()
        self.paths = {}
        self.source_state = None
        self.show_step(0)

    def _apply(self):
        if self.cfg.pkg_client_missing():
            d = Adw.AlertDialog(
                heading="Install the package client?",
                body="The AMIX package client (pkg, /var/pkg) is not on the destination system. "
                     "Without it the installer cannot add, update or remove packages there.")
            d.add_response("cancel", "Cancel")
            d.add_response("install", "Install It")
            d.set_response_appearance("install", Adw.ResponseAppearance.SUGGESTED)
            d.set_default_response("install")

            def answer(_d, r):
                if r == "install":
                    self.cfg.set_on("pkg", True)
                    self._sync()
                    self._apply()
            d.connect("response", answer)
            d.present(self)
            return
        pl = plan.make_plan(self.cfg, self.paths)
        foreign = [n for n, mine in pl.existing if not mine]
        if foreign and pl.problems and self.cfg.sel.get("existing") != "backup":
            # the only thing in the way may be those files: ask, by name
            ok = plan.make_plan(self.cfg, dict(self.paths, overwrite_confirmed=True))
            if not ok.problems:
                d = Adw.AlertDialog(
                    heading="Overwrite files the installer did not write?",
                    body="In %s:\n%s\n\nThey are replaced by the new disks and cannot be "
                         "brought back. Keep a backup instead, or choose another folder, "
                         "to keep them." % (self.paths["out_dir"],
                                            "\n".join("\u2022 " + n for n in foreign)))
                d.add_response("cancel", "Cancel")
                d.add_response("backup", "Keep a Backup")
                d.add_response("overwrite", "Overwrite")
                d.set_response_appearance("overwrite", Adw.ResponseAppearance.DESTRUCTIVE)
                d.set_default_response("cancel")

                def answer(_d, r):
                    if r == "overwrite":
                        self.paths["overwrite_confirmed"] = sorted(foreign)
                    elif r == "backup":
                        self.cfg.select("existing", "backup")
                    if r in ("overwrite", "backup"):
                        self._apply()
                d.connect("response", answer)
                d.present(self)
                return
        if pl.problems and self._offer_written():
            return
        if pl.problems:
            self._alert("Not ready to write", "\n".join("\u2022 " + p for p in pl.problems))
            return
        if self.cfg.in_place():
            net = [a for a in pl.actions if a.id == "install"][0]
            if net.missing:
                self._alert("Not ready", net.missing)
            elif plan.fs_uae().installed:
                self._ask_where(net)
            else:
                self._net_page(net, real=True)
            return
        out = self.paths["out_dir"]
        left = plan.leftover(out)
        if left:
            d = Adw.AlertDialog(
                heading="An earlier run was interrupted",
                body="It left these files in %s:\n%s\nRemove them? They are the earlier run\u2019s "
                     "own, unfinished." % (out, "\n".join("\u2022 " + n for n in left)))
            d.add_response("cancel", "Cancel")
            d.add_response("remove", "Remove")
            d.set_response_appearance("remove", Adw.ResponseAppearance.DESTRUCTIVE)

            def answer(_d, r):
                if r == "remove":
                    plan.clean_leftover(out)
                    self._apply()
            d.connect("response", answer)
            d.present(self)
            return
        runs = pl.runnable()
        files = [n for a in runs for n in a.outputs]
        body = "In %s, %d MB:\n%s" % (out, pl.write_bytes() >> 20,
                                       "\n".join("\u2022 " + n for n in files))
        if pl.existing:
            names = ", ".join(n for n, _ in pl.existing)
            body += ("\n\nKept as .bak (%d MB): %s." % (pl.existing_bytes() >> 20, names)
                     if self.cfg.sel.get("existing") == "backup" else
                     "\n\nReplaced when all new files are written and checked: %s." % names)
        if not pl.complete:
            body += ("\n\nThis version does only part of the plan; not yet: %s."
                     % ", ".join(a.title.lower() for a in pl.actions if not a.available))
        d = Adw.AlertDialog(heading="Write the new disks?" if pl.complete else
                            "Write the parts this version can?", body=body)
        d.add_response("cancel", "Cancel")
        d.add_response("write", "Write" if pl.complete else "Write Available Parts")
        d.set_response_appearance("write", Adw.ResponseAppearance.SUGGESTED)
        d.set_default_response("cancel")
        d.connect("response", lambda _d, r: self._run_plan(pl) if r == "write" else None)
        d.present(self)

    def _alert(self, heading, body):
        d = Adw.AlertDialog(heading=heading, body=body)
        d.add_response("close", "Close")
        d.present(self)

    # -- writing -----------------------------------------------------------
    def _run_plan(self, pl):
        self._cancel = False
        self._busy(True)
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        for m in ("top", "bottom", "start", "end"):
            getattr(page, "set_margin_" + m)(28 if m in ("top", "bottom") else 16)
        page.append(label("Writing the disks", ("title-1",)))
        self.run_title = label("Starting\u2026", ("heading",))
        page.append(self.run_title)
        self.run_bar = Gtk.ProgressBar(show_text=True)
        page.append(self.run_bar)
        self.run_text = label("", ("caption", "dim-label"))
        page.append(self.run_text)
        self.run_log = Gtk.TextView(editable=False, monospace=True, cursor_visible=False)
        self.run_log.set_wrap_mode(Gtk.WrapMode.CHAR)
        sw = Gtk.ScrolledWindow(min_content_height=220, vexpand=True)
        sw.add_css_class("card")
        sw.set_child(self.run_log)
        page.append(sw)
        self.run_btns = Gtk.Box(spacing=10, halign=Gtk.Align.END)
        cancel = Gtk.Button(label="Cancel")
        cancel.add_css_class("destructive-action")
        cancel.connect("clicked", lambda b: (setattr(self, "_cancel", True),
                                             b.set_sensitive(False)))
        self.run_btns.append(cancel)
        page.append(self.run_btns)
        self.clamp.set_child(page)
        self.wtitle.set_title("Writing")
        last = [0.0]

        def progress(frac, title, text):
            now = time.monotonic()
            if now - last[0] > 0.1 or frac >= 1.0:
                last[0] = now
                GLib.idle_add(self._run_progress, frac, title, text)

        def log(text):
            GLib.idle_add(self._run_logline, text)

        def work():
            try:
                res = plan.execute(pl, progress, log, lambda: self._cancel)
                GLib.idle_add(self._run_done, pl, res, None)
            except plan.Cancelled:
                GLib.idle_add(self._run_done, pl, None, "Cancelled.")
            except (OSError, IOError, safety.Refused) as e:
                GLib.idle_add(self._run_done, pl, None, str(e))
        threading.Thread(target=work, daemon=True).start()

    def _busy(self, on):
        self.steplist.set_sensitive(not on)
        self.back_btn.set_visible(not on)
        self.next_btn.set_visible(False if on else self.step < len(model.STEPS) - 1)
        for name in ("open-profile", "reset", "apply"):
            self.lookup_action(name).set_enabled(not on)

    def _run_progress(self, frac, title, text):
        self.run_bar.set_fraction(min(1.0, frac))
        self.run_title.set_text(title)
        self.run_text.set_text(text)
        return False

    def _run_logline(self, text):
        buf = self.run_log.get_buffer()
        buf.insert(buf.get_end_iter(), text + "\n")
        self.run_log.scroll_to_iter(buf.get_end_iter(), 0, False, 0, 0)
        return False

    def _run_done(self, pl, res, err):
        c = self.run_btns.get_first_child()
        while c is not None:
            self.run_btns.remove(c)
            c = self.run_btns.get_first_child()
        out = pl.paths["out_dir"]
        if err:
            self.run_title.set_text("Stopped")
            self.run_text.set_text(err + " Nothing this run wrote is left in %s." % out)
            self.run_text.remove_css_class("dim-label")
            self.run_text.add_css_class("error")
        else:
            self.run_bar.set_fraction(1.0)
            self.run_title.set_text("Done")
            self.run_text.set_text("%d files written and checked in %s; their checksums are in "
                                   "SHA256SUMS." % (len(res), out))
            show = Gtk.Button(label="Open Folder")
            show.connect("clicked", lambda b: Gtk.FileLauncher.new(
                Gio.File.new_for_path(out)).launch(self, None, None))
            self.run_btns.append(show)
            net = [a for a in pl.actions if a.id == "install" and a.available]
            if net:
                go = Gtk.Button(label="Install over the Network")
                go.add_css_class("suggested-action")
                go.connect("clicked", lambda b: self._net_page(net[0], real=False, pl=pl))
                self.run_btns.append(go)
        back = Gtk.Button(label="Back to Summary")
        if err or not [a for a in pl.actions if a.id == "install" and a.available]:
            back.add_css_class("suggested-action")
        back.connect("clicked", lambda b: (self._busy(False), self.show_step(self.step)))
        self.run_btns.append(back)
        return False

    def _ask_where(self, net):
        """A running system: on the Amiga itself, or in FS-UAE on this computer?
        The two see this computer at different addresses."""
        from . import netinstall
        d = Adw.AlertDialog(
            heading="Where does this AMIX run?",
            body="On an Amiga, AMIX fetches the bootstrap from this computer over the LAN "
                 "(%s). In FS-UAE here, it reaches this computer through FS-UAE\u2019s network "
                 "as %s, and the installer can start FS-UAE for you."
                 % (netinstall.lan_address(), netinstall.FS_UAE_HOST))
        d.add_response("cancel", "Cancel")
        d.add_response("amiga", "On the Amiga")
        d.add_response("fsuae", "In FS-UAE Here")
        d.set_response_appearance("fsuae", Adw.ResponseAppearance.SUGGESTED)
        d.set_default_response("fsuae")
        d.connect("response", lambda _d, r: r in ("amiga", "fsuae") and
                  self._net_page(net, real=True, emu=(r == "fsuae")))
        d.present(self)

    def _fsuae_start(self, btns, info, cfgf):
        """The Start FS-UAE button, with the configurations to start from; or a
        row that says why FS-UAE cannot be started from here."""
        fs = plan.fs_uae()
        if not fs.installed:
            info.add(Adw.ActionRow(title="FS-UAE is not installed here",
                                   subtitle="Install it with: " + fsuae.install_hint()))
            return
        paths = []
        if cfgf:
            paths.append(cfgf)
        for n in fs.configurations():
            p = fs.config_path(n)
            if p not in paths:
                paths.append(p)
        if not paths:
            info.add(Adw.ActionRow(
                title="No FS-UAE configuration",
                subtitle="None in %s. Write the disks with this installer (it writes %s), or "
                         "make an A3000 configuration with network_card = a2065 in FS-UAE\u2019s "
                         "launcher." % (fs.folder("Configurations"), plan.FSUAE_CONFIG)))
            return
        # the ones with the A2065 first: the others cannot reach this computer
        paths.sort(key=lambda p: fs.network_of(p) != "a2065")
        names = [os.path.basename(p)[:-7] if p.endswith(".fs-uae") else p for p in paths]
        row = Adw.ComboRow(title="FS-UAE configuration", model=Gtk.StringList.new(names))
        info.add(row)

        def check(*_a):
            p = paths[row.get_selected()]
            card = fs.network_of(p)
            if card == "a2065":
                row.set_subtitle("With an A2065: AMIX must be 10.0.2.15 (the amigaunix line of "
                                 "/etc/inet/hosts)")
                start.set_sensitive(True)
            else:
                row.set_subtitle("No A2065 (network_card = %s): AMIX cannot reach this "
                                 "computer. Add network_card = a2065 in FS-UAE\u2019s launcher."
                                 % (card or "none"))
                start.set_sensitive(False)
        start = Gtk.Button(label="Start FS-UAE")
        start.add_css_class("suggested-action")
        start.connect("clicked", lambda b: subprocess.Popen(
            [fs.binary, paths[row.get_selected()]], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, start_new_session=True))
        row.connect("notify::selected", check)
        check()
        btns.append(start)

    def _offer_written(self):
        """The out folder already holds the disks of this plan (an earlier run):
        offer the network install with them instead of writing them again."""
        if self.cfg.in_place():
            return False
        pl = plan.make_plan(self.cfg, self.paths, written=True)
        net = [a for a in pl.actions if a.id == "install" and a.available]
        if pl.problems or not net:
            return False
        d = Adw.AlertDialog(
            heading="The disks are already there",
            body="%s holds the disks of this selection from an earlier run. Install over the "
                 "network with them? To write new ones, choose another folder."
                 % self.paths["out_dir"])
        d.add_response("cancel", "Cancel")
        d.add_response("net", "Install over the Network")
        d.set_response_appearance("net", Adw.ResponseAppearance.SUGGESTED)
        d.set_default_response("net")
        d.connect("response", lambda _d, r: r == "net" and self._net_page(net[0], False, pl))
        d.present(self)
        return True

    # -- the network install ---------------------------------------------------
    def _net_page(self, net, real, pl=None, emu=False):
        """Serve the bootstrap and the repository; show what to type; log AMIX."""
        from . import netinstall
        data_disk = None
        if pl is not None and not real:
            try:
                data_disk = netinstall.fs_uae_data_disk(pl)
            except (OSError, hdf.NotAnImage) as e:
                self._alert("The data disk cannot be read", str(e))
                return
        emu = emu or not real           # AMIX in FS-UAE: this computer is 10.0.2.2
        host = netinstall.FS_UAE_HOST if emu else netinstall.lan_address()
        try:
            self.net = netinstall.Session(self.cfg, net.repo, host=host, data_disk=data_disk,
                                          kernel=netinstall.plan_kernel(pl) if pl else None,
                                          patch=self.paths.get("patch_disk"),
                                          report=netinstall.report_path(pl) if pl else None,
                                          log=lambda t: GLib.idle_add(self._run_logline, t))
            self.net.start()
        except OSError as e:
            self._alert("The servers cannot start", "%s (UDP %d, TCP %d)" % (
                e.strerror or e, netinstall.TFTP_PORT, netinstall.HTTP_PORT))
            self.net = None
            return
        self._busy(True)
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        for m in ("top", "bottom", "start", "end"):
            getattr(page, "set_margin_" + m)(28 if m in ("top", "bottom") else 16)
        page.append(label("Install over the network", ("title-1",)))
        page.append(label(("In FS-UAE" if emu else "On the Amiga") +
                          ", log in as root at the console and type these lines. AMIX "
                          "fetches a small bootstrap from this computer and installs "
                          "everything with its own tools; its progress shows below.",
                          ("dim-label",)))
        cmd = Gtk.Label(label="\n".join(self.net.commands()), xalign=0, selectable=True)
        cmd.add_css_class("monospace")
        cmd.add_css_class("card")
        cmd.add_css_class("amix-card-pad")
        page.append(cmd)
        info = Adw.PreferencesGroup()
        info.add(Adw.ActionRow(title="Serving", subtitle="TFTP on UDP %d, HTTP on TCP %d, as %s"
                               % (self.net.tftp.port, self.net.http_port, host)))
        info.add(Adw.ActionRow(title="Package repository", subtitle=net.repo))
        if data_disk:
            info.add(Adw.ActionRow(title="Data disk",
                                   subtitle="%s, %d sectors: AMIX makes its file system"
                                   % data_disk))
        if not emu:
            info.add(Adw.ActionRow(title="The Amiga\u2019s network",
                                   subtitle="Its A2065 must be up and reach %s (ping %s)"
                                   % (host, host)))
        page.append(info)
        self.run_title = label("Waiting for AMIX\u2026", ("heading",))
        page.append(self.run_title)
        self.run_log = Gtk.TextView(editable=False, monospace=True, cursor_visible=False)
        self.run_log.set_wrap_mode(Gtk.WrapMode.CHAR)
        sw = Gtk.ScrolledWindow(min_content_height=200, vexpand=True)
        sw.add_css_class("card")
        sw.set_child(self.run_log)
        page.append(sw)
        btns = Gtk.Box(spacing=10, halign=Gtk.Align.END)
        if emu:
            fs = plan.fs_uae()
            cfgf = fs.config_path() if fs.has_config() else None
            if pl is not None:
                own = os.path.join(pl.paths["out_dir"], plan.FSUAE_CONFIG)
                cfgf = own if os.path.exists(own) else cfgf
            self._fsuae_start(btns, info, cfgf)
        stop = Gtk.Button(label="Stop and Go Back")
        stop.connect("clicked", lambda b: self._net_stop())
        btns.append(stop)
        page.append(btns)
        self.clamp.set_child(page)
        self.wtitle.set_title("Network install")
        GLib.timeout_add(500, self._net_poll)

    def _net_poll(self):
        n = getattr(self, "net", None)
        if n is None:
            return False
        if n.done:
            self.run_title.set_text("Done: AMIX is installed")
        elif n.failed:
            self.run_title.set_text("Stopped on AMIX: see the log")
        elif n.messages:
            self.run_title.set_text(n.messages[-1][2])
        return True

    def _net_stop(self):
        if getattr(self, "net", None):
            self.net.stop()
            self.net = None
        self._busy(False)
        self.show_step(self.step)

    def _about(self):
        a = Adw.AboutDialog(application_name="AMIX Installer", application_icon=APP_ID,
                            version=__version__, developer_name="isoriano1968",
                            license_type=Gtk.License.MIT_X11,
                            website="https://github.com/isoriano1968/amix-userland",
                            comments="Makes an Amiga UNIX disk for your machine from your own "
                                     "AMIX 2.1c core system.")

        def link(dlg, uri):
            # the browser opens on its own; the dialog closes, so it never
            # stays over the page the user comes back to
            Gtk.UriLauncher(uri=uri).launch(self, None, None)
            dlg.close()
            return True
        a.connect("activate-link", link)
        a.present(self)
        self._about_dialog = a


class App(Adw.Application):
    def __init__(self, args):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.NON_UNIQUE)
        self.args = args
        self.win = None
        self.connect("activate", self._activate)
        self.connect("startup", self._startup)

    def _startup(self, _app):
        display = Gdk.Display.get_default()
        theme = Gtk.IconTheme.get_for_display(display)
        comp = os.path.dirname(HERE)
        for d in [os.path.join(d, "..", "icons") for d in data_dirs()] + [
                os.path.join(comp, "build", "icons")]:
            d = os.path.normpath(d)
            if os.path.isdir(os.path.join(d, "hicolor")):
                theme.add_search_path(d)
        css = find_data("style.css")
        if css:
            prov = Gtk.CssProvider()
            prov.load_from_path(css)
            Gtk.StyleContext.add_provider_for_display(display, prov,
                                                      Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        # GTK < 4.20 has no @media in CSS: the dark additions are their own file
        dark = find_data("style-dark.css")
        if dark:
            dprov = Gtk.CssProvider()
            dprov.load_from_path(dark)
            sm = Adw.StyleManager.get_default()

            def follow(*_a):
                if sm.get_dark():
                    Gtk.StyleContext.add_provider_for_display(
                        display, dprov, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION + 1)
                else:
                    Gtk.StyleContext.remove_provider_for_display(display, dprov)
            sm.connect("notify::dark", follow)
            follow()
        q = Gio.SimpleAction.new("quit", None)
        q.connect("activate", lambda a, p: self.quit())
        self.add_action(q)
        self.set_accels_for_action("app.quit", ["<Control>q"])

    def _activate(self, _app):
        if self.win is None:
            self.win = Window(self)
            if self.args.profile:
                self.win.load_profile(self.args.profile)
            if self.args.out:
                self.win.paths["out_dir"] = os.path.abspath(self.args.out)
            if self.args.source:
                self.win.paths["core_hdf"] = os.path.abspath(self.args.source)
                self.win.read_source(self.win.paths["core_hdf"])
        self.win.present()
        if self.args.screenshot:
            from . import shot
            shot.run(self.win, self.args.screenshot)


def main(args):
    Adw.init()
    app = App(args)
    return app.run([sys.argv[0]])
