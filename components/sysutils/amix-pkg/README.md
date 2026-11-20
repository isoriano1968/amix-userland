# pkg: the AMIX IPS client

`pkg` installs and checks packages on AMIX from an IPS repository: the
format OpenIndiana's `pkgsend` and `pkgrepo` write, served by any HTTP
server or read from a folder. The plan is in
[IPS-INSTALLER-PLAN.md](../../../doc/plans/IPS-INSTALLER-PLAN.md), the
formats in [IPS-CLIENT-DESIGN.md](../../../doc/plans/IPS-CLIENT-DESIGN.md).

Version 0.5: the read side, the write side, several origins, search and the CPU check.

    pkg set-publisher -O http://192.168.0.10/amix amix   # or file:///path, or a folder
    pkg set-publisher -g URL       # a mirror more; -G URL (or '*') removes one (all)
    pkg set-publisher --disable-origin URL   # kept, not used; --enable-origin
    pkg unset-publisher            # every origin forgotten
    pkg publisher [-t]             # the origins, on/off, the one in use (-t: tabs)
    pkg refresh                    # the catalog (the first origin that answers),
                                   # its SHA-1 signatures checked
    pkg list [-a] [-u] [-H] [-t]   # installed; -a everything; -u updates; -t tabs:
                                   # name, version, iu flags, cpu, newest, requires, summary
    pkg search [-t] text           # name or summary holds text
    pkg info [-r] name[@version]   # -r: from the repository
    pkg contents [-r] [-m] [-t file|dir|link] name
    pkg verify [-q] [name...]      # files, sizes, SHA-512/256, modes, owners, links

    pkg install [-n] name[@version]...   # with what they require
    pkg update [-n] [name...]            # all installed, without names
    pkg uninstall [-n] name...           # refused while another package needs it
    pkg history

A package for the 68060 only (`set name=org.amix.cpu value=68060`) is refused on a
machine whose kernel names no 68060 (uname); `PKG_CPU=060|030` decides in tests.

Every change is checked first: no path of another IPS package, of stock
AMIX or an SVR4 package (`/var/sadm/install/contents`; directories may be
shared), and no file nobody owns is overwritten, and 100 MB stay free on each
file system it writes to (`--min-free MB`). Files are written as `<path>.pkgtmp`, checked
(size, SHA-512/256) and renamed after a journal's commit point; an
interrupted change is undone or finished by the next one. A configuration
file you changed stays; the package's goes beside it as `.new`. A file you
changed is never removed: it is kept as `.pkgsave`.

A file may move between packages: when the package that has it now drops it
in its new version, both are changed in one run (an installed owner with a
newer version is updated along), the old path removed before the new file is
renamed in. When `sysutils/amix-pkg` itself has an update, pkg installs that
alone first and runs the same command again as the new pkg.

`-R root` works on an image below another root (tests, a mounted disk);
`PKG_IMAGE` does the same. Without it, pkg changes nothing but AMIX.

## Build and test

    make build    # the AMIX binary: gcc 14 cross, the 68020-68060 baseline, zlib
    make test     # host build, unit tests (FIPS hash vectors, JSON, actions,
                  # versions) and tests/run.sh against tests/repo: 80 checks,
                  # conflicts, preserved files, update, uninstall, crashes
    make stage

`tests/repo` is a small repository published with OpenIndiana's `pkgsend`
(demo/hello 1.0 and 1.1, library/world): the catalog format and the
signatures come from the real tool. The SHA-1 and SHA-512/256 code is
built in, without OpenSSL.

HTTP is `src/http.c`, plain HTTP/1.0 without TLS: stock AMIX has no
`/dev/urandom`, so OpenSSL (and libcurl with it) cannot start there.
Every payload is checked against the hash in its manifest. `pkg http get
URL [FILE]` and `pkg http put URL` are what the AMIX Installer's bootstrap
uses instead of curl. Free space is checked on every file system a change
writes to, so `/usr/local` on a data disk (a symlink) counts there.
