"""Command line: the window by default; --plan, --apply and --net without GTK."""
import argparse
import os
import sys

from . import __version__


def main(argv=None):
    ap = argparse.ArgumentParser(prog="amix-installer")
    ap.add_argument("--profile", help="load this profile at start")
    ap.add_argument("--source", metavar="HDF", help="read this core HDF at start (read-only)")
    ap.add_argument("--out", metavar="DIR", help="folder for the new disks")
    ap.add_argument("--kickstart", metavar="ROM",
                    help="your A3000 Kickstart ROM, for the FS-UAE configuration")
    ap.add_argument("--patch-disk", metavar="ADF",
                    help="your AmigaUNIX 2.1 patch disk image, applied when the core lacks it")
    ap.add_argument("--repo", metavar="DIR", help="the AMIX package repository (else AMIX_REPO)")
    ap.add_argument("--no-fs-uae-list", action="store_true",
                    help="do not add \"AMIX Installer\" to FS-UAE's configurations")
    ap.add_argument("--plan", action="store_true", help="print the plan and exit (no window)")
    ap.add_argument("--apply", action="store_true",
                    help="write what this version can (no window); needs --source and --out")
    ap.add_argument("--net", action="store_true",
                    help="serve the network install for the disks --apply wrote (no window; "
                    "needs --source and --out), or for --running; runs until AMIX reports "
                    "DONE or Ctrl-C")
    ap.add_argument("--running", choices=("fs-uae", "amiga"),
                    help="install into an AMIX that runs now, in FS-UAE on this computer or "
                    "on an Amiga on the LAN (with --net: no disks are written)")
    ap.add_argument("--vscode-only", action="store_true",
                    help="only the VS Code agent: a console system VS Code develops for")
    ap.add_argument("--agent-only", action="store_true",
                    help="with --running: install or update the VS Code agent and nothing "
                    "else (no base set, no package client)")
    ap.add_argument("--screenshot", metavar="DIR",
                    help="render every step to DIR/*.png and quit (tests)")
    ex = ap.add_mutually_exclusive_group()
    ex.add_argument("--overwrite", action="store_true",
                    help="replace files of the same name in --out, also ones the installer did "
                         "not write (never the source HDF)")
    ex.add_argument("--backup", action="store_true",
                    help="keep files of the same name in --out as <name>.<date>-<time>.bak")
    ap.add_argument("--version", action="version", version="%(prog)s " + __version__)
    args = ap.parse_args(argv)
    if args.plan or args.apply or args.net:
        return run_plan(args)
    from .app import main as gui
    return gui(args)


def run_plan(args):
    from . import hdf, model, plan, profile, safety
    cfg = model.Config()
    if args.profile:
        try:
            cfg, msgs = profile.load(args.profile)
        except (OSError, ValueError) as e:
            print("amix-installer: %s: not a profile (%s)" % (args.profile, e), file=sys.stderr)
            return 1
        for m in msgs:
            print("profile: %s" % m, file=sys.stderr)
    paths = {}
    if args.running:
        cfg.sel["src"] = "run"
        paths["running"] = args.running
        cfg.normalize()
    if args.vscode_only:
        model.vscode_only(cfg)
    if args.agent_only:
        if not args.running:
            print("amix-installer: --agent-only needs --running fs-uae or --running amiga",
                  file=sys.stderr)
            return 1
        model.agent_only(cfg)
    if args.source:
        paths["core_hdf"] = os.path.abspath(args.source)
        try:
            cfg.source = hdf.inspect(paths["core_hdf"], probes=model.all_probes())
        except (OSError, hdf.NotAnImage, hdf.UfsError) as e:
            print("amix-installer: %s: %s" % (args.source, e), file=sys.stderr)
            return 1
        cfg.normalize()
    if args.out:
        paths["out_dir"] = os.path.abspath(args.out)
    if args.kickstart:
        paths["kickstart"] = os.path.abspath(args.kickstart)
    if args.patch_disk:
        paths["patch_disk"] = os.path.abspath(args.patch_disk)
    if args.repo:
        paths["repo"] = os.path.abspath(args.repo)
    if args.no_fs_uae_list:
        paths["fsuae_add"] = False
    # files that exist in --out: the profile's choice, or the flag's
    if getattr(args, "backup", False):
        cfg.sel["existing"] = "backup"
    if getattr(args, "overwrite", False):
        cfg.sel["existing"] = "overwrite"
        paths["overwrite_confirmed"] = True
    pl = plan.make_plan(cfg, paths, written=args.net)
    if args.net:
        return run_net(cfg, pl, paths)
    print(pl.text())
    if pl.existing:
        print("\nIn %s already (%s):" % (paths.get("out_dir"), "kept as .bak" if
                                         cfg.sel.get("existing") == "backup" else "replaced"))
        for n, mine in pl.existing:
            print("  %s%s" % (n, "" if mine else "  (not written by the installer)"))
    if pl.problems:
        return 1
    if not args.apply:
        return 0
    if not pl.complete:
        print("\nThis version writes the parts marked available (no '-'); the others follow.")

    def prog(frac, title, text):
        sys.stderr.write("\r%3d%%  %-40.40s %-30.30s" % (frac * 100, title, text))
        sys.stderr.flush()

    try:
        res = plan.execute(pl, prog, lambda t: print("\n" + t if t.startswith("==") else t))
    except (OSError, safety.Refused) as e:
        print("\namix-installer: stopped: %s (nothing half-written is left)" % e, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\namix-installer: cancelled (nothing half-written is left)", file=sys.stderr)
        return 130
    sys.stderr.write("\n")
    for n, h in sorted(res.items()):
        print("%s  %s" % (h, n))
    return 0


def run_net(cfg, pl, paths=None):
    """The network install page without a window: the disks are written, FS-UAE
    boots them, AMIX fetches the bootstrap and the packages from here."""
    import time
    from . import hdf, netinstall, plan
    if pl.problems:
        print("\n".join("! " + p for p in pl.problems), file=sys.stderr)
        return 1
    net = [a for a in pl.actions if isinstance(a, plan.NetInstall)]
    if not net or not net[0].repo:
        print("amix-installer: %s (or --repo DIR)" % plan.NO_REPO, file=sys.stderr)
        return 1
    why = netinstall.not_ready(net[0].repo)
    if why:
        print("amix-installer: %s" % why, file=sys.stderr)
        return 1
    real = cfg.in_place()
    try:
        data_disk = None if real else netinstall.fs_uae_data_disk(pl)
    except (OSError, hdf.NotAnImage) as e:
        print("amix-installer: the data disk: %s (run --apply first)" % e, file=sys.stderr)
        return 1
    # AMIX in FS-UAE (disks written here, or a running one with --running
    # fs-uae) reaches this computer as 10.0.2.2; an Amiga over the LAN
    emu = not real or (paths or {}).get("running") == "fs-uae"
    host = netinstall.FS_UAE_HOST if emu else netinstall.lan_address()
    try:
        s = netinstall.Session(cfg, net[0].repo, host=host, data_disk=data_disk, log=print,
                               kernel=None if real else netinstall.plan_kernel(pl),
                               patch=(paths or {}).get("patch_disk"),
                               report=netinstall.report_path(pl))
        s.start()
    except OSError as e:
        print("amix-installer: the servers cannot start: %s" % e, file=sys.stderr)
        return 1
    print("repository: %s" % net[0].repo)
    if data_disk:
        print("data disk: %s" % netinstall.data_disk_text(data_disk))
    print("\nAs root at the AMIX console, type:\n")
    for c in s.commands():
        print("    " + c)
    print("")
    if s.agent_key_file:
        print("VS Code agent key: %s (VS Code: AMIX: Set Agent Key)\n" % s.agent_key_file)
    sys.stdout.flush()
    try:
        while not (s.done or s.failed):
            time.sleep(0.5)
        if s.failed:
            time.sleep(5)       # AMIX sends the end of its log right after FAILED
    except KeyboardInterrupt:
        print("\namix-installer: stopped", file=sys.stderr)
        return 130
    finally:
        s.stop()
    return 1 if s.failed else 0
