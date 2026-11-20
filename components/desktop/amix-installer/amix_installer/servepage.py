"""The Serve Packages tab: this computer as the package server of AMIX
machines (serve.py), with the address lines to type on AMIX, the repository
served, and what the Amigas fetch."""
import os

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gdk, GLib, Gtk, Pango  # noqa: E402

from . import plan, serve  # noqa: E402

LOG_LINES = 200


class ServePage(object):
    """build() the page; the window calls stop() when it closes."""

    def __init__(self, win, tabs):
        self.win = win
        self.tabs = tabs            # tabs("serve"): the Install / Serve Packages switch
        self.srv = None
        self.repo = plan.default_repo()
        self.discovery = True
        self.local = False
        self.lines = []

    # -- page ----------------------------------------------------------------
    def build(self):
        tv = Adw.ToolbarView()
        hb = Adw.HeaderBar()
        hb.pack_start(self.tabs("serve"))
        hb.set_title_widget(Adw.WindowTitle(title="AMIX Installer", subtitle="Serve Packages"))
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
        return tv

    def _clear(self):
        c = self.box.get_first_child()
        while c is not None:
            n = c.get_next_sibling()
            self.box.remove(c)
            c = n

    def refresh(self):
        self._clear()
        on = self.srv is not None
        # status: icon, what, the one button
        st = Gtk.Box(spacing=18)
        icon = Gtk.Image.new_from_icon_name("network-server-symbolic")
        icon.set_pixel_size(48)
        icon.add_css_class("dim-label" if not on else "accent")
        st.append(icon)
        col = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2, hexpand=True)
        t = Gtk.Label(xalign=0, wrap=True)
        t.add_css_class("title-2")
        if on:
            t.set_label("Serving %d packages" % serve.package_count(self.srv.repo))
        else:
            t.set_label("Not serving")
        col.append(t)
        sub = Gtk.Label(xalign=0, wrap=True)
        sub.add_css_class("dim-label")
        sub.set_label("Your Amigas install and update from this computer." if on else
                      "Start lets AMIX machines (FS-UAE here, or Amigas on the network) install "
                      "and update packages from this computer, with pkg or the AMIX Software Center.")
        col.append(sub)
        st.append(col)
        b = Gtk.Button(label="Stop" if on else "Start")
        b.set_valign(Gtk.Align.CENTER)
        b.add_css_class("destructive-action" if on else "suggested-action")
        b.add_css_class("pill")
        b.connect("clicked", lambda _b: self.stop_serving() if on else self.start_serving())
        st.append(b)
        self.box.append(st)

        if on:
            pg = Adw.PreferencesGroup(title="Type this on AMIX",
                                      description="As root, in a terminal, or with Repositories → Add "
                                      "in the AMIX Software Center.")
            for what, url in self.srv.urls():
                row = Adw.ActionRow(title=what, subtitle=serve.command(url))
                row.set_subtitle_selectable(True)
                row.add_css_class("property")
                cp = Gtk.Button(label="Copy", valign=Gtk.Align.CENTER)
                cp.connect("clicked", lambda _b, s=serve.command(url): self._copy(s))
                row.add_suffix(cp)
                pg.add(row)
            self.box.append(pg)

        pg = Adw.PreferencesGroup(title="Repository")
        if self.repo:
            why = plan.repo_problem(self.repo)
            sub = (why if why else "%s · %s · %d packages" % (
                self.repo, serve.repo_date(self.repo), serve.package_count(self.repo)))
        else:
            sub = plan.NO_REPO
        row = Adw.ActionRow(title="Packages served", subtitle=sub)
        row.set_subtitle_selectable(True)
        ch = Gtk.Button(label="Choose…", valign=Gtk.Align.CENTER)
        ch.set_sensitive(not on)
        ch.connect("clicked", lambda _b: self._choose())
        row.add_suffix(ch)
        pg.add(row)
        sw = Adw.SwitchRow(title="Answer “Find on Network”",
                           subtitle="The AMIX Software Center finds this computer by itself "
                           "(UDP port %d)" % serve.DISCOVERY_PORT)
        sw.set_active(self.discovery)
        sw.set_sensitive(not on)
        sw.connect("notify::active", lambda s, _p: setattr(self, "discovery", s.get_active()))
        pg.add(sw)
        lo = Adw.SwitchRow(title="Only this computer (FS-UAE)",
                           subtitle="Listen on 127.0.0.1 only: no Amiga on the network can connect")
        lo.set_active(self.local)
        lo.set_sensitive(not on)
        lo.connect("notify::active", lambda s, _p: setattr(self, "local", s.get_active()))
        pg.add(lo)
        self.box.append(pg)

        pg = Adw.PreferencesGroup(title="Requests")
        frame = Gtk.Frame()
        self.logview = Gtk.TextView(editable=False, cursor_visible=False, monospace=True)
        self.logview.set_wrap_mode(Gtk.WrapMode.NONE)
        for m in ("top", "bottom", "left", "right"):
            getattr(self.logview, "set_" + m + "_margin")(10)
        self.logview.get_buffer().set_text("\n".join(self.lines) if self.lines else
                                           "Nothing fetched yet.")
        lsc = Gtk.ScrolledWindow(min_content_height=150)
        lsc.set_child(self.logview)
        frame.set_child(lsc)
        pg.add(frame)
        self.box.append(pg)

    # -- actions -------------------------------------------------------------
    def _copy(self, text):
        Gdk.Display.get_default().get_clipboard().set(text)
        self.win.toast("Copied: %s" % text)

    def _choose(self):
        dlg = Gtk.FileDialog(title="The AMIX Package Repository", modal=True)

        def done(d, res):
            try:
                f = d.select_folder_finish(res)
            except GLib.Error:
                return
            why = plan.repo_problem(f.get_path())
            if why:
                self.win.toast(why)
                return
            self.repo = f.get_path()
            self.refresh()
        dlg.select_folder(self.win, None, done)

    def _log(self, t, client, rel, code, nbytes):
        line = "%s  %-15s %-44s %s%s" % (t, client, rel[:44], code,
                                         "  %s" % _size(nbytes) if nbytes else "")
        GLib.idle_add(self._add_line, line)

    def _add_line(self, line):
        self.lines = (self.lines + [line])[-LOG_LINES:]
        buf = self.logview.get_buffer()
        buf.set_text("\n".join(self.lines))
        end = buf.get_end_iter()
        self.logview.scroll_to_iter(end, 0.0, False, 0.0, 1.0)
        return False

    def start_serving(self):
        if not self.repo or plan.repo_problem(self.repo):
            self.win.toast(plan.repo_problem(self.repo) if self.repo else plan.NO_REPO)
            return
        if self.win.net is not None:
            self.win.toast("A network install is running: it serves the packages itself")
            return
        try:
            self.srv = serve.RepoServer(self.repo, local=self.local, discovery=self.discovery,
                                        log=self._log).start()
        except OSError as e:
            self.srv = None
            self.win._alert("Packages cannot be served",
                            "Port %d (HTTP) or %d (UDP) is in use: %s.  Is another server "
                            "running, such as a network install?" % (serve.HTTP_PORT,
                                                                     serve.DISCOVERY_PORT,
                                                                     e.strerror or e))
            return
        self.lines = []
        self.refresh()

    def stop_serving(self):
        self.stop()
        self.refresh()

    def stop(self):
        if self.srv is not None:
            self.srv.stop()
            self.srv = None

    @property
    def serving(self):
        return self.srv is not None


def _size(n):
    if n >= 1 << 20:
        return "%.1f MB" % (n / float(1 << 20))
    if n >= 1 << 10:
        return "%d KB" % (n >> 10)
    return "%d bytes" % n
