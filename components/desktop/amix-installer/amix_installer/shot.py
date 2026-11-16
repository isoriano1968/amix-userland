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
# with --source: what the image holds, real sizes, installed marks
SOURCE_SCENES = [
    ("s-base", {}, {}, 0),
    ("s-base-end", {}, {}, 0),
    ("s-disks", {}, {}, model.step_index("disks")),
    ("s-disks-single", {"layout": "single"}, {}, model.step_index("disks")),
    ("s-kernel", {}, {}, model.step_index("kernel")),
    ("s-desktop", {}, {}, model.step_index("desktop")),
    ("s-summary", {}, {}, model.step_index("summary")),
]
# with --source and AMIX_SHOT_OUT=DIR: the plan for HDF files, and a real run into DIR
RUN_SCENES = [
    ("r-summary", {"target": "file"}, {}, model.step_index("summary")),
    ("r-summary-end", {"target": "file"}, {}, model.step_index("summary")),
    ("r-done-run", {"target": "file"}, {}, model.step_index("summary")),
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
    # with a source image only the scenes that show it
    scenes = list(SOURCE_SCENES if getattr(win, "source_state", None) else SCENES)
    if os.environ.get("AMIX_SHOT_NET"):
        from . import plan
        if plan.default_repo():
            scenes = [("n-net", {"src": "run"}, {}, 0), ("n-fsuae-net", {"src": "run"}, {}, 0)]
    out = os.environ.get("AMIX_SHOT_OUT")
    if out and getattr(win, "source_state", None):
        win.paths["out_dir"] = out
        scenes += RUN_SCENES
    dark = os.environ.get("AMIX_SHOT_DARK") == "1"
    if dark:
        from gi.repository import Adw
        Adw.StyleManager.get_default().set_color_scheme(Adw.ColorScheme.FORCE_DARK)

    def setup():
        if not scenes:
            win.get_application().quit()
            return False
        st = getattr(win, "source_state", None)
        if st and st[0] == "reading":
            GLib.timeout_add(200, setup)        # wait for the source image
            return False
        name, sel, on, step = scenes[0]
        from . import model as m
        source = st[1] if st and st[0] == "done" else None
        win.cfg = m.Config(sel=sel, on=on, source=source)
        win.show_step(step)
        if name.endswith("-end"):
            after_paint(to_end)
        elif name.endswith("-run"):
            from . import plan
            win._run_plan(plan.make_plan(win.cfg, win.paths))
            GLib.timeout_add(500, wait_run)
        elif name.endswith("-net"):
            # the network install page for a running system (servers start, then stop)
            from . import plan
            win._net_page(plan.NetInstall("", plan.default_repo()), real=True,
                          emu=name.endswith("fsuae-net"))
            GLib.timeout_add(300, lambda: after_paint(net_grab) and False)
        else:
            after_paint(grab)
        return False

    def net_grab():
        grab()
        win._net_stop()
        return False

    def after_paint(then):
        """Call then() once the window has painted the new page (a fixed
        delay is not enough: a big page can take longer to lay out)."""
        clock = win.get_frame_clock()
        state = {"id": None, "done": False}

        def painted(*_a):
            if state["done"]:
                return
            state["done"] = True
            clock.disconnect(state["id"])
            GLib.timeout_add(150, then)
        state["id"] = clock.connect("after-paint", painted)
        win.queue_draw()

    def wait_run():
        if win.run_title.get_text() not in ("Done", "Stopped"):
            return True
        after_paint(grab)
        return False

    def to_end():
        adj = win.scroller.get_vadjustment()
        adj.set_value(adj.get_upper() - adj.get_page_size())
        after_paint(grab)
        return False

    def grab():
        name = scenes.pop(0)[0]
        path = os.path.join(outdir, name + (".dark" if dark else "") + ".png")
        save_png(win.get_content(), path)
        print(path)
        GLib.idle_add(setup)
        return False

    GLib.timeout_add(800, setup)
