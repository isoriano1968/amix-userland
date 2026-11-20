# cde-apps-amix

Applications for CDE on Commodore Amiga UNIX (AMIX), for an A3000 with a Z3660.
One directory per application.

| Directory | What it is |
|---|---|
| `amixcast/` | Streaming: a window with the tree of a DLNA server's folders. `amixcast.py` runs on a PC, reads the server and has VLC transcode at the bitrate and from the time asked for; ffplay shows it through the Z3660's second ARM core |
| `ztopcde/` | The Z3660 in a window: temperatures, clocks, and the programs of its second ARM core |
| `amixsysmon/` | System Monitor, after GNOME's: processes (end, kill), the last minute of CPU, memory, swap and network, the file systems, the Z3660; on [library/amixstat](../../library/amixstat) |
| `amixdisplay/` | Display, after the Linux desktops' panels: the screen resolution and refresh, live through RandR with a 15 s "keep?" countdown; the user's own default and (setuid helper) the login screen's; design and mode table in [DESIGN.md](amixdisplay/DESIGN.md) |
| `amixprinters/` | Printers: finds network printers by UPnP (SSDP) and Bonjour (mDNS) broadcasts (`amixprfind`; AMIX has no multicast), adds one to the SVR4 lp spooler for its raw port through the setuid helper `prset`, default, test page, removal; Properties for paper, sides and colour, sent as PJL and PostScript setpagedevice by `prsend` (also per job: `lp -o duplex,mono`) |
| `amixuae/` | Amiga: the front end of the Amiga emulator on the Z3660's second ARM core ([emulators/uae4arm](../../emulators/uae4arm)): model, Kickstart, DF0-DF3, hard file, Start/Stop/Reset; the picture lies in the RTG overlay over its window, keys, mouse and the A3000's joystick port 2 (or a keyboard joystick) go to the Amiga (Left Amiga + Right Amiga, or both mouse buttons held 2 s, gives them back) |
| `amixsoftware/` | AMIX Software Center: both package systems of AMIX as equals, in one list: our IPS `pkg` (sysutils/amix-pkg, publisher amix) and [apkg](https://github.com/amigaux/amix/tree/master/userland/pkgtools) of the amigaux project (installed separately). Each row says where it comes from (Source filter: Both, IPS, apkg); categories, Installed, Updates and In both (the same software from both systems, paired by name); search; details; Install/Update/Remove go to the owning system, Try = its dry run (`pkg -n`, `apkg -n`); Check for Updates and Update All cover both, one after the other, never both at once. Installing what the other system already has shows the overlapping files (IPS manifests against the SVR4 contents file) and offers to keep or remove the other first. 68060-only packages (`org.amix.cpu`) are hidden or greyed out on a 68020/030. Repositories: IPS origins (add, remove, on/off, reachability, refresh) and apkg's `repo=`, `cachedir=`, `nameserver=`; Find on Network asks AMIX Installers serving packages (UDP 8070, and 10.0.2.2 for FS-UAE). Preferences: both systems on/off with their paths, which installs when both offer it, the space kept free, confirmations, dry runs, 68060-only packages, original Amix 2.1 packages; saved for everyone (`/etc/amixsoftware.conf`) or the user (`~/.amixsoftware`). root runs the programs itself, others give the root password once per change to the setuid helper `swrun` (fixed verbs, checked names and URLs, root-owned programs only). `amixsoftware -list` prints the merged table without a window |
| `panel/` | The AMIX control in CDE's front panel, with a subpanel holding both |

The core 1 programs and `armbench` are in
[amix-gate firmware/core1](https://github.com/isoriano1968/amix-gate/tree/main/usr/src/firmware/core1).

## Building

`build.sh` cross-builds them with gcc14-cross-amix against the shared Motif and
X11R6.3 libraries, taking the flags from `tools/amix-pkg-config xm` (and
`amixstat` for amixsysmon: stage library/amixstat into the sysroot first).
Each directory has an `install.sh` to run on the box. Every application has a
control in the AMIX subpanel of the front panel (`panel/amix.fp`): a new one
adds its CONTROL there, and its `install.sh` runs `panel/install.sh`. The
Application Manager entry (`/etc/dt/appconfig/appmanager/C/<group>/<action>`)
shows after the next login, or Application Manager, Reload Applications;
Reload Actions does not gather it.

## AmixCast

On the PC, with VLC and GStreamer's `gst-discoverer-1.0`:

    python3 amixcast/amixcast.py --server http://<DLNA server>:<port>

On the Amiga the PC's address goes in `$HOME/.amixcast/config` ("host port"; an
older `$HOME/.amixcast` file is moved there), or as arguments. Double-click a
folder to open it, a video to play it. In ffplay: Left/Right 10 s, Down/Up 60 s,
1, 2 and h the size, f fullscreen, q quit. ffplay keeps where a video was left
in `$HOME/.amixcast/pos`; played again, amixcast offers to resume there.

The network sets the limit: about 1 Mbit/s on the Z3660's Ethernet under AMIX.
Auto measures it and leaves a margin.
