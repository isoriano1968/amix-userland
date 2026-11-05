# amixtop

`amixtop` is a small curses system monitor for AMIX, in the manner of `top`:

- the CPU's user and system shares, with a bar;
- memory and swap in use, with bars;
- packets a second on the network interface;
- the processes by state, and the Z3660's core 1 from `ztop`;
- the processes sorted by their CPU share: PID, user, state, CPU %,
  resident and virtual size, CPU time and command.

The display refreshes every two seconds, in colour where the terminal has it
(dtterm does; the Amiga console's entry has none).

Its numbers come from [library/amixstat](../../library/amixstat), which reads
the running kernel through `/dev/kmem`, so amixtop is installed setgid `sys`.
Without that group it still lists the processes and says that memory and swap
are unknown. AMIX books idle time as system time of the process that ran last;
the CPU's shares come from the kernel profiler, and a process's system time
is scaled to the kernel's real share, so that single figure is an estimate.

It was called amtop until 2026-11-05. The first version read `ps`, `swap -s`
and `swap -l`; on AMIX `swap -s`
prints a negative reserved figure and there is no command for real memory.

## Build

Cross, with the sysroot holding library/amixstat and library/ncurses:

```sh
make build stage
```

`build/amixtop-amix.tar` is installed with `amix-install`, which makes amixtop
setgid `sys`. On the Amiga itself, with both libraries installed:

```sh
make -f Makefile.amix
```

## Keys

- `q` quits
- `Ctrl-L` redraws the screen
