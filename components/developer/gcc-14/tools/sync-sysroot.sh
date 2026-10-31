#!/bin/bash
# sync-sysroot.sh -- add the X11 and screen-subsystem pieces to the AMIX sysroot.
#
# A sysroot assembled for C work holds just enough to build and link a C
# program: usr/include, libc and the crt objects.  A video driver needs
# more -- the X11 headers and libraries, and the socket libraries SVR4 puts Xlib
# on top of.  This copies them out of a mounted AMIX image.
#
# NONE OF WHAT THIS COPIES MAY BE COMMITTED.  It is licensed material from a real
# AMIX installation; the sysroot lives outside the repository for that reason.
#
# One thing needs fixing rather than copying: AMIX's usr/include/X11 is an
# ABSOLUTE symlink to /usr/X/include, which resolves against the host's root and
# finds nothing.  It is replaced with the equivalent relative link.
#
# Usage: sync-sysroot.sh /path/to/mounted/amix/image
set -e
cd "$(dirname "$0")/.."
[ -f config.sh ] || { echo "sync-sysroot.sh: no config.sh" >&2; exit 1; }
. ./config.sh
: "${AMIX_SYSROOT:?set AMIX_SYSROOT in config.sh}"

IMG="$1"
[ -n "$IMG" ] || { echo "usage: sync-sysroot.sh /path/to/mounted/amix/image" >&2; exit 1; }
[ -d "$IMG/usr/X/include" ] || { echo "sync-sysroot.sh: $IMG does not look like a mounted AMIX root (no usr/X/include)" >&2; exit 1; }

mkdir -p "$AMIX_SYSROOT/usr/X/include" "$AMIX_SYSROOT/usr/X/lib" "$AMIX_SYSROOT/usr/lib"

echo "== X11 headers"
# The image's files are mode 444 from 1991, and cp preserves that on the way in,
# so a second run cannot overwrite what the first one wrote.  Make the tree
# writable before copying, not after.
chmod -R u+wX "$AMIX_SYSROOT/usr/X" 2>/dev/null || true
cp -Rf "$IMG/usr/X/include/." "$AMIX_SYSROOT/usr/X/include/"
chmod -R u+wX "$AMIX_SYSROOT/usr/X/include"

echo "== X11 libraries"
for l in libX11.a libXext.a libXau.a libXdmcp.a libXmu.a; do
	[ -f "$IMG/usr/X/lib/$l" ] || continue
	rm -f "$AMIX_SYSROOT/usr/X/lib/$l"
	cp "$IMG/usr/X/lib/$l" "$AMIX_SYSROOT/usr/X/lib/$l" && chmod u+w "$AMIX_SYSROOT/usr/X/lib/$l"
done

echo "== the socket libraries Xlib needs on SVR4"
# Xlib on SVR4 talks TLI/sockets: every X client links -lX11 -lsocket -lnsl.
for l in libsocket.a libsocket.so libnsl.so libsockdns.so libsockhost.so; do
	[ -f "$IMG/usr/lib/$l" ] || continue
	rm -f "$AMIX_SYSROOT/usr/lib/$l"
	cp "$IMG/usr/lib/$l" "$AMIX_SYSROOT/usr/lib/$l" && chmod u+w "$AMIX_SYSROOT/usr/lib/$l"
done
if [ -f "$IMG/usr/ccs/lib/libm.a" ]; then
	rm -f "$AMIX_SYSROOT/usr/ccs/lib/libm.a"
	cp "$IMG/usr/ccs/lib/libm.a" "$AMIX_SYSROOT/usr/ccs/lib/libm.a"
	chmod u+w "$AMIX_SYSROOT/usr/ccs/lib/libm.a"
fi

echo "== our own headers: what SVR4.0 predates"
# stdint.h is not a convenience.  GCC's <stdint.h> for this target is a wrapper
# that #include_next's the C library's real one, so without this even
# libstdc++ will not build.  The file is ours (see compat/include/stdint.h);
# nothing licensed goes the other way.
mkdir -p "$AMIX_SYSROOT/usr/include"
rm -f "$AMIX_SYSROOT/usr/include/stdint.h"
cp compat/include/stdint.h "$AMIX_SYSROOT/usr/include/stdint.h"
chmod u+w "$AMIX_SYSROOT/usr/include/stdint.h"

echo "== relative X11 include link"
rm -f "$AMIX_SYSROOT/usr/include/X11"
ln -s ../X/include "$AMIX_SYSROOT/usr/include/X11"

echo "== verify"
for f in usr/include/X11/Xlib.h usr/include/X11/extensions/XShm.h usr/include/sys/screen.h \
         usr/include/stdint.h usr/X/lib/libX11.a usr/X/lib/libXext.a usr/lib/libsocket.a; do
	if [ -e "$AMIX_SYSROOT/$f" ]; then echo "  ok   $f"; else echo "  MISSING $f"; fi
done
