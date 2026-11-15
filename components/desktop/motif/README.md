# Motif for AMIX

Motif 2.3.8: `libXm.so.4` and `libMrm.so.4`, their headers and `.pc` files
(`xm`, `mrm`), and `mwm` with `system.mwmrc` and its app-defaults, in
`/usr/motif`. Needs `x11/x11r6.3` (see `motif.manifest`, which lists every
file).

    gmake AMIX_SYSROOT=/path/to/sysroot stage

builds everything from the pinned `motif-2.3.8.tar.gz` and `patches/` against
a clean sysroot that holds X11R6.3 (on OpenIndiana: `developer/amix/sysroot`
and `developer/amix/x11r6.3`; elsewhere `amix-requires` installs x11r6.3 into
a local sysroot), and writes `build/motif-amix.tar`.

- `scripts/prep.sh` unpacks and patches Motif and builds the tools Motif runs
  while it builds (makestrs, wml) for the build host (host cc, flex and the
  host's X11 headers).
- `scripts/build-shared.sh` cross-builds Motif with gcc 14 (`AMIX_CPU`,
  default `-m68060`, `--sysroot` = `AMIX_SYSROOT`), makes the shared
  libraries from the `-fPIC` archives (libtool does not know this target),
  writes the `.pc` files and links `mwm`. configure cannot run test programs
  for AMIX, so `files/motif-amix.cache` holds its answers.

The result equals the Motif in use on AMIX (2026-11-15): `libMrm.so.4` and
`mwm` byte for byte, `libXm.so.4` except the source path `assert()` records
(now `motif-2.3.8/...` for every build host), `.pc` files the same. `Xm.h`
now says `PRINTING_SUPPORTED` is off, which matches the library (no Xp
functions in either build); the old header came from a different configure
run.
