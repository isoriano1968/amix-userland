"""Renders the window to PNG files without showing it to anyone (tests, docs).

Run under a virtual X server: amix-installer --screenshot DIR.
"""
import os

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gsk", "4.0")
gi.require_version("Graphene", "1.0")
from gi.repository import Gdk, GLib, Graphene, Gsk, Gtk  # noqa: E402

from . import model  # noqa: E402

# (file name, radio choices, switches, step)
SCENES = [("%d-%s" % (i + 1, s.id), {}, {}, i) for i, s in enumerate(model.STEPS)] + [
    ("x-a2500-a2630-machine", {"mach": "a2500", "accel": "a2630"}, {}, model.step_index("machine")),
    ("x-a2500-a2630-drivers", {"mach": "a2500", "accel": "a2630"}, {}, model.step_index("drivers")),
    ("x-running-base", {"src": "run"}, {}, 0),
    ("x-single-disk-full", {"layout": "single"}, {}, model.step_index("disks")),
    ("x-running-summary", {"src": "run"}, {}, model.step_index("summary")),
]


def save_png(widget, path):
    w, h = widget.get_width(), widget.get_height()
    snap = Gtk.Snapshot()
    # the window draws the page background, not its content: paint it first
    ok, bg = widget.get_root().get_style_context().lookup_color("window_bg_color")
    if not ok:
        bg = Gdk.RGBA()
        bg.parse("#fafafb")
    snap.append_color(bg, Graphene.Rect().init(0, 0, w, h))
    Gtk.WidgetPaintable.new(widget).snapshot(snap, w, h)
    node = snap.to_node()
    if node is None:
        raise RuntimeError("nothing rendered for %s" % path)
    r = Gsk.CairoRenderer()
    r.realize_for_display(widget.get_display())
    try:
        tex = r.render_texture(node, Graphene.Rect().init(0, 0, w, h))
        tex.save_to_png(path)
    finally:
        r.unrealize()


def run(win, outdir):
    os.makedirs(outdir, exist_ok=True)
    scenes = list(SCENES)
    dark = os.environ.get("AMIX_SHOT_DARK") == "1"
    if dark:
        from gi.repository import Adw
        Adw.StyleManager.get_default().set_color_scheme(Adw.ColorScheme.FORCE_DARK)

    def setup():
        if not scenes:
            win.get_application().quit()
            return False
        name, sel, on, step = scenes[0]
        from . import model as m
        win.cfg = m.Config(sel=sel, on=on)
        win.show_step(step)
        GLib.timeout_add(400, grab)
        return False

    def grab():
        name = scenes.pop(0)[0]
        path = os.path.join(outdir, name + (".dark" if dark else "") + ".png")
        save_png(win.get_content(), path)
        print(path)
        GLib.idle_add(setup)
        return False

    GLib.timeout_add(800, setup)
