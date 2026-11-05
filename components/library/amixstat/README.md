# amixstat

`libamixstat` tells a program what the running AMIX system is doing: memory,
swap, CPU, processes, file systems and network interfaces (`amixstat.h`,
`pkg-config amixstat`). The `amixstat` command prints it all; `amixstat 5`
adds the CPU's shares over five seconds and each process's. The System
Monitor ([desktop/cde-apps/amixsysmon](../../desktop/cde-apps/amixsysmon)) is
built on it.

## Where the numbers come from

AMIX has no kstat, and its `sar` and `netstat -i` look the kernel's variables
up in `/unix`, which is not the kernel that runs on an A3000 with a Z3660.
The library finds the running kernel in `/stand` by the build id `uname -a`
prints, and reads its variables through `/dev/kmem`:

- the kernel files are relocatable ELF, loaded at `0x08000000` with their
  sections one after the other, so a symbol defined in a section has its
  address from the section map;
- a COMMON symbol (`physmem`, `freemem`, `anoninfo`, `sysinfo`, `ifstats`)
  is placed at load time: its address is read from the live kernel at the
  relocation sites in `.text` that use it, and they must all agree.

Processes come from `/proc`, file systems from `/etc/mnttab` and `statvfs`,
the interfaces from the IP module's `ifstats` chain.

The CPU: AMIX's idle loop is `stop #0x2000`, and the clock books an idle tick
as system time of the process that ran last, so neither `sysinfo` nor the
processes' times show idle time. The library sets the kernel profiler
(`/dev/prf`) to three buckets around `idle` and reads how many clock ticks
found the CPU in user mode, in the kernel or stopped in `idle`. It does so
only when `prf` is off or already set so; a profile someone loaded with
`prfld` is left alone, and the CPU shares are then unknown.

## Using it

`/dev/kmem` is readable by group `sys`: a program using the library is
installed setgid `sys` (`amix-install` does it for `amixstat`). Without root,
`/proc` shows one's own processes.

Sizes are in KB and times in ms. The structures hold no `double`: AMIX's
gcc 2.7 aligns one in a struct to 8 bytes and GCC 14 to 4, and the library
serves programs of both compilers.
