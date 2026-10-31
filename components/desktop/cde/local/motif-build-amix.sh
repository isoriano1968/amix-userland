#!/bin/sh
# motif-build-amix.sh -- rebuild Motif 2.3.8 for AMIX with gcc14-cross-amix in
# build-motif-amix/ (configured earlier; see its config.log), install libXm/
# libMrm and the headers into the gcc14 sysroot, and link mwm.
#
#   sh motif-build-amix.sh [clean]
#
# "clean" deletes the m68k objects and archives under lib/ and clients/ first,
# which is what a compiler change needs.  tools/ and config/util/ are left
# alone: wml, wmluiltok and makestrs there are HOST binaries (from
# build-motif-host) and must not be rebuilt with the cross compiler.
#
# The -Wno-error set: GCC 14 made these errors, and Motif 2.3.8 plus the 1991
# R6.3 headers (implicit int, select() with no prototype, ...) trip all of them.
# mwm links Xt/Xmu through the R6.3 .sa stubs, and libgen for regcmp/regex.
set -e
B=$HOME/Sources/usr-amix/build-motif-amix
SR=$HOME/opt/m68k-amix-sysroot
L=$SR/usr/x11r6/xc/exports/lib
export PATH=$HOME/opt/m68k-amix/bin:$PATH
W="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=return-mismatch -Wno-error=declaration-missing-parameter-type"
CF="-O2 -U__linux__ -Ulinux -D__amix__ -D__svr4__ -DSVR4 -fno-strict-aliasing -fcommon $W"
cd "$B"

if [ "$1" = clean ]; then
	find lib clients \( -name '*.o' -o -name '*.lo' -o -name '*.a' -o -name '*.la' \) -print0 | xargs -0 rm -f
	find lib clients -name .libs -type d -prune -print0 | xargs -0 rm -rf
fi

make -j12 -C lib CFLAGS="$CF" > make-lib.log 2>&1 || { echo "lib failed: make-lib.log"; exit 1; }
make -C lib install DESTDIR=$SR > make-install-lib.log 2>&1
make -C include install DESTDIR=$SR > make-install-inc.log 2>&1
make -j12 -C clients/mwm CFLAGS="$CF" \
	LIBS="$L/libXt.sa $L/libXmu.sa -lXext -lSM -lICE -lX11 -lgen -lsocket -lnsl" \
	> make-mwm.log 2>&1 || { echo "mwm failed: make-mwm.log"; exit 1; }
m68k-elf-strip -o mwm.stripped clients/mwm/mwm
ls -la $SR/usr/motif/lib/libXm.a mwm.stripped
