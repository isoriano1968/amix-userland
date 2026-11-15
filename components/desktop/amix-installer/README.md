<p align="center"><img src="../../../doc/assets/amix-logo.png" alt="AMIX" width="160"></p>

# AMIX Installer

A GNOME desktop app (GTK 4, libadwaita) that makes an AMIX disk for one
machine from your own AMIX 2.1c core system: a core HDF, a tape image with
its root floppy, or a running AMIX system that it changes in place. It never
ships Commodore's files; it adds the kernel, drivers, desktop and tools from
amix-userland and amix-gate that fit the machine.

It runs on the host, not on AMIX: OpenIndiana first, Linux too.

**Version 0.2 writes nothing.** All eight steps work (Base system, Machine,
Disks, Kernel, Drivers, Desktop, Development, Summary), options a machine
cannot use are greyed out, and the choices can be saved and loaded as a
profile. A chosen core HDF is read, read-only: partitions, root file
system use, AMIX packages, kernels, what is installed already, a pvmk boot
partition and the package client; the Disks page then shows its real sizes. Disk writing and package installs come next; see
[the plan](../../../doc/plans/AMIX-INSTALLER-APP-PLAN.md).

## Needs

Python 3.9 or later, PyGObject, GTK 4.12 or later, libadwaita 1.4 or later,
and pycairo to make the icons.

- OpenIndiana: `pkg install library/desktop/gtk4 library/desktop/libadwaita library/python/pygobject-39`
- Debian: `apt install python3-gi python3-gi-cairo gir1.2-gtk-4.0 gir1.2-adw-1`

## Build, test, run

    make build          # icons from the AMIX logo
    make test           # model, profiles, safety checks
    make run            # from the source tree
    make screenshots    # every step, light and dark, under Xvfb, to build/shots
                        # (AMIX_SHOT_SOURCE=core.hdf: also with that image read)
    ./amix-installer --source core.hdf   # start with a core HDF read
    make install DESTDIR=/tmp/root HOST_PREFIX=/usr

## Layout

    amix-installer          the launcher
    amix_installer/
      model.py              steps, options and what each one needs, as data
      profile.py            profiles (JSON, choices only: no paths or hosts)
      safety.py             checks before anything is written
      hdf.py                reads an HDF (RDB, UFS, packages), read-only
      app.py                the window
      shot.py               headless screenshots
    data/                   stylesheet, desktop entry
    tools/mkicons.py        org.amix.AMIX and org.amix.Installer icons
    tests/                  unit tests

The options are data in `model.py`: an option's `needs` lists the machines,
cards, layouts or other choices it works with, and the window greys out the
rest. Adding a CPU card or a driver means adding one line there.
