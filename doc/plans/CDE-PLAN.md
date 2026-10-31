# CDE on AMIX (A3000 + Z3660 060): plan

Status: draft, 2026-10-24. Nothing downloaded or built yet.

## Goal

The original CDE look and feel on AMIX: dtwm, front panel, dtsession, dtterm,
dtfile, dtpad, the style manager. It runs on your X11R6.3 port with `Xzz9000`
on the Z3660 RTG.

Principle: an **era-matched stack**. Your X11R6.3 (1996/97) is the base, with
a CDE release that still has the old SVR4 code paths and the imake build. It
should not need UTF-8, Xft or PAM, which AMIX doesn't have.

## What is there already

| Piece | State |
|---|---|
| X11R6.3 libraries (X11, Xt, Xmu, Xext, ...) | built natively on the disk, `/usr/x11r6/xc/exports/lib` (shared, `.so.6.x`); used by `gears` |
| X server | `Xzz9000` from your tree + the amix-z3660rtg patches (blitter, Z3660 modes, keyboard hand-back), cross-built 2026-10-24 |
| Toolchains | gcc-cross-amix 2.7.2.3 (C, kernel-safe); gcc14-cross-amix (C99/C++, libstdc++, 060-tuned, **userland only**) |
| Kernel | 68060, 128 MB, Z3660 SCSI/RTG/net drivers, RTG console |

## Choices

**CDE version: 2.2.4a** (2018, last of the imake series, `cde-src-2.2.4a.tar.gz`, 58 MB).
- 2.3.0 removed the old reference platforms (UnixWare 2.02, SVR4.2, is AMIX's nearest
  relative), and 2.4 switched to autoconf.
- 2.5 needs UTF-8 locales, Xft/Xrender, lmdb, OpenSSL and PAM.
- Fallback: the 2012 initial import in the CDE git history (the original 2.1 code
  from The Open Group).
- To check first: that 2.2.4a still has the USL/UnixWare `#ifdef`s that an AMIX
  port would build on.

**Motif: 2.3.x (LGPL)**, built against your R6.3 Xt, without Xft, PNG or JPEG.
- Open Motif 2.1.30 would match the era better (imake).
- Its Open Group Public License allows free use only on open-source operating
  systems, so it's not usable on AMIX.

**Compiler: gcc14-cross-amix** for Motif and CDE.
- ToolTalk (libtt, which dtfile, dtmail and dtpad depend on) is C++.
- The 060 tuning helps a desktop.
- gcc 2.7.2.3 stays for kernel work.

## Open questions (they decide the effort)

1. **Shared libraries from gcc14-cross-amix.** The README doesn't say.
   - Without `.so` support, every dt program links Motif + DtSvc + Tt
     statically: about 30 programs of several MB each.
   - With it, `libXm`, `libDtSvc` and `libtt` are shared, as on the reference
     platforms. First test.
2. **Mixing ABIs.** Can a gcc14 C++ program link against your gcc-2.7.2.3-built
   R6.3 shared libraries (C ABI only, so probably yes)? Second test.
3. **ONC RPC.** ToolTalk and dtcm use RPC. AMIX's `libnsl` is SVR4 TI-RPC (the
   API CDE was written for). Check that `rpcbind` runs on the box.
4. **ksh.** CDE's scripts need ksh. AMIX has ksh88 (`/usr/bin/ksh`?). dtksh
   builds from the ksh93 in the CDE tree.
5. **Build-time tools.** CDE generates code with tools it builds itself
   (nsgmls for the help files, message catalogues with `gencat`, fonts with
   `bdftopcf`/`mkfontdir`). These have to be built for the host, as
   `mkg3states` was for the server.
6. **Getting files onto the box.**
   - The serial line moves about 1 MB per 13 minutes. An installed CDE is
     100–200 MB, which would take days.
   - So **networking comes first**: `zen0` (the Z3660 Ethernet driver is
     already in `unix.z3`; the Ethernet cable and `S99zen` setup are missing),
     then FTP or NFS.
   - Alternative: write files into a copy of the HDF on the PC (needs a UFS
     writer; ours is read-only).

## Phases

| Phase | Content | Done when |
|---|---|---|
| P0 | `zen0` up: cable, IP, `S99zen`; FTP between PC and A3000 | a 2 MB file arrives in seconds |
| P1 | gcc14-cross-amix: install, sysroot, `check-68060-safe.py`; C and C++ hello, X client against R6.3, shared-library test | answers to questions 1 and 2 |
| P2 | R6.3 as the desktop runtime: library path, R6.3 clients and fonts next to R5 | `xterm`/`twm` from R6.3 on `Xzz9000` |
| P3 | Motif 2.3.x: `libXm`, `mwm`, a demo | mwm session on the Z3660 HDMI |
| P4 | CDE core: DtSvc, DtWidget, Tt, dtwm, dtsession, dtterm, dtpad, front panel | a CDE session from `startx-z3` |
| P5 | the rest: dtfile, dtstyle, dthelp, dtcm, dtmail; dtlogin on the RTG | login screen to desktop |

## Downloads needed (each needs your OK)

- https://github.com/asokero/gcc14-cross-amix (git clone)
- `cde-src-2.2.4a.tar.gz`, 58 MB, from sourceforge.net/projects/cdesktopenv
- Motif 2.3.8 source (about 6 MB)

## Status 2026-10-24

Built (gcc14-cross-amix, static, stripped): dtsession, dtwm, dtterm, dtfile,
dtpad, dtstyle, dtmail, dtcm (+ rpc.cmsd), dtcalc, dticon, dtcreate, dtlogin
(dtgreet, dtchooser), dtprintinfo, dthelpview, dtaction, dtexec, dtspcd,
dtscreen, dtsearchpath, ttsession, rpc.ttdbserver, dtsr tools, ... (74 in
/usr/dt/bin).

Package: `sh cde-package.sh` -> build-cde/cde-amix.tar (166 MB; 145 MB of it
binaries, each ~5 MB because Motif/Xt/X11/libstdc++ are linked statically).
`cde-udb-stage.py` lays the tree out from CDE's own databases/*.udb (installCDE
needs ksh on the host and kills desktop processes there).  On the A3000:
`cd / && tar xf cde-amix.tar && sh /usr/dt/AMIX-INSTALL.sh` (root; does not
touch inetd.conf/services), then `startx /usr/dt/bin/Xsession`.

Not built / deferred:
- help volumes (doc/C -> *.sdl): need dtdocbook's toolchain on the host
  (nsgmls C++, instant, tcl, xlate_locale) and a ksh (`set -A`).
- dtksh (ksh93 build needs a ksh on the host), dtappbuilder + ttsnoop (need
  Motif's libUil), dtinfo (nsgmls C++ errors).
- ToolTalk types.xdr: compiled on the A3000 by AMIX-INSTALL.sh.
- Shared libraries: would cut ~120 MB; open question.

## Next: the Solaris 10 look and dtlogin at boot (asked 2026-10-25)

Goal: CDE on the A3000 looking like Solaris 10's CDE 1.6, with graphical login
(dtlogin) as the default at boot.

1. Fix ToolTalk in dtwm first (EXIT, actions and app launches all fail with
   TT_ERR_AUTHORIZATION/INTERNAL); dtlogin is pointless before a session works.
2. Look from open CDE: Solaris-style palette and fonts, front panel, dtlogin
   layout.  Sun-only artwork (SunLogo backdrop, Solaris dtlogin logo, Sun
   fonts) is NOT recreated; the user supplies it later from Solaris media.
   Slots: /usr/dt/appconfig/icons/C (backdrop), /etc/dt/config/Xresources
   (Dtlogin*logo), /etc/dt/config/xfonts or the font path.
3. dtlogin at boot: an /etc/rc2.d script; Xservers/Xsetup/Xreset do what
   startx-z3 does (Z3660 mode, keyboard hand-back, console state).  Escape
   hatch: skip when /etc/dt/nodtlogin exists; serial login always stays.

## Status 2026-10-25 (evening)

CDE runs on the A3000: session, front panel, dtterm, Style Manager, help
viewer, dtlogin (manual start: /usr/dt/bin/dtlogin -nodaemon).

Fixed today:
- gcc14 fold-mem-offsets and pointer-return ABI;
- vfork corrupting the parent (-Dvfork=fork);
- the sysroot libsocket variant;
- dtlogin shadow passwords;
- Lucida Sans aliases and 100dpi in the font path;
- XKeysymDB/XErrorDB;
- session PATH and the R6.3 runtime (/usr/x11r6/lib/r63) in .dtprofile and .bashrc.

Open, left as is for now (user, 2026-10-25):
- dtinfo is not built (DtMmdb/nsgmls C++), so the front panel's book icon fails.
Still to do:
- dtlogin at boot (rc2.d script, /etc/dt/nodtlogin escape);
- backdrops not selectable and File Manager home looking empty: to check with the user;
- Sun artwork from a Solaris 10 VM.
