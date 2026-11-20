"""The Build tab: AMIX packages built on this computer from amix-userland
(build.py), the components that are new or newer than the repository served,
and the build log.  What it builds lands in the local repository, which
Serve Packages then serves."""
import json
import os
import sys
import threading
import time

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk  # noqa: E402

from . import build, plan  # noqa: E402

LOG_LINES = 400
AMIX_CHECK_GAP = 300        # s: a pkg refresh on AMIX checks the source at most this often
SETTINGS = os.path.expanduser("~/.config/amix-installer/build.json")
STATE_TEXT = {"new": "new", "newer": "newer version", "changed": "changed since published",
              "same": "as published"}


def _load():
    try:
        with open(SETTINGS) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save(d):
    try:
        os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
        with open(SETTINGS, "w") as f:
            json.dump(d, f)
    except OSError:
        pass


class BuildPage(object):
    """build() the page; refresh() when the tab is shown."""

    def __init__(self, win, tabs):
        self.win = win
        self.tabs = tabs
        st = _load()
        self.source = st.get("source", build.DEFAULT_SOURCE)
        self.auto = bool(st.get("auto_build", False))
        self.last_check = 0.0
        self.rows = []              # (stem, state, ...) from build.compare
        self.chosen = set()
        self.show_all = False
        self.busy = None            # "look" or "build" while a thread runs
        self.lines = []
        self.src_dir = None

    # -- page ----------------------------------------------------------------
    def build(self):
        tv = Adw.ToolbarView()
        hb = Adw.HeaderBar()
        hb.pack_start(self.tabs("build"))
        hb.set_title_widget(Adw.WindowTitle(title="AMIX Installer", subtitle="Build Packages"))
        tv.add_top_bar(hb)
        sc = Gtk.ScrolledWindow(vexpand=True)
        sc.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        clamp = Adw.Clamp(maximum_size=760, tightening_threshold=560)
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=24)
        for m in ("top", "bottom", "start", "end"):
            getattr(self.box, "set_margin_" + m)(28 if m in ("top", "bottom") else 16)
        clamp.set_child(self.box)
        sc.set_child(clamp)
        tv.set_content(sc)
        self.refresh()
        # at start: the source brought up to date (git pull) and compared
        # (not in a screenshot run: those show prepared states)
        if not any(k.startswith("AMIX_SHOT") for k in os.environ) and "--screenshot" not in sys.argv:
            GLib.timeout_add_seconds(2, lambda: (self.look(reason="start"), False)[1])
        return tv

    def _clear(self):
        c = self.box.get_first_child()
        while c is not None:
            n = c.get_next_sibling()
            self.box.remove(c)
            c = n

    def refresh(self):
        self._clear()
        bad = build.problems()
        # status
        st = Gtk.Box(spacing=18)
        icon = Gtk.Image.new_from_icon_name("applications-engineering-symbolic")
        icon.set_pixel_size(48)
        icon.add_css_class("dim-label" if bad else "accent")
        st.append(icon)
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
        t = Gtk.Label(xalign=0, wrap=True)
        t.add_css_class("title-2")
        t.set_label("Building…" if self.busy == "build" else
                    "This computer cannot build yet" if bad else "Build AMIX packages here")
        col.append(t)
        sub = Gtk.Label(xalign=0, wrap=True)
        sub.add_css_class("dim-label")
        sub.set_label("; ".join(bad) if bad else
                      "From amix-userland, with the AMIX compiler and sysroot installed on this "
                      "computer, through the same checks as every published package. What you "
                      "build goes into your own repository, which Serve Packages serves.")
        col.append(sub)
        st.append(col)
        self.box.append(st)

        pg = Adw.PreferencesGroup(title="Source",
                                  description="An amix-userland checkout on this computer, or a "
                                  "git URL (#branch or #commit after it). It is updated (git pull, "
                                  "keeping your own edits) when the installer starts, on Refresh, "
                                  "and when an Amiga runs pkg refresh against Serve Packages.")
        er = Adw.EntryRow(title="amix-userland")
        er.set_text(self.source)
        er.set_sensitive(self.busy is None)
        er.connect("changed", lambda e: setattr(self, "source", e.get_text().strip()))
        look = Gtk.Button(label="Refresh", valign=Gtk.Align.CENTER)
        look.set_sensitive(self.busy is None)
        look.connect("clicked", lambda _b: self.look())
        er.add_suffix(look)
        pg.add(er)
        ab = Adw.SwitchRow(title="Build what is newer or changed by itself",
                           subtitle="After each update of the source: the packages are published for "
                           "the next pkg update on AMIX")
        ab.set_active(self.auto)
        ab.connect("notify::active", lambda s, _p: self._set_auto(s.get_active()))
        pg.add(ab)
        self.box.append(pg)

        local = os.path.isfile(os.path.join(build.LOCAL_REPO, "pkg5.repository"))
        pg = Adw.PreferencesGroup(title="Components",
                                  description=(("Compared with your repository (%s)" % build.LOCAL_REPO)
                                               if local else "Compared with the bundled repository")
                                  + ". AMIX packages only: the AMIX Installer itself is a host app, "
                                  "updated with pkg (OpenIndiana) or apt (Debian).")
        if self.busy == "look":
            pg.add(Adw.ActionRow(title="Looking…", subtitle="Fetching and comparing the source"))
        elif not self.rows:
            pg.add(Adw.ActionRow(title="Not compared yet",
                                 subtitle="Refresh lists what is new or newer than the repository"))
        else:
            shown = [r for r in self.rows if self.show_all or r[4] != "same"]
            if not shown:
                pg.add(Adw.ActionRow(title="Everything is as published"))
            for stem, rv, sv, rev, state in shown:
                row = Adw.ActionRow(title=stem, subtitle="%s · source %s (revision %s) · published %s"
                                    % (STATE_TEXT[state], sv, rev, rv or "never"))
                cb = Gtk.CheckButton(valign=Gtk.Align.CENTER)
                cb.set_active(stem in self.chosen)
                cb.set_sensitive(self.busy is None)
                cb.connect("toggled", self._toggle, stem)
                row.add_prefix(cb)
                row.set_activatable_widget(cb)
                pg.add(row)
            sw = Adw.SwitchRow(title="Show all components")
            sw.set_active(self.show_all)
            sw.connect("notify::active", lambda s, _p: (setattr(self, "show_all", s.get_active()),
                                                         self.refresh()))
            pg.add(sw)
        self.box.append(pg)

        bar = Gtk.Box(spacing=12, halign=Gtk.Align.END)
        b = Gtk.Button(label="Build %d" % len(self.chosen) if self.chosen else "Build")
        b.add_css_class("suggested-action")
        b.add_css_class("pill")
        b.set_sensitive(bool(self.chosen) and not bad and self.busy is None)
        b.connect("clicked", lambda _b: self.start_build())
        bar.append(b)
        self.box.append(bar)

        pg = Adw.PreferencesGroup(title="Log")
        frame = Gtk.Frame()
        self.logview = Gtk.TextView(editable=False, cursor_visible=False, monospace=True)
        self.logview.set_wrap_mode(Gtk.WrapMode.NONE)
        for m in ("top", "bottom", "left", "right"):
            getattr(self.logview, "set_" + m + "_margin")(10)
        self.logview.get_buffer().set_text("\n".join(self.lines) if self.lines else "Nothing built yet.")
        lsc = Gtk.ScrolledWindow(min_content_height=220)
        lsc.set_child(self.logview)
        frame.set_child(lsc)
        pg.add(frame)
        self.box.append(pg)

    def _toggle(self, cb, stem):
        (self.chosen.add if cb.get_active() else self.chosen.discard)(stem)
        self.refresh()

    # -- work ----------------------------------------------------------------
    def _log(self, s):
        GLib.idle_add(self._add_line, s)

    def _add_line(self, line):
        self.lines = (self.lines + [line])[-LOG_LINES:]
        buf = self.logview.get_buffer()
        buf.set_text("\n".join(self.lines))
        self.logview.scroll_to_iter(buf.get_end_iter(), 0.0, False, 0.0, 1.0)
        return False

    def _repo(self):
        if os.path.isfile(os.path.join(build.LOCAL_REPO, "pkg5.repository")):
            return build.LOCAL_REPO
        return plan.default_repo()

    def _save(self):
        _save({"source": self.source, "auto_build": self.auto})

    def _set_auto(self, on):
        self.auto = on
        self._save()

    def look(self, reason="refresh"):
        """The source updated (git pull) and compared, in a thread; reason:
        start, refresh (the button), amix (a pkg refresh on an Amiga)."""
        if self.busy is not None:
            return
        self._save()
        self.busy = "look"
        self.last_check = time.time()
        self.refresh()

        def work():
            try:
                d = build.source_dir(self.source, self._log, update=True)
                rows = build.compare(d, self._repo())
                err = None
            except (build.BuildError, OSError) as e:
                d, rows, err = None, [], str(e)
            GLib.idle_add(self._looked, d, rows, err, reason)
        threading.Thread(target=work, daemon=True).start()

    def check_from_amix(self, client):
        """Serve Packages saw an Amiga fetch the catalog (pkg refresh)."""
        if self.busy is None and time.time() - self.last_check >= AMIX_CHECK_GAP:
            self._add_line("pkg refresh from %s: checking the source" % client)
            self.look(reason="amix")
        return False

    def _looked(self, d, rows, err, reason="refresh"):
        self.busy = None
        self.src_dir = d
        self.rows = rows
        self.chosen &= {r[0] for r in rows}
        if err:
            self.win.toast(err)
            self.refresh()
            return False
        todo = [r[0] for r in rows if r[4] in ("newer", "changed")]
        if todo and reason != "refresh":
            self.win.toast("amix-userland: %d component%s newer or changed than the repository"
                           % (len(todo), "" if len(todo) == 1 else "s"))
        self.refresh()
        if todo and self.auto and not build.problems():
            self.chosen = set(todo)
            self.start_build()
        return False

    def start_build(self):
        stems = sorted(self.chosen)
        self.busy = "build"
        self.lines = []
        self.refresh()

        def work():
            try:
                repo, done = build.build_many(self.src_dir or self.source, stems, log=self._log)
                err = None
            except (build.BuildError, OSError) as e:
                repo, done, err = None, [], str(e)
            GLib.idle_add(self._built, repo, done, err)
        threading.Thread(target=work, daemon=True).start()

    def _built(self, repo, done, err):
        self.busy = None
        if err:
            self._add_line("FAILED: " + err)
            self.win.toast("The build failed: see the log")
        else:
            new = [f for _, f in done if f]
            self.win.toast("%d package%s published into your repository" % (len(new), "" if len(new) == 1 else "s")
                           if new else "Nothing new: the same content as published")
            self.win.serve.repo = repo          # Serve Packages serves it from now on
            self.chosen.clear()
            if self.src_dir:
                self.rows = build.compare(self.src_dir, repo)
        self.refresh()
        return False
