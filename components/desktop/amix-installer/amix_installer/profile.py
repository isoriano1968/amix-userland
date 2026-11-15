"""Profiles: a selection saved as JSON, for the next machine.

Only choices are saved, by id: never paths, hosts or devices.
Unknown ids are skipped with a message; the model's rules apply after loading.
"""
import json
import os

from . import model

FORMAT = "amix-installer-profile"
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
        if oid not in switch_ids:
            msgs.append("unknown option %s" % oid)
            continue
        on[oid] = bool(val)
    cfg = model.Config()
    cfg.sel.update(sel)
    cfg.on.update(on)
    for gid in cfg.normalize():
        if gid in sel:
            msgs.append("%s: %s is not available here, now %s" % (gid, sel[gid], cfg.sel[gid]))
    return cfg, msgs


def load(path):
    with open(path) as f:
        return load_data(json.load(f))
