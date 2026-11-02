# GLQuake for AMIX

Quake with two ways to the GL on the Z3660's second ARM core:

| Program | Makefile | GL | timedemo demo1, 866 MHz |
| ------- | -------- | -- | ------- |
| `glquake.zzgl` | `Makefile.amix-zzgl` | `libzzgl.so.1`, the fast path | 320x240 15.4 fps, 640x480 7.9 fps |
| `glquake.m18` | `Makefile.amix-m18` | `libGL.so.18`, Mesa 18.3 on zz3660 | 320x240 2.4 fps |

`glquake.sh` and `glquake18.sh` start them, with sound on HDMI. Both install to
`/opt/quake`. The game's data is not here: use your own, in `/opt/quake/id1`.

Quake is under the GPL version 2.
