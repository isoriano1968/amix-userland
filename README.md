# amix-userland

Software for AMIX (Amiga UNIX System V Release 4) on the A3000 with a Z3660,
laid out like [oi-userland](https://github.com/OpenIndiana/oi-userland):
one directory per component, grouped by category.

## Layout

    amix-userland/
      components/<category>/<name>/            one version of a package
      components/<category>/<name>-<version>/  several versions side by side
      make-rules/                              shared build rules
      templates/                               a new component's starting point
      tools/                                   helpers for the tree
      doc/                                     how to add a component

A component holds what it takes to build it, not the built result
(`components/library/pth` is the worked example):

    <name>/
      Makefile        where the source comes from, version, configure and build
      <name>.license  the upstream licence
      patches/        the AMIX changes, numbered
      files/          files that are ours (drivers, scripts, config)

No binaries, and no game data or other third-party content that may not be
redistributed.

## Several versions

As in oi-userland, the version goes into the directory name once more than one
is kept, with the major and minor numbers only:

    components/shell/bash-44/
    components/shell/bash-52/
    components/runtime/python-27/
    components/runtime/python-311/
    components/x11/mesa-183/
    components/x11/mesa-31/
    components/multimedia/ffmpeg-44/
    components/library/sdl-2/

This holds for libraries as much as for programs. Each version is installed
under its own prefix (`/opt/<name>-<version>`), a program links against the
version it names in its Makefile, and one version is linked into
`/usr/local` as the default.

## Categories

| Category       | For                                  | Today's repos                                     |
| -------------- | ------------------------------------ | ------------------------------------------------- |
| `audio`        | sound and music                      | moved: libmikmod, pt2-clone                       |
| `desktop`      | desktop environments                 | moved: cde, cde-apps                              |
| `developer`    | compilers and development tools      | gcc-cross-amix, vscode-amix                       |
| `driver`       | kernel drivers, core 1 programs      | paula-amix, random-amix, hydra-amix, amix-z3660net, amix-z3660scsi, soft3d-z3660rtg-amix, core1-z3660-amix |
| `games`        | games and their engines              | (quake, rtcw: source patches only)                |
| `library`      | general libraries                    | moved: pth, sdl2                                  |
| `multimedia`   | video                                | moved: ffmpeg                                     |
| `runtime`      | language runtimes                    | moved: python3                                    |
| `shell`        | shells                               | (bash)                                            |
| `x11`          | X server, libraries, GL              | zz9000-amix; moved: x11r6.3, mesa-31, mesa-183    |

The kernel port (amix-040-060-port) and the Z3660 firmware fork (Z3660) stay
separate: they are not userland.

To move a repository in, see [doc/moving-a-repo.md](doc/moving-a-repo.md).

## Licence

Each component keeps its own upstream licence. What is ours is MIT.
