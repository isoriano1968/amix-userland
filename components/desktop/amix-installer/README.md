<p align="center"><img src="../../../doc/assets/amix-logo.png" alt="AMIX" width="160"></p>

# AMIX Installer

A GNOME desktop app (GTK 4, libadwaita) that makes an AMIX disk for one
machine from your own AMIX 2.1c core system: a core HDF, a tape image with
its root floppy, or a running AMIX system that it changes in place. It never
ships Commodore's files; it adds the kernel, drivers, desktop and tools from
amix-userland and amix-gate that fit the machine.

It runs on the host, not on AMIX: OpenIndiana first, Linux too.

## Install

- OpenIndiana and Solaris: the IPS package `desktop/amix-installer`
  (publisher `amix`): `pkg install desktop/amix-installer`
- Debian, Ubuntu, Mint: `make deb` makes
  `build/amix-installer_<version>-1_all.deb`; install it with
  `sudo apt install ./amix-installer_<version>-1_all.deb`, which also pulls
  in GTK 4, libadwaita and PyGObject

The AMIX packages the network install serves come as a package of their
own, `desktop/amix-installer/repository` (an IPS repository of every
amix-userland package, under `/usr/share/amix-installer/repo`);
`desktop/amix-installer` requires it, so a first run needs no source tree.
The installer takes the first usable repository of: `$AMIX_REPO`, a "make
repo" in the source tree (`build/repo`), your own
(`~/.local/share/amix-installer/repo`), the package's. "Choose…" on the
Base system page takes any other folder; a folder that is not an AMIX
package repository, or one older than the installer (without the foundation
packages), is refused with the reason.

Both put the same files under `/usr`. The driver kits, pvmk and the m68k
cross assembler and linker are their own packages; without them the
installer still runs, and its plan names what is missing.

**Version 0.3 prepares the disks**: the core copy, the pvmk boot disk, an
empty data disk and an FS-UAE configuration (an A3000 with an A2065), as
HDF files, for the Z3660 SD card, or as ZuluSCSI/BlueSCSI images named by
SCSI ID. AMIX itself then installs the kernel and packages over the A2065,
in FS-UAE or on an Amiga that has one (version 0.4); OpenIndiana or Linux never writes
AMIX's file system. All eight steps work (Base system, Machine,
Disks, Kernel, Drivers, Desktop, Development, Summary), options a machine
cannot use are greyed out, and the choices can be saved and loaded as a
profile. A chosen core HDF is read, read-only: partitions, root file
system use, AMIX packages, kernels, what is installed already, a pvmk boot
partition and the package client; the Disks page then shows its real sizes. Disk writing and package installs come next; see
[the plan](../../../doc/plans/AMIX-INSTALLER-APP-PLAN.md).

**Developing with VS Code on OpenIndiana or Linux** (0.6.5): the
Development step has a **VS Code agent** switch (developer/vscode-agent)
and a button, **Only the VS Code agent**, that makes a console system with
just the agent: no desktop, applications or compilers on the Amiga, because
they stay on OpenIndiana or Linux. For an AMIX that is already running
(0.6.7), **Just the Agent** sets AMIX's clock (sysutils/setclk) and installs
or updates only the agent, which brings sysutils/terminfo
(`TERM=xterm-256color`); **Agent and Motif** adds desktop/motif and
x11/x11r6.3 for Motif and CDE programs. The network install writes
`/etc/vscode-agent.conf` (root only):
- the agent dials the OpenIndiana or Linux computer on port 7777;
- it runs programs as the first login user, or as a new user `developer`;
- it signs in with a new key.

That key is also left in `vscode-agent.key` (mode 600) in
`~/.local/share/amix-installer` and beside the foundation report. In VS
Code, run **AMIX: Set Agent Key** and choose "Use the AMIX Installer's
key": it reads the first one, so it is always the newest install's.

**Serve Packages** (0.7): the window's second tab makes this computer the
package server of your AMIX machines. Start serves the AMIX package
repository (the packaged one, or a folder you choose) read-only over HTTP on
port 8069 at `/repo/`, shows the line to type on AMIX for FS-UAE here
(`pkg set-publisher -O http://10.0.2.2:8069/repo amix`) and for an Amiga on
the network, and lists what the Amigas fetch. It answers the AMIX Software
Center's **Find on Network** (UDP port 8070), unless that is switched off;
**Only this computer** listens on 127.0.0.1 alone, for FS-UAE. Without a
window: `amix-installer --serve [--repo DIR] [--local] [--port N]`, until
Ctrl-C. A network install serves the packages itself on the same port, so
the two do not run at once.

**Build** (0.8): the third tab builds AMIX packages on this computer from
amix-userland. The source is a checkout here, or a git URL (`#branch` or
`#commit` after it; the default is the project's GitHub repository, cloned
into `~/.cache/amix-installer`). **Look** lists the components that are new,
newer, or changed since the repository's version (the revision is the
component's commit count, `.1` for edits not committed). **Build** builds the
chosen ones with the AMIX cross compiler installed here
(developer/amix/gcc-14 in `/opt/m68k-amix/bin`, or `AMIX_TOOLS`) against a
sysroot made from the installed base sysroot (developer/amix/sysroot,
`/opt/m68k-amix-sysroot`, or `AMIX_SYSROOT_BASE`) and the packages the
component requires, unpacked from the repository. Each passes the same gates
as every published package (the source's `tools/amix-p5m` and
`tools/amix-publish`: shared libraries, used files, the CPU scan, a path two
packages deliver, a new revision for new content) and goes into your own
repository, `~/.local/share/amix-installer/repo`: a copy of the bundled one
at first, the bundled one's newer packages merged in later. Serve Packages
serves that repository from then on, so `pkg update` on AMIX gets what you
built. Without a window: `amix-installer --build-list [--from SOURCE]` and
`amix-installer --build COMPONENT... [--from SOURCE]`. A component that
fetches its upstream source needs the network, and some need host tools of
their own (their README says which). The Build tab builds userland with gcc 14 only; a component
whose Makefile also uses gcc 2.7.2.3 (x11/x11r6.3) needs developer/amix/gcc-27
(`/opt/amix-cross/bin` or `GCC27_TOOLS`). Kernels and drivers are never built
here: they need gcc 2.7.2.3 and come with the install's kernel step.

## Needs

Python 3.9 or later, PyGObject, GTK 4.12 or later, libadwaita 1.4 or later,
and pycairo to make the icons.

- OpenIndiana: `pkg install library/desktop/gtk4 library/desktop/libadwaita library/python/pygobject-39`
- Debian: `apt install python3-gi python3-gi-cairo gir1.2-gtk-4.0 gir1.2-adw-1`

## Build, test, run

    make build          # icons from the AMIX logo
    make test           # model, profiles, safety checks
    make deb            # the Debian package, under build/
    make repo           # the AMIX package repository, build/repo
    make repo-archive   # it as build/amix-installer-repository-<date>.tar.gz,
                        # the source of desktop/amix-installer/repository
    make run            # from the source tree
    make screenshots    # every step, light and dark, under Xvfb, to build/shots
                        # (AMIX_SHOT_SOURCE=core.hdf: also with that image read)
    ./amix-installer --source core.hdf   # start with a core HDF read
    ./amix-installer --source core.hdf --out DIR --plan    # print the plan
    ./amix-installer --source core.hdf --out DIR --kickstart A3000.rom --apply
    ./amix-installer --running fs-uae --vscode-only --net
                        # into the AMIX running in FS-UAE here (it reaches this
                        # computer as 10.0.2.2), only the VS Code agent; prints
                        # what to type at the AMIX console and the agent key
                        # file (~/.local/share/amix-installer/vscode-agent.key)
    ./amix-installer --running fs-uae --agent-only --net
                        # the VS Code agent alone, for a quick test: no
                        # foundation, no other package; tests/e2e/agent-only-fsuae.sh
                        # runs this from end to end in FS-UAE, headless
    ./amix-installer --serve             # serve the package repository to AMIX
                        # machines (port 8069, Find on Network on UDP 8070)
                        # until Ctrl-C; --local: this computer only (FS-UAE)
    ./amix-installer ... --apply --backup      # keep files of the same name as .bak
    ./amix-installer ... --apply --overwrite   # replace them, also ones not ours

Files of the same name in the output folder: the choice "Files that exist
already" (Base system page, saved in profiles), shown again on the Summary.
Overwrite replaces them; a file the installer did not write (not in its
amix-installer.json or SHA256SUMS) is named and asked about first. Keep a
backup renames each to NAME.YYYYMMDD-HHMMSS.bak. Either way the new files are
written as .part and checked, and only when the whole run has succeeded are
they renamed into place: a failed run leaves every old file as it was. The
source HDF is never written.

Your Kickstart ROM is only referred to by its path in the FS-UAE
configuration, never copied.

pvmk comes from the package `developer/amix/pvmk` (`/usr/share/pvmk`:
bootblock.bin, pvmk.bin, conf/; `/usr/lib/pvmk`: mkadf, mkhdf), or from a
pvmk build in `$AMIX_PVMK_DIR` or `~/Sources/usr-amix/pvmk/build`
(bootblock.bin, pvmk.bin, host/mkadf, host/mkhdf).
    make install DESTDIR=/tmp/root HOST_PREFIX=/usr

## Layout

    amix-installer          the launcher
    amix_installer/
      model.py              steps, options and what each one needs, as data
      profile.py            profiles (JSON, choices only: no paths or hosts)
      safety.py             checks before anything is written
      hdf.py                reads an HDF (RDB, UFS, packages), read-only
      plan.py               the plan, and running it (.part, read back, rename)
      rdb.py                the empty data disk, RDB copied from the core disk
      cli.py                --plan, --apply and --serve without a window
      serve.py              Serve Packages: the repository over HTTP, Find on Network
      servepage.py          the Serve Packages tab
      app.py                the window
      shot.py               headless screenshots
    data/                   stylesheet, desktop entry
    tools/mkicons.py        org.amix.AMIX and org.amix.Installer icons
    tests/                  unit tests

The options are data in `model.py`: an option's `needs` lists the machines,
cards, layouts or other choices it works with, and the window greys out the
rest. Adding a CPU card or a driver means adding one line there.
