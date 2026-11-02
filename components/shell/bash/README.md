# bash for AMIX

GNU bash 5.2.37 in `/usr/gnu/bin`, with line editing: Tab completion and arrow-key history, through its bundled readline on `library/ncurses`. It builds on `gnu/gnu.mk`.

No multibyte support: AMIX has the C locale only. `time` shows 0 for user and system time, because AMIX has no `getrusage`. `patches/` holds a fix for the upstream build without multibyte.
