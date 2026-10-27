#!/bin/sh
# build-deps.sh -- zlib 1.2.13, libjpeg 6b and libpng 1.2.59 for AMIX, as
# static libraries built by gcc14-cross-amix for the 68060, into deps/
# (include/, lib/).  Blender 2.31a's image code needs all three.
# Sources: ../downloads/blender-deps (checksums in README there).
set -e
H=$(cd "$(dirname "$0")" && pwd)
DL=$H/../downloads/blender-deps
P=$H/deps
W=$H/work
export PATH=$HOME/opt/m68k-amix/bin:$PATH
CC=m68k-elf-gcc
AR=m68k-elf-ar
CFLAGS="-O2 -m68060 -U__linux__ -Ulinux -D__amix__ -w"
rm -rf "$W" "$P"; mkdir -p "$W" "$P/include" "$P/lib"

# zlib: its own configure, static only; the test programs are not built
cd "$W" && tar xzf "$DL/zlib-1.2.13.tar.gz" && cd zlib-1.2.13
CC=$CC AR=$AR RANLIB=m68k-elf-ranlib CFLAGS="$CFLAGS" CHOST=m68k-elf \
	./configure --static --prefix="$P" > "$W/zlib-configure.log" 2>&1
make libz.a > "$W/zlib-make.log" 2>&1
cp libz.a "$P/lib/"; cp zlib.h zconf.h "$P/include/"

# libjpeg 6b: configure predates cross builds; jconfig.h by hand (m68k: signed
# char, big-endian is irrelevant to libjpeg, ANSI C with prototypes)
cd "$W" && tar xzf "$DL/jpegsrc.v6b.tar.gz" && cd jpeg-6b
cat > jconfig.h <<'JC'
#define HAVE_PROTOTYPES
#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#ifdef JPEG_INTERNALS
#undef RIGHT_SHIFT_IS_UNSIGNED
#endif
JC
OBJ=$(sed -n '/^LIBOBJECTS=/,/^$/p;/^COMOBJECTS=/,/^$/p;/^CLIBOBJECTS=/,/^$/p;/^DLIBOBJECTS=/,/^$/p;/^SYSDEPMEM=/p' makefile.ansi | tr '\\' ' ' | sed 's/^[A-Z]*= *//' | tr ' \t' '\n\n' | grep '\.o$' | sort -u)
for o in $OBJ; do
	c=${o%.o}.c; [ "$o" = "jmemnobs.o" ] || [ -f "$c" ] || continue
	$CC $CFLAGS -c $c -o $o
done
[ -f jmemnobs.o ] || $CC $CFLAGS -c jmemnobs.c -o jmemnobs.o
$AR rcs libjpeg.a $(ls *.o | grep -v "^cjpeg\|^djpeg\|^jpegtran\|^rdjpgcom\|^wrjpgcom\|^cdjpeg\|^rdbmp\|^rdgif\|^rdppm\|^rdrle\|^rdtarga\|^wrbmp\|^wrgif\|^wrppm\|^wrrle\|^wrtarga\|^rdcolmap\|^rdswitch\|^transupp\|^jmemansi\|^jmemname\|^jmemdos\|^jmemmac")
cp libjpeg.a "$P/lib/"; cp jpeglib.h jconfig.h jmorecfg.h jerror.h "$P/include/"

# libpng 1.2.59: the library sources directly, against the zlib above
cd "$W" && tar xzf "$DL/libpng-1.2.59.tar.gz" && cd libpng-1.2.59
for c in png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c \
	pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c; do
	$CC $CFLAGS -I"$P/include" -c $c
done
$AR rcs libpng.a png*.o
cp libpng.a "$P/lib/"; cp png.h pngconf.h "$P/include/"

ls -l "$P/lib"
