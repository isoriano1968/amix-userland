# AMIX Installer: a GNOME desktop app

Plan of 2026-11-15. Version 0.2 (UI, model, profiles, reading the source
HDF) is in `components/desktop/amix-installer`; nothing writes disks yet.

## What it is

A desktop app on the host (OpenIndiana first, Linux too) that turns the
user's own AMIX 2.1c core system into a disk made to measure for one machine:
kernel, drivers, desktop and tools from amix-userland and amix-gate. It never
ships Commodore's files. It always starts from what the user has:

- a core HDF (`AMIGA_UNIX.hdf` with the core packages),
- an AMIX 2.1c tape image plus the root floppy (the core packages are
  installed first, as the stock installer would),
- a running AMIX system on the network: then it only adds, updates or removes
  packages in place, and copies nothing.

The design reference is the mockup approved on 2026-11-15 (eight steps: Base
system, Machine, Disks, Kernel, Drivers, Desktop, Development, Summary;
options a machine cannot use are greyed out with "Not for this machine or
layout").

## Language and toolkit

**Python 3 with PyGObject, GTK 4 and libadwaita.** Reasons:

- Both hosts have it packaged: OpenIndiana 2025 has gtk4 4.16,
  libadwaita 1.6, pygobject 3.54 on Python 3.9; Debian 13 has GTK 4.18,
  libadwaita 1.7. The code targets the lower end: Python 3.9 syntax,
  libadwaita 1.4 widgets (NavigationSplitView, ToolbarView, AlertDialog,
  breakpoints), GTK 4.12 API.
- The app is mostly UI and orchestration; the heavy work (UFS writing, package
  payloads) is done by tools that exist or will exist anyway (`tools/ufsget.py`
  style readers, the IPS client of IPS-INSTALLER-PLAN.md, `mkfs`/`newfs` on
  AMIX itself). C would add a build step on two platforms for no gain.
- GNOME's own installers and setup tools (gnome-initial-setup is C, but
  Boxes-style tools and many GNOME Circle apps) use the same widgets, so the
  look is the platform's, light and dark, without custom drawing.

If the backend later needs speed (checksumming 2 GB HDFs), that part can be
a small C helper called as a subprocess; the UI does not change.

## Architecture

    amix_installer/
      model.py    steps, groups, options as data; what each option needs;
                  sizes; the Config (selection) and its rules
      profile.py  save/load a selection as JSON
      safety.py   checks a source/target pair before anything is written
      hdf.py      reads an HDF read-only: RDB, UFS, packages, kernels
      app.py      the GTK/libadwaita UI; reads the model, writes nothing
      shot.py     renders every step to PNG (tests, docs), no visible window
      backend/    (later) plan -> actions; HDF, partitions, packages

**Model as data.** Every step, group and option is a record: id, title,
subtitle, badge, size in MB, and `needs`. `needs` is a list of alternatives
("any of these tags"), or a list of such lists that must all hold. Tags are
the chosen radio values (machine, CPU card, layout, source, kernel,
desktop). The UI never knows about machines: it asks the model whether an
option is available, why not, and what is chosen. When a choice makes a
selected radio option unavailable (A2500 after Z3660), the model moves that
group to its first available option, so the selection is always valid.
Switches keep their value but count as off while unavailable. The model
has unit tests and no GTK import, so the backend and a later command line
front end (`amix-installer --profile x.json --apply`) share it.

**Plan, then actions.** The backend (version 0.3) turns a Config into an
ordered, printable plan: copy core image, create data disk, install kernel
packages, set boot entry, install packages by name and version. The Summary
page shows that plan, the "Apply" button runs it with a progress page and a
log, and `--dry-run` prints it. Package installs go through the IPS client
(`pkg -R <mounted image>` on a host-mounted copy, or `pkg` over ssh on a
running system), so the conflict check, `.new` + sha256 + rename and the
journal are not written twice.

**Profiles.** JSON with a format name and version, the radio choices and
the switches, by id; never paths, hosts or device names (a profile is for
the next machine). Loading ignores unknown ids with a message and then
applies the model's rules, so an old profile still loads after options
change.

**Running systems.** In place mode reads the system first (uname, `pkg list`,
SVR4 `/var/sadm/install/contents`, free space, kernel), preselects what is
installed and offers only differences; switching something off is a removal
and is listed as such in the Summary. On the A3000 this goes through
`tools/amix-box` like every other box access, and refuses while a player
runs.

## Boot: the pvmk partition

Every disk the installer makes gets a pvmk boot partition, so there is a
boot loader: the boot menu, the serial console, and the kernel loaded from
`UNIX_Root:/stand/unix` (the Z3660 kernel needs pvmk's `z3reloc`; the stock
and 040/060 kernels can also boot from Commodore's `UNIX_Boot`, which is
kept). The Kernel step offers it as the default ("pvmk boot partition");
"AMIX boot partition only" is greyed out for the Z3660 kernel.

- On a SCSI/BlueSCSI disk: a PVMK partition (DOS\1, about 900 KB) before
  UNIX_Root, made as `mkhdf -p PVMK,image=pvmk.adf,pvbb` does it (boot block
  pointed at the partition's absolute block, medium hard disk, checksum).
  Never the raw .adf (red screen, AmigaOS boots).
- On the Z3660 SD card: its own `pvmk.hdf` (SCSI unit 5 in z3660cfg.txt),
  as on the A3000 now.
- `pvmk.conf` on the volume gets `entry=AMIX,UNIX_Root:/stand/unix,native`
  for the chosen kernel.
- The pvmk image must pass `make check` (the 320 KB hole) before it is
  written, and the written volume is read back and compared.
- The reader recognises a PVMK partition on a source disk (DOS\1 with the
  PVBB magic at 0x3c0 of the boot block) and keeps it.

## The package client on the destination

Every package install goes through the IPS client (`pkg`, `/var/pkg`, see
IPS-INSTALLER-PLAN.md). The installer checks the destination for it
(`/var/pkg`, `/usr/bin/pkg`): on a source HDF when it reads it, on a
running system over ssh. The Base system step has a "Package client"
switch, on by default and marked "installed" when found; if it is
switched off on a destination without it, Apply asks whether to install it
first, because nothing else can be added without it. It goes on first, from
its bootstrap SVR4 datastream (`pkgadd`, so even it is recorded), then
uploads and installs the packages.

## Disk writing stays safe

1. **The source is never written.** A source HDF or tape image is opened
   read-only, and its path, device and inode are recorded. Every target is
   compared with it (`os.path.samefile`, and the same st_dev/st_ino); the
   core is copied to a new file first and only the copy is changed.
2. **New files, then rename.** HDFs are written as `<name>.hdf.part` and
   renamed when complete and checked; nothing existing is overwritten
   without an explicit confirmation that names the file and its size.
3. **Never the host's disks.** A device target (SD card, SCSI disk via a USB
   reader) is offered only if it is removable or not in use: the installer
   lists disks itself (Linux: `/sys/block`, `lsblk -J`; illumos: `diskinfo`,
   `rmformat`), and refuses any disk that holds a mounted filesystem, swap,
   a ZFS pool member (`zpool status`), an LVM/MD member, or the host's
   root/boot disk. The check runs again right before writing.
4. **Privileges only for the write.** The app runs as the user. Writing a
   device needs root: one small helper (pkexec on Linux, pfexec with a
   profile on illumos) takes the plan's device and image path, repeats
   check 3 itself and writes nothing else.
5. **Space.** Before anything is copied: room on the host for the copies;
   on the AMIX root, at least 200 MB free after the selection (the same rule
   the box has); a selection that does not fit is shown on the Disks page and
   blocks Apply.
6. **Verify.** Written images are read back and hashed; the Summary of the
   result lists the files with sha256.
7. **Z3660 SD card.** HDFs on the card must not be fragmented (firmware beta
   21 misdirects files with 32 or more fragments): files are preallocated
   in one piece and checked with `filefrag`/`fiemap` where available, or
   written to an empty card.

## Versions

- **0.1 (this one):** UI with all eight steps, model with the greyed-out
  rules and example sizes, profile save/load, icons and desktop entry,
  headless screenshots, unit tests. Source/target choosers record paths
  only. "Create disks" says that disk writing is not in this version.
- **0.2 (done):** read the source HDF, read-only, in pure Python
  (`hdf.py`, the same layout as pvmk's `svc/ufs.c`): RDB partitions, UFS
  superblock (size, used, free after minfree), SVR4 packages from
  `/var/sadm/pkg/*/pkginfo`, kernels in `/stand`, the `/unix` link, du of
  `/usr/local`, `/usr/gnu`, `/opt`, a PVMK partition, the package client,
  and what of the offered software is there already (probe paths). The
  Disks page uses the real sizes (what moves to the data disk included);
  installed options are marked and not counted again. Tested on the stock
  AMIGA_UNIX.hdf and the 2026-10-26 Z3660 backup (du of / equals the
  superblock's used blocks within a few fragments; sha256 unchanged).
  Package sizes from the built stages: with the IPS manifests (0.3).
- **0.3:** plan and HDF-file targets: copy core, data disk HDF, install into
  the copies with the IPS client's `-R`, boot entry, stock kernel kept.
  Tested with FS-UAE.
- **0.4:** device targets (SD card, SCSI via USB) with the disk checks and the
  privileged helper.
- **0.5:** running systems over ssh, after the IPS client is on the box.
- Tape image + root floppy source last: it needs the core package install
  done the way the stock installer does it, in an emulator.

## Packaging

- amix-userland: `components/desktop/amix-installer`, host-side like
  `developer/vscode` (manifest without AMIX files; host needs in comments),
  MIT licence, `make test`, `make stage` (host prefix), `make screenshots`.
- OpenIndiana: `packages/desktop/amix-installer` in the local oi-local
  repository, requiring gtk4, libadwaita, pygobject; Debian package later.
- Icons: AMIX logo on a GNOME-style plate (`org.amix.AMIX`), the installer
  with a blue wrench badge (`org.amix.Installer`), generated from
  `doc/assets/amix-logo.png` at build time.
