# fsv for AMIX

[fsv](https://github.com/mcuelenaere/fsv), the 3D file system visualizer and clone of IRIX's fsn (the "It's a Unix system!" file browser from Jurassic Park), with a Motif interface in place of GTK+, so it needs nothing beyond Motif, Mesa 18.3 and GLU. It draws through Mesa on the Z3660's core 1.

- `fsv [directory]`: a launcher that loads the GPU program on core 1 first, then runs `/usr/local/libexec/fsv`.
- MapV and TreeV views, the directory tree (an outline XmContainer) and file list, colour by node type, timestamp or wildcards, the Properties dialog and the right-click menu.
- The 3D view uses fsn's IRIX colours: black ground, a green horizon, pink-red directories and cyan files.
- Help → Contents shows the manual as text (`/usr/local/share/fsv/fsv.txt`), and CDE gets an action and icons.

The component is the upstream source at commit 4ef9040 with two patches: `0001-motif-interface.patch` (the Motif front end and a small GLib subset, replacing the GTK+ files) and `0002-core-without-gtk.patch` (the 3D core on Xt timers, X events and GLX; the highlight is drawn with each frame instead of into the front buffer, which software GL does not keep). `files/` has the hand-written `config.h`, the build rules, and for AMIX an `fnmatch()` and the prototypes its headers hide.

Build: `make build stage`. Install on AMIX as root: `cd / && /usr/gnu/bin/tar --no-overwrite-dir -xf fsv-amix.tar`.
