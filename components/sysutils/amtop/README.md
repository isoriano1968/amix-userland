# amtop

`amtop` is a small curses system monitor for AMIX and other old SVR4 systems.
It is intentionally conservative: instead of depending on modern `/proc`
layouts, kernel memory offsets, or ncurses-only calls, it samples the tools
that are normally available on System V systems when present:

- `ps -el`
- `swap -s`
- `swap -l`

The display refreshes every five seconds and shows system load-style counters,
memory and swap summaries when available, and the currently running process
table.

## Build

```sh
cd amtop
make
```

If AMIX uses a different terminal library set, try one of these:

```sh
make LIBS=-lncurses
make LIBS=-lcurses
make LIBS="-lcurses -ltermlib"
```

## Install

```sh
cp amtop /usr/local/bin/amtop
```

## Keys

- `q` quits
- `Ctrl-L` redraws the screen

## Notes

This first version deliberately avoids reading `/dev/kmem`.  That keeps it
usable on stock AMIX installations without matching the running kernel's symbol
table and private structure layout.  A later AMIX-specific backend could add
true load averages and per-process CPU deltas using procfs or kernel symbols
once the exact installed headers are available.

When `vmstat` is not installed, the load / CPU / paging line is left blank.
