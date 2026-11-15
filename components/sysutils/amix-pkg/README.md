# pkg: the AMIX IPS client

`pkg` installs and checks packages on AMIX from an IPS repository: the
format OpenIndiana's `pkgsend` and `pkgrepo` write, served by any HTTP
server or read from a folder. The plan is in
[IPS-INSTALLER-PLAN.md](../../../doc/plans/IPS-INSTALLER-PLAN.md), the
formats in [IPS-CLIENT-DESIGN.md](../../../doc/plans/IPS-CLIENT-DESIGN.md).

This is version 0.1, the read side (step 2): it changes nothing on the
system except its own catalog copy in `/var/pkg`.

    pkg set-publisher -O http://192.168.0.10/amix amix   # or file:///path, or a folder
    pkg refresh                    # the catalog, its SHA-1 signatures checked
    pkg list [-a] [-u] [-H]        # installed; -a everything; -u updates
    pkg info [-r] name[@version]   # -r: from the repository
    pkg contents [-r] [-m] [-t file|dir|link] name
    pkg verify [-q] [name...]      # files, sizes, SHA-512/256, modes, owners, links

`-R root` works on an image below another root (tests, a mounted disk);
`PKG_IMAGE` does the same. Install, update and uninstall follow in step 3.

## Build and test

    make build    # the AMIX binary: gcc 14 cross, -m68020-60, libcurl, zlib
    make test     # host build, unit tests (FIPS hash vectors, JSON, actions,
                  # versions) and tests/run.sh against tests/repo
    make stage

`tests/repo` is a small repository published with OpenIndiana's `pkgsend`
(demo/hello 1.0 and 1.1, library/world): the catalog format and the
signatures come from the real tool. The SHA-1 and SHA-512/256 code is
built in, without OpenSSL; libcurl is only needed for http(s) origins.
