# libXp for AMIX

The X Print client library of X11R6.3: `libXp.so.7` in `/usr/x11r6/lib`,
shared like the other X11R6.3 libraries, with `xp.pc`. CDE links it.

    gmake AMIX_SYSROOT=/path/to/sysroot stage

takes `xc/lib/Xp` and `extutil.h` from the pinned `xc-1.tar.gz` (the
checksum is x11/x11r6.3's `SOURCES.sha256`; the download cache is
x11r6.3's `distfiles/`), compiles them with gcc 14 against the X11R6.3
headers in the sysroot, and makes the shared library with
`tools/amix-mkshlib`. The AMIX port patch does not touch `lib/Xp`. The
result is byte-identical to the libXp.so.7 built before from a prepared tree.

Under X11R6.3's licence.
