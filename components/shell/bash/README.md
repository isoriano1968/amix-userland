# bash for AMIX

GNU bash 5.2.37 in `/usr/gnu/bin`, with line editing: Tab completion and arrow-key history, through its bundled readline on `library/ncurses`. It builds on `gnu/gnu.mk`.

No multibyte support: AMIX has the C locale only. `time` shows 0 for user and system time, because AMIX has no `getrusage`. `patches/` holds a fix for the upstream build without multibyte.

## Startup files

Built with `SYS_BASHRC=/etc/bash/bashrc` and `SYS_BASH_LOGOUT=/etc/bash/bash_logout`
(patch 0002, as OpenIndiana builds bash). `/etc/bash/bashrc` puts `/usr/gnu/bin`
first in PATH, then `/usr/x11r6/bin`, `/usr/dt/bin`, `/usr/local/bin`, `/opt/amix/bin`
and AMIX's own directories (each once, only when it exists), sets MANPATH, a
`user@host:dir$` prompt and a terminal when none is set, and reads
`/etc/bash/bashrc.d/*.sh`. Local changes to it survive updates. New users get
`/etc/skel/.bashrc` and `.bash_profile` (a login shell reads the system bashrc
from there); the same two are in `/usr/share/bash/skel` to copy into an existing
home. AMIX's `/etc/profile` is left as it is.
