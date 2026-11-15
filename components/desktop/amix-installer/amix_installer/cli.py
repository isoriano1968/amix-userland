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
    ap.add_argument("--no-fs-uae-list", action="store_true",
                    help="do not add \"AMIX Installer\" to FS-UAE's configurations")
    ap.add_argument("--plan", action="store_true", help="print the plan and exit (no window)")
    ap.add_argument("--apply", action="store_true",
                    help="write what this version can (no window); needs --source and --out")
    ap.add_argument("--net", action="store_true",
                    help="serve the network install for the disks --apply wrote (no window); "
                    "needs --source and --out, runs until AMIX reports DONE or Ctrl-C")
    ap.add_argument("--screenshot", metavar="DIR",
                    help="render every step to DIR/*.png and quit (tests)")
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
    if args.no_fs_uae_list:
        paths["fsuae_add"] = False
    pl = plan.make_plan(cfg, paths, written=args.net)
    if args.net:
        return run_net(cfg, pl)
    print(pl.text())
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


def run_net(cfg, pl):
    """The network install page without a window: the disks are written, FS-UAE
    boots them, AMIX fetches the bootstrap and the packages from here."""
    import time
    from . import hdf, netinstall, plan
    if pl.problems:
        print("\n".join("! " + p for p in pl.problems), file=sys.stderr)
        return 1
    net = [a for a in pl.actions if isinstance(a, plan.NetInstall)]
    if not net or not net[0].repo:
        print("amix-installer: no package repository (make repo, or AMIX_REPO)", file=sys.stderr)
        return 1
    real = cfg.in_place()
    try:
        data_disk = None if real else netinstall.fs_uae_data_disk(pl)
    except (OSError, hdf.NotAnImage) as e:
        print("amix-installer: the data disk: %s (run --apply first)" % e, file=sys.stderr)
        return 1
    host = netinstall.lan_address() if real else netinstall.FS_UAE_HOST
    try:
        s = netinstall.Session(cfg, net[0].repo, host=host, data_disk=data_disk, log=print)
        s.start()
    except OSError as e:
        print("amix-installer: the servers cannot start: %s" % e, file=sys.stderr)
        return 1
    print("repository: %s" % net[0].repo)
    if data_disk:
        print("data disk: %s, %d sectors" % data_disk)
    print("\nAs root at the AMIX console, type:\n")
    for c in s.commands():
        print("    " + c)
    print("")
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
