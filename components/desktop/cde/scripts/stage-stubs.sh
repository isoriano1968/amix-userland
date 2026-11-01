#!/bin/sh
# stage-stubs.sh STAGE SYSROOT OUT -- source for the link stubs of every shared
# library, to build on the box with its own as and ld (AMIX-STUBS.sh in OUT):
# see tools/amix-link-stub.  OUT also takes libamixc99.a.
set -e
ST=$1; SR=$2; OUT=$3
T=$(cd "$(dirname "$0")/../../../../tools" && pwd)
K=$(ls $ST/usr/x11r6/lib/lib*.so.7 $ST/usr/motif/lib/lib*.so.4 $ST/usr/dt/lib/lib*.so.2 $ST/usr/local/lib/lib*.so.18 $ST/usr/local/lib/libzzgl.so.1 \
	$SR/usr/local/lib/libmikmod.so.3 $SR/usr/local/lib/libncurses.so.6 $SR/usr/local/lib/libform.so.6 \
	$SR/usr/local/lib/libmenu.so.6 $SR/usr/local/lib/libpanel.so.6)
mkdir -p "$OUT"; : > "$OUT/LIST"
for L in $K; do
	b=${L##*/}; d=${L%/*}; d=${d#$ST}; d=${d#$SR}
	{ case $b in libGL.so.18) export STUB_ONLY="^(gl[A-Z]|glX|GL|OSMesa|zzgl_)";; *) unset STUB_ONLY;; esac; }
	"$T/amix-link-stub" "$L" "$OUT/$b.s" -k $K "$SR/usr/lib/libc.so.1" "$SR/usr/lib/libnsl.so" "$SR/usr/lib/libsocket.so" > /dev/null
	echo "$b $d" >> "$OUT/LIST"
done
cp "$($HOME/opt/m68k-amix/bin/m68k-elf-gcc -print-file-name=libamixc99.a)" "$OUT/"
cat > "$OUT/AMIX-STUBS.sh" <<'E'
cd `dirname $0`
while read b d; do
	n=`echo $b | sed 's/\.so\..*/.so/'`
	/usr/ccs/bin/as -o $b.o $b.s
	/usr/ccs/bin/ld -G -h $b -o $n.stub $b.o
	if [ -s $n.stub ]; then rm -f $d/$n; mv $n.stub $d/$n; else echo "$b FAILED"; fi
done < LIST
cp libamixc99.a /usr/local/lib/
E
