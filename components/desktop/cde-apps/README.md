# cde-apps-amix

Applications for CDE on Commodore Amiga UNIX (AMIX), for an A3000 with a Z3660.
One directory per application.

| Directory | What it is |
|---|---|
| `amixcast/` | Streaming: a window with the tree of a DLNA server's folders. `amixcast.py` runs on a PC, reads the server and has VLC transcode at the bitrate and from the time asked for; ffplay shows it through the Z3660's second ARM core |
| `ztopcde/` | The Z3660 in a window: temperatures, clocks, and the programs of its second ARM core |
| `amixsysmon/` | System Monitor, after GNOME's: processes (end, kill), the last minute of CPU, memory, swap and network, the file systems, the Z3660; on [library/amixstat](../../library/amixstat) |
| `panel/` | The AMIX control in CDE's front panel, with a subpanel holding both |

The core 1 programs and `armbench` are in
[amix-gate firmware/core1](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1).

## Building

`build.sh` cross-builds them with gcc14-cross-amix against the shared Motif and
X11R6.3 libraries, taking the flags from `tools/amix-pkg-config xm` (and
`amixstat` for amixsysmon: stage library/amixstat into the sysroot first).
Each directory has an `install.sh` to run on the box.

## AmixCast

On the PC, with VLC and GStreamer's `gst-discoverer-1.0`:

    python3 amixcast/amixcast.py --server http://<DLNA server>:<port>

On the Amiga the PC's address goes in `$HOME/.amixcast` ("host port"), or as
arguments. Double-click a folder to open it, a video to play it. In ffplay:
Left/Right 10 s, Down/Up 60 s, 1, 2 and h the size, f fullscreen, q quit.

The network sets the limit: about 1 Mbit/s on the Z3660's Ethernet under AMIX.
Auto measures it and leaves a margin.
