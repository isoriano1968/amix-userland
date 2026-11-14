# An IPS package client for AMIX

Plan of 2026-11-14. Nothing built yet.

## Why

Today `sysutils/amix-pkgtools`' `amix-install` unpacks `build/<name>-amix.tar`
into `/` with GNU tar. What that cannot do:

- no record of which package owns which file: no uninstall, no verify;
- no conflict check: a package silently overwrites a stock AMIX file, an
  apkg/SVR4 package's file, or another amix-userland package's file (only
  `etc/` is protected);
- files are replaced in place, not as `.new` + checksum + `mv`: an
  interrupted unpack leaves half-written libraries;
- versions and dependencies are not checked at install time (`amix-requires`
  checks only at build time, without versions);
- the stage on the P52 is the only source of truth.

## What it is

A small IPS client written in C for AMIX, `pkg`, speaking the IPS repository
format, so the same tooling (pkgsend, pkgrecv, pkgrepo, pkglint, pkg.depotd)
on OpenIndiana publishes and serves AMIX packages. Porting pkg(5) itself is
not the plan: it is Python with several C extensions, SAT solver, pycurl and
cryptography, and too slow on a 68060 at 80 MHz.

Libraries it needs are already components: zlib (gzip'd file payloads),
expat or a small JSON reader (catalog v1 is JSON), openssl (sha1/sha256,
https), curl (http/https to a depot).

### Subcommands (first version)

`pkg refresh`, `pkg list [-a|-u]`, `pkg info`, `pkg contents`, `pkg search`
(local catalog), `pkg install`, `pkg update`, `pkg uninstall`, `pkg verify
[-f]`, `pkg fix`, `pkg history`, `pkg set-publisher`/`publisher`,
`pkg change-facet`, `-n` (dry run) and `-v` on every changing command.

### Safety rules (the reason it exists)

1. **Ownership database** `/var/pkg`: per installed package its manifest;
   a path index of every file, link and directory and who owns it.
2. **Conflict check before anything is written**, against: the packages it
   installed, the SVR4 database `/var/sadm/install/contents` (stock AMIX packages and
   everything apkg installed), and files present on disk that nobody owns.
   Any of these: refuse, name the path and the owner. No `--force` that
   silently overwrites; an explicit, logged `--take-over PATH` at most.
3. **Every file as `.new`** next to its target, sha256 checked against the
   manifest, then `rename()` (the box rule for every install). Directories
   already there keep owner and mode.
4. **Transaction journal** in `/var/pkg/journal`: what will be renamed and
   removed, written before the first rename. An interrupted install is
   rolled forward or back on the next run (AMIX has no ZFS BEs, so the
   journal is the rollback).
5. **Configuration**: `preserve=true` files are never replaced; the
   package's version goes to `<file>.new` with a message (as `amix-install`
   does for `etc/` today, but per file and recorded).
6. **Versions everywhere**: FMRIs `pkg://amix/x11/x11r6.3@6.3.<rev>`;
   `depend type=require fmri=...@version` checked at install; `pkg list`
   shows exactly what is installed and from which publisher.
7. **Uninstall** removes only files the package owns and that still match
   its hash; changed files are kept as `<file>.pkgsave` and reported.
8. **Free space**: refuse when the target filesystem would drop below a
   limit (default 200 MB on `/`).
9. **Native only**: refuses to run unless it is on AMIX; cross hosts use the
   OI packages under `/opt/m68k-amix-sysroot` (see OpenIndiana
   `notes/amix-cross-dev-plan.md`). `-R <root>` exists for tests on a
   mounted image only.

### Facets instead of -dev packages

The box gets the IPS answer to runtime vs development: one package per
library, headers, `.so` links, `.pc` and link stubs tagged `facet.devel`.
A plain desktop installs with `facet.devel=false` and saves the disk space;
`pkg change-facet facet.devel=true` (or `meta/amix-dev`) adds them. Same
manifests, same facet on the OI cross packages: one build of a component
feeds both.

## Packages and repository

- amix-userland's `.manifest` files are converted to IPS `.p5m` (one
  generator, checked by pkglint), `variant.arch=m68k`, versions on every
  `require`. The stage stays as it is; `pkgsend publish` takes it.
- One publisher `amix`, one repository, served by `pkg.depotd` (on the P52
  or OI first; a public server later is the user's decision).
- Bootstrap: the client itself ships once as an SVR4 datastream for stock
  `pkgadd`, so even it is recorded; from then on it updates itself.

## Living next to apkg

apkg (Jussi, heirloom pkgtools, SVR4 database) stays usable. My client
reads the SVR4 database and never overwrites its files. The other direction
needs agreement with Jussi: apkg must not overwrite the client's files
either, e.g. the client registers its paths in `/var/sadm/install/contents` under one
pseudo package, or apkg reads `/var/pkg`. To be discussed before step 4.

## Order

1. Format and DB design note; manifest generator (`.manifest` -> `.p5m`),
   pkglint on OI. P52 only.
2. Client read side: refresh, list, info, contents, verify — tested against
   a repo of a few packages with a mounted image copy (`-R`), on the P52
   (qemu or the PC test rig), not on the box.
3. Write side: install/uninstall/update with conflict check, `.new` +
   sha256 + rename, journal; crash tests by killing it mid-install on the
   image copy.
4. I agree the SVR4 side with Jussi; bootstrap datastream.
5. On the A3000, one step at a time: install the client, then migrate the
   amix-userland packages already installed: compare every file against the manifests
   (`pkg verify` on a dry run) before the DB takes them over.
6. Retire `amix-install` tar installs.
7. GUI: amixsoftware gets an IPS backend next to apkg.
