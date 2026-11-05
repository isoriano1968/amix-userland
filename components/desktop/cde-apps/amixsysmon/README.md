# amixsysmon

A system monitor for AMIX in CDE, after GNOME's, in four tabs:

- **Processes**: PID, user, state, CPU share, memory (resident and virtual),
  CPU time and command; sorted by CPU, memory, PID or name; End (SIGTERM)
  and Kill (SIGKILL) after asking. Root sees every process, a user their own.
- **Resources**: the last minute of the CPU (user and system), memory and
  swap, and network packets in and out on the first interface that is not
  the loopback.
- **File Systems**: device, mount point, type, size, free and available,
  with a bar.
- **Z3660**: what `ztop` prints, every two seconds.

The numbers come from [library/amixstat](../../../library/amixstat), which
reads the running kernel through `/dev/kmem`, so `amixsysmon` is installed
setgid `sys`.

## The CPU on AMIX

AMIX's idle loop is `stop #0x2000`, and the clock books an idle tick as
system time of the process that ran last. Neither the kernel's `sysinfo` nor
the processes' times show idle time; `flopd` and `fsflush` look busy. The
library takes the CPU's shares from the kernel profiler (`/dev/prf`), which
samples every clock tick, with three buckets around `idle`. A process's user
time is right; its system time is scaled to the kernel's real share, so the
column adds up but a single process's system share is an estimate.

If `prf` is already in use for a profile (`prfld`), the library leaves it
alone and the CPU graph says so.

## Build and install

`../build.sh amixsysmon` on the PC (library/amixstat staged first), then on
the box, as root, in a copy of this directory: `sh install.sh`, and Reload
Actions in CDE. It appears in the Application Manager under Desktop_Apps.
