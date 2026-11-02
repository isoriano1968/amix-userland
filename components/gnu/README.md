# GNU tools for AMIX

Current GNU tools in `/usr/gnu/bin`, beside AMIX's own. `/usr/bin` stays as it is:
put `/usr/gnu/bin` first in your `PATH` to use them.

Each tool is a component here, on the shared recipe `gnu.mk`, and installs alone.
`gnu-tools` is a metapackage that brings them all, with `shell/bash`, which uses the
same recipe. `compat/amix-gnu.c`
has what current gnulib wants and a 1991 libc has not, among them the wide-character
functions, for the C locale only.
