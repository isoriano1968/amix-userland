#!/bin/sh
# build-libxp.sh -- a static libXp (X Print client library) for CDE, from the
# X11R6.3 source tree; the AMIX X11R6.3 build installs only a shared one.
set -e
. "$(dirname "$0")/common.sh"
X=$X11_EXPORTS
[ -d "$X11_SRC/lib/Xp" ] || { echo "no $X11_SRC/lib/Xp (X11_SRC in config.sh)"; exit 1; }
rm -rf "$LIBXP"; mkdir -p "$LIBXP"
cd "$X11_SRC/lib/Xp"
for f in *.c; do
	m68k-elf-gcc -O2 -fno-strict-aliasing -fcommon -Wno-error=implicit-function-declaration \
		-Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-int \
		-DSVR4 -DAMIX -D__amix__ -I"$X/include" -I"$X/include/X11" -I"$X/include/X11/extensions" \
		-I"$X11_SRC/include/extensions" -I. -c "$f" -o "$LIBXP/${f%.c}.o" 2>> "$LIBXP/build.log" \
		|| { echo "failed: $f ($LIBXP/build.log)"; exit 1; }
done
m68k-elf-ar rcs "$LIBXP/libXp.a" "$LIBXP"/*.o
ls -l "$LIBXP/libXp.a"
