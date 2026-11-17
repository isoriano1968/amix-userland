"""Profiles: a selection saved as JSON, for the next machine.

Only choices are saved, by id: never paths, hosts or devices.
Unknown ids are skipped with a message; the model's rules apply after loading.
"""
import json
import os

from . import model

FORMAT = "amix-installer-profile"
# the further cards of 0.5 are the hardware switches of the Machine page
OLD_SWITCH = {"more_hydra": "hw_hydra", "more_xsurf": "hw_xsurf",
              "more_zz9000eth": "hw_zz9000eth", "more_picasso2": "hw_picasso2",
              "more_zz9000": "hw_zz9000", "more_va2000": "hw_va2000"}
VERSION = 1


def dump(cfg):
    radio = {}
    switches = {}
    for _, g in model.all_groups():
        if g.radio:
            radio[g.id] = cfg.sel[g.id]
        else:
            for o in g.options:
                switches[o.id] = bool(cfg.on.get(o.id))
    return {"format": FORMAT, "version": VERSION, "choose": radio, "switch": switches}


def save(cfg, path):
    tmp = path + ".part"
    with open(tmp, "w") as f:
        json.dump(dump(cfg), f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, path)


def load_data(data):
    """Returns (Config, messages)."""
    msgs = []
    if not isinstance(data, dict) or data.get("format") != FORMAT:
        raise ValueError("not an AMIX Installer profile")
    if data.get("version", 0) > VERSION:
        msgs.append("profile version %s is newer than this installer" % data.get("version"))
    sel = {}
    on = {}
    for gid, oid in (data.get("choose") or {}).items():
        try:
            g = model.find_group(gid)
        except KeyError:
            msgs.append("unknown group %s" % gid)
            continue
        if not g.radio or g.option(oid) is None:
            msgs.append("unknown choice %s/%s" % (gid, oid))
            continue
        sel[gid] = oid
    switch_ids = set(o.id for _, g in model.all_groups() if not g.radio for o in g.options)
    for oid, val in (data.get("switch") or {}).items():
        if oid == "rtg":
            continue        # before 0.5 a switch; now the "gfx" choice
        oid = OLD_SWITCH.get(oid, oid)
        if oid not in switch_ids:
            msgs.append("unknown option %s" % oid)
            continue
        on[oid] = bool(val) or on.get(oid, False)
    # a profile from before the Machine page listed the hardware: its Amiga's
    # own, and the cards its choices use, ticked (model.implied_on); the
    # switches it names win
    cfg = model.Config(sel=sel, on=on)
    for gid in sorted(sel):
        if cfg.sel.get(gid) != sel[gid]:
            msgs.append("%s: %s is not available here, now %s" % (gid, sel[gid], cfg.sel[gid]))
    return cfg, msgs


def load(path):
    with open(path) as f:
        return load_data(json.load(f))
