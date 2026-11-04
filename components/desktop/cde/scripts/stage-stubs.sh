#!/bin/sh
# stage-stubs.sh STAGE SYSROOT -- the link stubs of the shared libraries from
# the shared X, Motif, Mesa and CDE build, and of the sysroot libraries whose
# components do not make their own yet: tools/amix-link-stub writes each into
# STAGE/usr/local/share/amix-stubs/<component>, the place make-rules/shlib.mk
# uses, and on the box amix-stubs (sysutils/amix-pkgtools) builds them.
# Run it after stage-dev.sh, whose glue libraries get stubs too.  libamixc99.a,
# which the stubs name for what the libraries need from it, goes into
# STAGE/usr/local/lib.
set -e
ST=$1; SR=$2
T=$(cd "$(dirname "$0")/../../../../tools" && pwd)
S=$ST/usr/local/share/amix-stubs
K=$(ls $ST/usr/x11r6/lib/lib*.so.7 $ST/usr/motif/lib/lib*.so.4 $ST/usr/dt/lib/lib*.so.[12] $ST/usr/local/lib/libGL.so.18 $ST/usr/local/lib/libGLU.so.1 $ST/usr/local/lib/libglut.so.3 $ST/usr/local/lib/libzzgl.so.1 \
	$SR/usr/local/lib/libz.so.1 $SR/usr/local/lib/libpng16.so.16 $SR/usr/local/lib/libmikmod.so.3 $SR/usr/local/lib/libncurses.so.6 $SR/usr/local/lib/libform.so.6 \
	$SR/usr/local/lib/libmenu.so.6 $SR/usr/local/lib/libpanel.so.6)
for L in $K; do
	b=${L##*/}; d=${L%/*}; d=${d#$ST}; d=${d#$SR}
	case $d/$b in
	/usr/x11r6/*) c=x11r6.3;; /usr/motif/*) c=motif;; /usr/dt/*) c=cde;;
	*/libGL.so.*) c=mesa-183;; */libGLU.so.*) c=glu;; */libglut.so.*) c=freeglut;; */libzzgl.so.*) c=zzgl;;
	*/libz.so.*) c=zlib;; */libpng16.so.*) c=libpng;; */libmikmod.so.*) c=libmikmod;; *) c=ncurses;;
	esac
	mkdir -p "$S/$c"
	{ case $b in libGL.so.18) export STUB_ONLY="^(gl[A-Z]|glX|GL|OSMesa|zzgl_)";; *) unset STUB_ONLY;; esac; }
	"$T/amix-link-stub" "$L" "$S/$c/$b.s" -k $K > /dev/null
	grep -qx "$b $d" "$S/$c/LIST" 2>/dev/null || echo "$b $d" >> "$S/$c/LIST"
done
cp "$($HOME/opt/m68k-amix/bin/m68k-elf-gcc -print-file-name=libamixc99.a)" "$ST/usr/local/lib/"
