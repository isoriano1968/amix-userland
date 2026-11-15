# AMIX Installer: a GNOME desktop app

Plan of 2026-11-15. Version 0.3 (UI, model, profiles, reading the source
HDF, the plan, writing the core copy and the pvmk boot disk) is in
`components/desktop/amix-installer`.

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
      plan.py     Config + source -> ordered actions; runs them safely
      cli.py      --plan / --apply without a window
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
log, and `--plan` prints it. Package installs go through the IPS client
on AMIX itself (see "One way to install"), so the conflict check, `.new` +
sha256 + rename and the journal are not written twice.

**Profiles.** JSON with a format name and version, the radio choices and
the switches, by id; never paths, hosts or device names (a profile is for
the next machine). Loading ignores unknown ids with a message and then
applies the model's rules, so an old profile still loads after options
change.

**Running systems.** In place mode reads the system first (uname, `pkg list`,
SVR4 `/var/sadm/install/contents`, free space, kernel), preselects what is
installed and offers only differences; switching something off is a removal
and is listed as such in the Summary. It needs the machine's network: an
A2065 on a stock system (the Network choice on the Machine page; "Running
AMIX system" is greyed out without one). On the A3000 the installer's own
tests go through `tools/amix-box` like every other box access.

## Boot: the pvmk partition

Every disk the installer makes gets a pvmk boot partition, so there is a
boot loader: the boot menu, the serial console, and the kernel loaded from
`UNIX_Root:/stand/unix` (the Z3660 kernel needs pvmk's `z3reloc`; the stock
and 040/060 kernels can also boot from Commodore's `UNIX_Boot`, which is
kept). The Kernel step offers it as the default ("pvmk boot partition");
"AMIX boot partition only" is greyed out for the Z3660 kernel.

- Always its own disk image, SCSI ID 5: `pvmk.hdf` (Z3660 SD card, FS-UAE)
  or `HD50_512.hda` (ZuluSCSI/BlueSCSI), made as `mkhdf -p
  PVMK,image=pvmk.adf,pri=4,bootblocks=2,pvbb` does it (boot block pointed
  at the partition's absolute block, medium hard disk, checksum). Never the
  raw .adf (red screen, AmigaOS boots).
- The same image boots FS-UAE and the real machine: without a Z3660,
  `z3reloc` reports "no Z3660 board" and changes nothing.
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
running system when the bootstrap reports back. The Base system step has a "Package client"
switch, on by default and marked "installed" when found; if it is
switched off on a destination without it, Apply asks whether to install it
first, because nothing else can be added without it. It goes on first, from
its bootstrap SVR4 datastream (`pkgadd`, so even it is recorded); then it
fetches and installs the packages.

## Disk writing stays safe

1. **The source is never written.** A source HDF or tape image is opened
   read-only, and its path, device and inode are recorded. Every target is
   compared with it (`os.path.samefile`, and the same st_dev/st_ino); the
   core is copied to a new file first and only the copy is changed.
2. **New files, then rename.** HDFs are written as `<name>.hdf.part` and
   renamed when complete and checked; nothing existing is overwritten
   without an explicit confirmation that names the file and its size.
3. **Never a raw disk.** The installer writes files only: HDFs and card
   images in a folder (the SD card's own folder at most). It never opens a
   block device for writing, so it needs no root and cannot hit the host's
   disks. A real SCSI disk gets an image with the user's own tool.
4. **Space.** Before anything is copied: room on the host for the copies;
   on the AMIX root, at least 200 MB free after the selection (the same rule
   the box has); a selection that does not fit is shown on the Disks page and
   blocks Apply.
5. **Verify.** Written images are read back and hashed; the Summary of the
   result lists the files with sha256.
6. **Z3660 SD card.** HDFs on the card must not be fragmented (firmware beta
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
- **0.3 (done, part):** the plan (`plan.py`): every step in order with what
  it writes, and the steps this version cannot do yet with the reason.
  The Summary page shows it; `--plan` prints it. Running it (`--apply`, or
  the window's progress page with log and Cancel) does the parts that need
  no UFS writing: the core copy (sha256 of source and read-back compared),
  the pvmk boot disk (`pvmk.hdf` from pvmk's bootblock.bin/pvmk.bin with
  our `pvmk.conf`: AMIX entry, keymap, z3reloc; hole check; read back as a
  PVMK partition), SHA256SUMS and a JSON record. Safety as below: `.part`,
  read-back, rename, never overwrite, a journal; a stopped run removes all
  it wrote. Targets: HDF files and a folder on the Z3660 SD card (with a
  fragment check through `filefrag`, which illumos lacks).
  Then (same day): the Network choice (A2065, Z3660 Ethernet, none), the
  ZuluSCSI/BlueSCSI target (images `HD<ID>0_512.hda`), the empty data disk
  (RDB copied from the core disk, one UNIX_Data partition, sparse; AMIX
  makes the file system), the FS-UAE configuration and a README with the
  SCSI IDs. The 4 GB data disk is gone until AMIX is tested above 2 GB.
- **0.4 (built, not yet run on AMIX):** the network install (below), into
  FS-UAE and into an Amiga with an A2065 alike, on the IPS client
  (amix-pkg 0.2). `netinstall.py`: TFTP and HTTP servers, the bootstrap
  datastream (`svr4.py`, checked with OI's pkginfo, pkgtrans and pkgchk;
  AMIX's cpio knows 070701), the `go` and step scripts (Bourne shell,
  `sh -n` checked); the network install page. `tools/amix-publish`
  publishes on Linux (repositories pass OI's pkgrepo verify and pkg);
  `make repo` publishes every component with a built stage (71 on
  2026-11-15). FS-UAE check and the "AMIX Installer" configuration.
  Open: the first run in FS-UAE; 11 components whose manifests no longer
  match their stage (ncurses among them) are left out of the repository;
  kernels and drivers are not packaged yet (amix-gate).
- Tape image + root floppy source last: the stock installation from the
  user's tape in FS-UAE (the "AMIGA3000 UNIX" configuration already has the
  tape as a SCSI tape), then the same network install.

## One way to install: over the A2065 (decided 2026-11-15)

AMIX writes its own file system; the host never writes UFS. A stock AMIX
2.1c has no ssh: its network is telnet, ftp and rsh over an A2065, the
only network card the stock kernel drives (the Z3660 Ethernet needs our
kernel). So there is one install path, a running AMIX with an A2065:

- **A real Amiga with an A2065** and enough free disk space: installed in
  place.
- **Everyone else** (Z3660, ZuluSCSI, BlueSCSI: machines whose disks are
  files on an SD card, and that cannot make HDFs themselves): the
  installer writes the core copy, `pvmk.hdf` and an empty data disk plus an
  FS-UAE configuration (A3000, A2065, the disks at their SCSI IDs: core 6,
  where the stock kernel roots; pvmk 5; data 1). FS-UAE boots it, the same
  network install fills it, and the user copies the files to the card.

How it starts, without a password on the network: the user logs in as
root at the Amiga's console (or FS-UAE's window) and types the lines the
installer shows: `tftp HOST 1069`, `binary`, `get go`, `quit`, `sh go`
(in FS-UAE the PC is 10.0.2.2). TFTP, not FTP as first planned: FS-UAE's
slirp NAT passes UDP replies that come from the listening port, but an
active FTP data connection only on port 21, which a user cannot open; and
TFTP to the PC has worked from AMIX before. The installer's TFTP server
(read-only, one socket, only while the install runs) gives `go`; `go`
fetches the bootstrap datastream the same way and installs it with stock
`pkgadd -n -a admin` into /opt/amixboot (pkg, curl and their libraries,
taken from the repository), then fetches the step script over HTTP with
that curl. The steps install the real package client through IPS
(sysutils/amix-pkg), then fetch the packages over HTTP from the
installer's repository: `mkfs -F ufs` for
the data disk, the moves of /usr/local, /usr/gnu and /opt with `cpio -p`
and a check before the links replace them, the packages, and the chosen
kernel last (the stock kernel stays as a fallback boot entry, so FS-UAE,
an 030, keeps booting until the end). Each step reports to the
installer (`curl -T - http://HOST:8069/report/msg`), which shows it on its
page; at the end `pkgrm` removes the bootstrap again, so nothing on the
system belongs to two package systems.

## Packaging

- amix-userland: `components/desktop/amix-installer`, host-side like
  `developer/vscode` (manifest without AMIX files; host needs in comments),
  MIT licence, `make test`, `make stage` (host prefix), `make screenshots`.
- OpenIndiana: `packages/desktop/amix-installer` in the local oi-local
  repository, requiring gtk4, libadwaita, pygobject; Debian package later.
- Icons: AMIX logo on a GNOME-style plate (`org.amix.AMIX`), the installer
  with a blue wrench badge (`org.amix.Installer`), generated from
  `doc/assets/amix-logo.png` at build time.
