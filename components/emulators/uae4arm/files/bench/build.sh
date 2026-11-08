#!/bin/sh
# build.sh OUT -- c1bench.adf (CoreMark for an emulated Amiga, 68000 code)
# and coremark.amix (the same natively on AMIX, -m68060 and -m68000)
# from the pristine CoreMark 1.01 archive.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../../build/bench}
TGZ=${COREMARK_TGZ:-$HERE/../../../../../../distfiles/coremark/coremark-1.01.tar.gz}
ITER_EMU=${ITER_EMU:-300}
ITER_060=${ITER_060:-3000}
GCC=${AMIX_TOOLS:-$HOME/opt/m68k-amix/bin}/m68k-elf-gcc
OBJCOPY=${AMIX_TOOLS:-$HOME/opt/m68k-amix/bin}/m68k-elf-objcopy
mkdir -p "$OUT"
echo "99c5a6d63af85a281b4e4d6ccb522c446653c435dfec9455ad73ef9e71f28bde  $TGZ" | sha256sum -c -
rm -rf "$OUT/coremark-1.01"
tar xzf "$TGZ" -C "$OUT"
CM=$OUT/coremark-1.01
SRCS="$CM/core_list_join.c $CM/core_main.c $CM/core_matrix.c $CM/core_state.c $CM/core_util.c"

# the Amiga boot disk
$GCC -m68000 -O2 -ffreestanding -fno-builtin -nostdlib -I"$HERE/amiga" -I"$CM" \
    -DITERATIONS=$ITER_EMU -DFLAGS_STR='"-O2 -m68000"' -Wl,-T,"$HERE/amiga/link.ld" \
    "$HERE/amiga/crt0.S" $SRCS "$HERE/amiga/core_portme.c" -lgcc -o "$OUT/c1bench.elf"
$OBJCOPY -O binary "$OUT/c1bench.elf" "$OUT/c1bench.bin"
SIZE=$(stat -c %s "$OUT/c1bench.bin")
LOADSZ=$(( (SIZE + 511) / 512 * 512 ))
$GCC -m68000 -c -DLOADSZ=$LOADSZ -x assembler-with-cpp "$HERE/amiga/boot.S" -o "$OUT/boot.o"
$OBJCOPY -O binary "$OUT/boot.o" "$OUT/boot.bin"
python3 -I "$HERE/mkbenchadf.py" "$OUT/boot.bin" "$OUT/c1bench.bin" "$OUT/c1bench.adf"

# natively on AMIX: coremark.060 (-m68060) and coremark.000 (-m68000, the emulated code)
for cpu in 68060 68000; do
    $GCC -m$cpu -O2 -I"$HERE/amix" -I"$CM" -DFLAGS_STR="\"-O2 -m$cpu\"" -DCOMPILER_FLAGS="\"-O2 -m$cpu\"" \
        $SRCS "$HERE/amix/core_portme.c" -o "$OUT/coremark.${cpu#68}"
done
echo "run on AMIX: ./coremark.060 0x0 0x0 0x66 $ITER_060   (and coremark.000)"
