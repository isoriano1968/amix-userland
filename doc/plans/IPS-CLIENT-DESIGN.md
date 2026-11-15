# The AMIX IPS client: formats and database

Design note of 2026-11-15, step 1 of IPS-INSTALLER-PLAN.md. It fixes what
the client reads and writes, so the manifest generator (`tools/amix-p5m`),
the client in C and the AMIX Installer agree. Nothing here is built on
AMIX yet.

## Packages

**Publisher** `amix`. **FMRI** `pkg://amix/<category>/<name>@<version>,2.1-<rev>`:

- `<category>/<name>` is the component's place under `components/`
  (`x11/x11r6.3`), the same name a manifest's `require` uses.
- `<version>` is the manifest's `version`, digits and dots only (IPS
  versions are dot-separated integers: `2.2.4a` becomes `2.2.4.1`, the
  letter as a number); a component without one is `0`.
- `2.1` is the build release: AMIX 2.1.
- `<rev>` is the number of commits that touched the component
  (`git rev-list --count HEAD -- components/<category>/<name>`): it only
  grows, so an update is seen without anyone bumping it by hand. A dirty
  tree adds `.1` so a test publish never claims a released revision.
- `pkgsend` adds the timestamp.

**Actions** generated from the component's stage and manifest:

| Action | From |
| --- | --- |
| `set pkg.fmri`, `pkg.summary`, `variant.arch=m68k`, `org.amix.component` | manifest `name`, `summary`, `version` |
| `dir` | every directory in the stage; a directory stock AMIX has keeps its owner, group and mode from the SVR4 contents file (`/usr/local bin bin 0755`), so installing never changes a stock directory; new ones are `root bin 0755` |
| `file` | every file: `owner=root group=bin`, mode `0555` if the stage's is executable, else `0444`; under `etc/` `preserve=renamenew` and `0644` (the client puts the package's copy beside a changed file as `.new`, as `amix-install` does today) |
| `link` / `hardlink` | symbolic links as they are; hard links as `hardlink` to the first path |
| `facet.devel=true` | on manifest `file ... devel` lines, and always on headers (`/include/`), `*.pc`, `lib*.so` links and link stubs |
| `depend type=require` | each manifest `require`, with its `>= VERSION` as `@VERSION` |
| `license` | the component's `.license`, `license=<category>/<name>` |

A `meta/` component (meta/amix-dev) is only its `require` lines: its
members bring the files, so no path belongs to two packages.

When a manifest lists files, the stage and the list must agree (both
ways), or the generator stops; a manifest without `file` lines takes the
stage as it is (and says so).

**Repository**: the plain file layout `pkgsend` and `pkgrepo` write
(`publisher/amix/catalog/catalog.{attrs,base.C,dependency.C}`,
`publisher/amix/pkg/<stem>/<version>`, `publisher/amix/file/<xx>/<sha1>`,
files gzipped). Any HTTP server serves it, the installer's own included,
and `pkgrepo`/`pkglint` on OpenIndiana check it. Hashes as OI's pkg writes
them: the SHA-1 of the content names the file, `pkg.content-hash` carries
`file:sha512t_256` and `gzip:sha512t_256`; the client checks SHA-512/256
(OpenSSL has it) and the size.

## What the client keeps: /var/pkg

Plain text, one file per thing, written as `.new` and renamed, so a
68060 at 80 MHz reads it with stdio and an interrupted write never leaves
half a file.

    /var/pkg/cfg                       publisher origin(s), facets (devel=true|false)
    /var/pkg/publisher/amix/catalog/   catalog.attrs, catalog.base.C as fetched
    /var/pkg/installed/<stem>          the installed manifest (with its hashes)
    /var/pkg/paths                     every path the client owns, sorted:
                                       path TAB type TAB stem TAB sha512t_256
    /var/pkg/journal                   the current transaction (see below)
    /var/pkg/history                   one line per change: time, command, result
    /var/pkg/lock                      one client at a time (O_EXCL)

`<stem>` is `category%2Fname`, as in the repository. `paths` is what the
conflict check reads; it is rebuilt from `installed/*` by `pkg rebuild-index`.

**Transaction** (install, update, uninstall): the client writes the whole
list of renames and removals to `journal` before the first one, each file
lands as `<path>.new` with its hash checked, then the renames run in order
and the journal is removed. On the next start an existing journal is
finished (all `.new` files present and good) or rolled back (they are
removed); nothing else is touched.

## Conflicts

Before anything is written, every path of the change is checked against:
`/var/pkg/paths` (another IPS package), `/var/sadm/install/contents` (stock
AMIX and SVR4/apkg packages; for directories the attributes only), and the
disk (a file nobody owns). A conflict stops the change and names the path
and its owner.

## Not decided yet

- **Link stubs** (`amix-stubs`, run by `amix-install` after a package that
  brings `usr/local/share/amix-stubs/<name>/`): IPS has no install
  scripts. Either the stubs are built when the package is made (they only
  depend on the package's own libraries), or the client gets one fixed
  post-step for packages tagged `org.amix.stubs=<name>`. I would build
  them at package time.
- The SVR4 side with Jussi (apkg and the client never overwriting each
  other's files): IPS-INSTALLER-PLAN.md step 4.
