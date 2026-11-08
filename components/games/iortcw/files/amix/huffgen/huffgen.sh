#!/bin/sh
# huffgen.sh SRC -- SRC is iortcw's SP/code (patched); writes ../huffinit.h
# from msg.c's msg_hData with the game's own huffman.c, then checks it.
set -e
SRC=$1
H=`dirname "$0"`
T=`mktemp -d`
trap 'rm -rf "$T"' EXIT
sed -n '/^int msg_hData\[256\] = {/,/^};/p' "$SRC/qcommon/msg.c" > "$T/hdata.inc"
[ -s "$T/hdata.inc" ]
cc -O2 -DARCH_STRING=\"host\" -I"$SRC/qcommon" -DHDATA="\"$T/hdata.inc\"" -o "$T/huffgen" "$H/huffgen.c" "$SRC/qcommon/huffman.c"
"$T/huffgen" > "$H/../huffinit.h"
cc -O2 -DARCH_STRING=\"host\" -I"$SRC/qcommon" -I"$H/.." -DHDATA="\"$T/hdata.inc\"" -o "$T/huffcheck" "$H/huffcheck.c" "$SRC/qcommon/huffman.c"
"$T/huffcheck"
