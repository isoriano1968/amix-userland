"""The window's update banner: at start and once a day the installer asks
selfupdate.check() in a thread (never blocking the window, silent offline);
a newer version shows a banner, its button a dialog with the release notes
and Later / What's New / Update.  The update runs through pkg or apt
(selfupdate.apply), never during a build or an install, and offers a
restart.  The main menu's "Check for Updates" turns it off."""
import os
import sys
import threading

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk  # noqa: E402

from . import selfupdate  # noqa: E402

NOTES_CHARS = 1500


def _plain(md):
    """Release notes for a dialog body: no markdown markup, not too long."""
    out = []
    for line in (md or "").splitlines():
        line = line.replace("**", "").replace("`", "")
        if line.startswith("#"):
            line = line.lstrip("# ").upper() if line.lstrip("#").strip() else ""
        if line.startswith("|") or line.strip() in ("```", "```sh", "```bash"):
            continue
        out.append(line)
    text = "\n".join(out).strip()
    return text if len(text) <= NOTES_CHARS else text[:NOTES_CHARS].rsplit("\n", 1)[0] + "\n…"


class UpdateBar(object):
    def __init__(self, win):
        self.win = win
        self.update = None
        self.banner = Adw.Banner(button_label="Update…", revealed=False)
        self.banner.connect("button-clicked", lambda _b: self._ask())
        self._timer = None

    def action(self):
        """win.check-updates: the menu's switch, kept in the settings."""
        a = Gio.SimpleAction.new_stateful("check-updates", None,
                                          GLib.Variant.new_boolean(selfupdate.settings()["check"]))
        a.connect("change-state", self._toggle)
        return a

    def _toggle(self, a, value):
        a.set_state(value)
        st = selfupdate.settings()
        st["check"] = value.get_boolean()
        selfupdate.save_settings(st)
        if st["check"]:
            self.start()
        else:
            self.banner.set_revealed(False)

    def start(self):
        if any(k.startswith("AMIX_SHOT") for k in os.environ) or "--screenshot" in sys.argv:
            return                      # screenshots show prepared states
        if not selfupdate.settings()["check"]:
            return
        threading.Thread(target=lambda: GLib.idle_add(self._checked, selfupdate.check()),
                         daemon=True).start()
        if self._timer is None:
            self._timer = GLib.timeout_add_seconds(selfupdate.DAY, self._daily)

    def _daily(self):
        self.start()
        return True

    def _checked(self, u):
        if selfupdate.wanted(u):
            self.update = u
            self.banner.set_title("AMIX Installer %s is available" % u.version)
            self.banner.set_revealed(True)
        return False

    def _ask(self):
        u = self.update
        if u is None:
            return
        how = ("the release's .p5p (about 800 MB), checked against its SHA256SUMS, with pkg update"
               if u.p5p else "pkg update (OpenIndiana)" if u.kind == "oi" else
               "the release's .deb, checked against its SHA256SUMS, with apt")
        d = Adw.AlertDialog(heading="AMIX Installer %s" % u.version,
                            body="You have %s. The update runs through %s.\n\n%s"
                            % (selfupdate.__version__, how, _plain(u.notes) or "No release notes."))
        d.add_response("later", "Later")
        if u.url:
            d.add_response("notes", "What's New")
        d.add_response("update", "Update")
        d.set_response_appearance("update", Adw.ResponseAppearance.SUGGESTED)
        d.set_default_response("update")
        d.set_close_response("later")
        d.connect("response", self._answer)
        d.present(self.win)

    def _answer(self, _d, r):
        u = self.update
        if r == "later":
            st = selfupdate.settings()
            st["later"] = u.version         # hidden until the next version
            selfupdate.save_settings(st)
            self.banner.set_revealed(False)
        elif r == "notes":
            Gtk.UriLauncher.new(u.url).launch(self.win, None, None, None)
        elif r == "update":
            why = self.win.busy_reason()
            if why:
                self.win.toast("The update waits until %s is done" % why)
                return
            self.banner.set_title("Updating to %s…" % u.version)
            self.banner.set_button_label(None)

            def work():
                ok, text = selfupdate.apply(u)
                GLib.idle_add(self._applied, ok, text)
            threading.Thread(target=work, daemon=True).start()

    def _applied(self, ok, text):
        if not ok:
            self.banner.set_title("AMIX Installer %s is available" % self.update.version)
            self.banner.set_button_label("Update…")
            self.win.toast(text)
            return False
        self.banner.set_revealed(False)
        d = Adw.AlertDialog(heading="AMIX Installer %s is installed" % self.update.version,
                            body="Restart the installer to use the new version.")
        d.add_response("later", "Not Now")
        d.add_response("restart", "Restart")
        d.set_response_appearance("restart", Adw.ResponseAppearance.SUGGESTED)
        d.connect("response", lambda _d, r: r == "restart" and self._restart())
        d.present(self.win)
        return False

    def _restart(self):
        self.win.close()
        os.execv(sys.executable, [sys.executable] + sys.argv)
