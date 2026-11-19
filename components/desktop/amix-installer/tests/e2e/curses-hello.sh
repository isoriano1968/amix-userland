#!/bin/sh
# curses-hello.sh OUT -- console-hello (vscode's sample) linked with AMIX's own
# SVR4 curses, for amix-e2e.js's curses screen check (AMIX_E2E_CURSES); the
# same build as vscode-agent's tests/run-fsuae.sh.  AMIX_STOCK_INCLUDE: the
# stock headers where the sysroot's usr/include holds ncurses'.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=$(cd "$HERE/../../../../.." && pwd)
SYSROOT=${AMIX_SYSROOT:-$HOME/opt/m68k-amix-sysroot}
m68k-elf-gcc -DSYSV -O2 -m68020-60 -I"${AMIX_STOCK_INCLUDE:-$SYSROOT/usr/include}" \
	"$TOP/components/developer/vscode/samples/console-hello/hello.c" -o "$1" \
	-L"$SYSROOT/usr/ccs/lib" -lcurses
"$TOP/tools/amix-fix-needed" "$1" > /dev/null
