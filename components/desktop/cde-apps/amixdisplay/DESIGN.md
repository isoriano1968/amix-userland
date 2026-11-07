# amixdisplay: a CDE "Display" settings app for AMIX

2026-11-07. Status: **pass 1 built and installed on the box; the user
confirmed it works (2026-11-07).** Pass 2 (the kernel step) is not started.
The user agreed to the design on 2026-11-07 ("yes to all", section 9).

This is a screen-mode selector like the Display panels on Linux desktops.
The user picks the resolution and refresh rate of the Xzz9000 server on the
A3000 + Z3660; colour depth is shown, fixed at 16 bit (section 3). The
change is **live** through RandR 1.1. The user decided in the X11R6.7
session to backport RandR into Xzz9000 and to have this panel use it; that
session owns the server patch (`components/x11/xzz9000/local-randr.patch`).
After a change, a dialog asks for 15 seconds whether to keep the new
resolution, and it goes back to the old one otherwise.

The first draft of this design assumed no RandR: every change restarted X
and the confirm dialog ran from Xsetup. With RandR none of that is needed.
That flow is kept in section 8 only for a future depth change, which no
RandR can do.

---

## 1. How the mode is chosen today

| layer | where | what it does |
|---|---|---|
| dtlogin | `/etc/dt/config/Xservers` → `/opt/amix/bin/Xzz9000-dt` (cde `files/dtlogin/`) | `-mode` = `$RTG_MODE`, else **the whole of line 1** of `/etc/dt/config/rtg-mode` (its comment says "first word", but the code passes the whole line unquoted, so a second word would become a stray server argument and the server would exit), else `1920x1080`. The wrapper `exec`s the server, so dtlogin's signals reach the server itself. |
| startx | `/opt/amix/bin/startx-z3 [WxH]` → `startx-rtg` (upstream `amix-z3660rtg/x11`) | `-mode` = first argument, else `$RTG_MODE`, else `1024x768`. It is checked against a hard-coded list: `640x480 800x600 1024x768 1280x720 1280x1024 1920x800 1920x1080 1280x800`. |
| X server | `hw/amix/amixIo.c` `-mode`, `rtg/zz9000/zz9000hw.c` | Has its own table of 8 names (the same as startx-rtg). With no `-mode` its default is `zz9000_modes[5]` = 1920x800, which the Z3660 refuses. Depth is fixed at 16 (`ZZ9000_DEPTH`), and SETMODE always uses `ZZ9K_COLOR_16BIT565`. |
| kernel | amix-gate `io/zz9000` `ZZ9KIOC_SETMODE` (struct `zz9k_mode` {width, height, color_mode, scale_mode}) | Looks up WxH in the board's mode table, giving one firmware id per WxH. Checks that `w*h*bpp` fits `fb_size`, then writes `REG_ZZ_MODE` = id \| colour<<8 \| scale<<12. There is no ioctl that lists the modes. |
| firmware | Z3660 `src/video.c` `preset_video_modes[]`, `rtg/rtg.c` REG_ZZ_MODE | 22 preset timings plus a CUSTOM slot (`ZZ_CUSTOM_VIDMODE` 0x200 / `_DATA` 0x204: any timing, "for debug"). Scale bits 0/1 double pixels horizontally/vertically. There is no EDID: the SiI9022 is set up blind. |

The box was checked read-only on 2026-11-07 through `amix-box`:
`/etc/dt/config/rtg-mode` does not exist, so dtlogin starts 1920x1080. See
section 1.1 for the rest.

The three tables do not agree. The X server and startx-rtg know 1920x800,
which only the old ZZ9000 row has. They do not know 1600x1200, 1920x1200,
1680x1050, 1600x900, 1366x768, 2560x1440, 720x576, 720x480, 640x512 or
640x400, although the kernel and the firmware accept all of them.

### 1.1 Box state (read-only, 2026-11-07)

Kernel `68060-261105-x4`. `/dev/zz9000` is 49,0. `/opt/amix/bin/Xzz9000` is
from 2026-10-26, `Xzz9000-dt` from 10-25, `startx-rtg` from 10-25 and
`startx-z3` from 10-24. There is no `/etc/dt/config/rtg-mode`. The box
installs match the trees described above (startx-z3 calls startx-rtg and then
rtg-kbdback). `zz9k_test /dev/zz9000` reports the following:

```
zz9000: base 0x50000000 board 0x2e00000 fb 0x0+0x2e00000 hw 0xffff fw 0x103 mode 1920x1080 color 1
fbinfo: 1920x1080 depth 16 bpp 2 stride 3840 offset 0x0 size 0x2e00000
fbinfo: masks r 0xf800 g 0x7e0 b 0x1f
```

So X is running at 1920x1080 RGB565, with firmware 1.03 and a 46 MB
framebuffer window. `/var/dt/Xpid` holds **dtlogin's** pid (202), not the
server's (Xzz9000 was 203), so the wrapper writes its own
`/var/dt/Xzz9000.pid`.

**Incident, 2026-11-07:** that check ran `zz9k_test /dev/zz9000` with no
subcommand. It is not a report: after printing it runs the mode test (SETMODE
800x600, colour bars, SETSWITCH RTG, "press return", SETSWITCH CAPTURE). With
ssh's empty stdin it went straight to CAPTURE, and the user's dtlogin screen
vanished while X kept running. Never run zz9k_test on the box for
information; GETINFO/GETFBINFO belong in a read-only tool (`zz9k_mode` with
no mode only shows).

## 2. The modes on this hardware

The source of truth is the kernel's `zz9k_modes_z3660[]`, which is identical
in `usr/sys` (the installed kernel's build tree) and in amix-gate. Behind it
is the firmware table, identical in beta 21, fork 1.0, amix-main and
`core1-loader` 849a94d (the c1fix image on the card). Refresh rates are
computed from the firmware's PLL values (100 MHz × mul / div / div2) and its
htotal/vtotal. They were **not measured**, because nothing was switched on the
box. The SiI9022 gets no EDID, so the monitor itself decides whether it can
show a mode.

| WxH | fw id | pixel clock | refresh | aspect | 16 bpp frame | X server today | note |
|---|---|---|---|---|---|---|---|
| 640x400 | 16 | 25.00 MHz | 59.5 Hz | 16:10 | 0.5 MB | no | |
| 640x480 | 2 | 25.00 MHz | 59.5 Hz | 4:3 | 0.6 MB | yes | |
| 640x512 | 9 | 40.00 MHz | 60.3 Hz | 5:4 | 0.7 MB | no | uses the 800x600 timing with a 640x512 window: probably borders; test before offering |
| 720x480 | 8 | 25.33 MHz | 60.3 Hz | 3:2 | 0.7 MB | no | |
| 720x576 | 6 | 27.11 MHz | **50.2 Hz** | 5:4 | 0.8 MB | no | PAL rate |
| 800x600 | 1 | 40.00 MHz | 60.3 Hz | 4:3 | 1.0 MB | yes | |
| 1024x768 | 3 | 65.00 MHz | 60.0 Hz | 4:3 | 1.6 MB | yes | |
| 1280x720 | 0 | 74.24 MHz | 60.0 Hz | 16:9 | 1.8 MB | yes | |
| 1280x800 | 17 | 66.67 MHz | 56.3 Hz | 16:10 | 2.0 MB | yes | |
| 1280x1024 | 4 | 108.0 MHz | 60.0 Hz | 5:4 | 2.6 MB | yes | |
| 1366x768 | 21 | 72.00 MHz | 60.0 Hz | 16:9 | 2.1 MB | no | 2732-byte pitch, not 16-byte aligned: check move16 paths |
| 1600x900 | 19 | 93.33 MHz | 57.3 Hz | 16:9 | 2.9 MB | no | |
| 1600x1200 | 10 | 161.5 MHz | 60.2 Hz | 4:3 | 3.8 MB | no | |
| 1680x1050 | 20 | 114.0 MHz | 57.4 Hz | 16:10 | 3.5 MB | no | |
| 1920x1080 | 5 | 150.0 MHz | 60.6 Hz | 16:9 | 4.1 MB | yes | today's mode |
| 1920x1080 | **7** | 150.0 MHz | **50.5 Hz** | 16:9 | 4.1 MB | — | firmware has it; the kernel's table cannot select it (one id per WxH) |
| 1920x1200 | 18 | 150.0 MHz | 58.4 Hz | 16:10 | 4.6 MB | no | above `ZZ9K_KMAP_FB` (4 MB): kernel console/VT mirror; see 2.1 |
| 2560x1440 | 11 | 146.4 MHz | **30.0 Hz** | 16:9 | 7.4 MB | no | above the 4 MB KMAP and VT buffer; slow for the 060 |
| 1920x800 | — | | | | | yes | **ZZ9000 only**: Z3660 SETMODE gives EINVAL. Remove it from X/startx |

What this means for the refresh choice:
- **Only 1920x1080 has two rates** (60.6 and 50.5 Hz). Every other size has
  exactly one. The refresh menu therefore exists, but it holds one entry
  except at 1080p.
- 1080p50 matters for video. The memory note "core 1 smoothness" puts 25p
  played at 60 Hz high on the list of judder suspects. At 50.5 Hz a 25p clip
  still repeats a frame about every 2 s, against a 3:2-like cadence at 60 Hz.
  That is better, but not exact. An exact 50.00 Hz (148.5 MHz) needs a new
  firmware preset or the CUSTOM slot; see 2.2.
- Several "60 Hz" presets are really 56–58 Hz (1280x800, 1600x900,
  1680x1050, 1920x1200). The app shows the computed rate, not a nominal 60.

### 2.1 Limits beyond the table

- The kernel's 4 MB windows (`ZZ9K_KMAP_FB`, `ZZ9K_VT_SIZE`) are sized for
  1920x1080x16. For larger frames (1920x1200, 2560x1440) SETMODE succeeds
  (it checks only `fb_size`, 46 MB), but the kernel console mirror and the
  Alt+F VT switch would see only the first 4 MB. The first pass offers these
  two modes marked "experimental", or hides them until the kernel is checked
  (question Q3).
- X server memory: `Xzz9000-dt` sets `ulimit -v 128 MB` and maps the whole
  46 MB board, so the frame size changes nothing there.
- Speed: the 060 draws every non-blitted pixel, so 2560x1440 costs 1.8× the
  pixels of 1080p at 30 Hz.

### 2.2 Modes the firmware could add later (not in the first pass)

The CUSTOM slot (registers 0x200/0x204: hres … div2, then REG_ZZ_MODE with
id 22) takes any timing: exact 1080p50/1080p24/1080p25, or 720p50. It would
need a kernel ioctl `ZZ9KIOC_SETTIMING` (root only) and testing on the user's
monitor. Pixel doubling (scale bits) would give 960x540 on 1080p or 640x360
on 720p, at a quarter of the cost, for games. The kernel's SETMODE would need
the framebuffer size to be output/2. Both are possible later rows in the same
UI.

## 3. Depth

`components/x11/xzz9000/docs/DEPTH-24-32-EVALUATION.md` (the parallel
session, 2026-11-07) concludes: **stay at 16 bpp**. The Z3660's video
formatter drives the SiI9022 over 16 pins (`hdmi_data =
{R[7:3],G[7:2],B[7:3]}`), so a 32 bpp frame shows the same 65536 colours at
twice the cost. The X server is 16-only throughout.

So the depth control offers what the server reports, which today is only
"16 bit (65 536 colours)". The menu is shown, but insensitive. The app does
not hard-code this: it reads the depth list from the running server
(`XListDepths`). An 8-bit PseudoColor
server would appear by itself if one is ever built (kernel and firmware
already accept `ZZ9K_COLOR_8BIT`). It is not planned.

## 4. What pass 1 is (built 2026-11-07)

Everything lives in `components/desktop/cde-apps/amixdisplay/`, is built by
`cde-apps/build.sh` (gcc14-cross-amix, `amix-pkg-config xm xrandr`) and is
installed by its `install.sh`. The X server is not touched here.

| file | on the box | what |
|---|---|---|
| `amixdisplay.c` | `/opt/amix/bin/amixdisplay` | the window, plus `-restore` and `-system` (no window) |
| `setlogin.c` | `/opt/amix/lib/amixdisplay/setlogin`, setuid root | writes the login screen's mode, nothing else |
| `amixdisplay.dt` | `/etc/dt/appconfig/types/C` | action AmixDisplay, label Display |
| `icons/Display.{l,m,t}.pm` (`mkicons.py`) | `/etc/dt/appconfig/icons/C` | a screen with a smaller frame and arrows |
| `0012.amixdisplay` | `/etc/dt/config/Xsession.d` | `amixdisplay -restore` at login, before dtwm |
| (line appended by install.sh) | `/etc/dt/config/Xsetup` | `amixdisplay -system` before each login screen |
| `../panel/amix.fp` | CONTROL DisplayPanel, POSITION_HINTS 8 | in the AMIX subpanel |
| (install.sh) | `Desktop_Apps/AmixDisplay` | App Manager entry |

### 4.1 The window

- The monitor line says that nothing is read from the monitor (no EDID).
- "Now" shows the size, rate and depth from RandR and the X display.
  "Your default" and "Login screen" show what the two files hold.
- Resolution is a list of the server's RandR sizes, smallest first, each with
  its aspect ratio. Refresh is an option menu of the size's RandR rates; it is
  insensitive while a size has only one rate (today every size has one).
- Colours is an option menu of the server's depths (`XListDepths`, without
  depth 1). It is always insensitive: the server has one depth, and a
  different one would need a server restart.
- Two toggles: "Save as my default (applied when I log in)" (on) and "Also use
  it for the login screen (all users)" (off). Then Apply and Close.

### 4.2 Apply

1. Same size and rate as now: only save, as the toggles say.
2. Otherwise `XRRSetScreenConfig(AndRate)`. Then dtwm is restarted without its
   question: `f.restart -noconfirm` is appended to `_DT_WM_REQUEST` on the
   window named by `_MOTIF_WM_INFO`, as libDtSvc's `_DtWmRestartNoConfirm`
   does, so the front panel moves to the new bottom edge. The window is moved
   back on screen if the screen got smaller.
3. A modal dialog, centred on the new screen: "Keep this resolution? Going
   back to the old one in 15 s", with Keep (the default button, Return) and Go
   Back. Closing the dialog also means Go Back.
4. Keep saves the mode. Go Back, or the timeout, sets the old size and rate
   again and restarts dtwm again. So a mode the monitor cannot show undoes
   itself without anyone seeing it.

Only a mode the server is showing gets saved, either unchanged or kept after
the countdown. The login screen's file can therefore never name a size the
installed server does not know; such a size would make `-mode` fail, and
dtlogin would give up on the display.

Without RandR (today's server on the box) the list is the 7 sizes every
Xzz9000 knows (zz9000hw.c before RandR, without the ZZ9000-only 1920x800).
Apply only saves, and says that the mode applies when X starts again.

### 4.3 The files

`$HOME/.amixdisplay` (the user's) and `/etc/dt/config/rtg-mode` (the login
screen's) have the same format:

```
1920x1080
rate 60
# comment
```

Line 1 is `WxH` and nothing else, because Xzz9000-dt passes the whole of
line 1 to `-mode`. Line 2 (`rate N`) is optional and is used through RandR.
Today's server ignores it at start.

- At login: `Xsession.d/0012.amixdisplay` runs `amixdisplay -restore`
  before dtsession starts dtwm. If the user's size is not the current one,
  RandR switches to it.
- Before each login screen: dtlogin runs Xsetup as root and waits for it.
  `amixdisplay -system` switches back to the login screen's mode, because a
  logged-out user's mode would otherwise stay after the server reset.
- At server start: Xzz9000-dt passes line 1 to `-mode`, as before.

### 4.4 setlogin (setuid root)

The user decided on setuid, so that every user can store defaults; the
user's own file needs no privilege. `setlogin WxH [rate]` accepts only the
Z3660 kernel table's sizes that Xzz9000 knows (16 sizes: not 2560x1440, not
1920x800) and rates of 20–120 Hz or 0. It writes `rtg-mode.new` with
`O_EXCL` and mode 0644, fsyncs it, renames it over `rtg-mode`, and notes
the user's name in a comment line. It reads no environment and no other
file. Tested on the PC, with the paths moved: valid sizes are accepted;
1920x800, 2560x1440, rate 5, `1920x1080x`, `x1080`, extra arguments and
long numbers are refused.

### 4.5 Dependencies

- x11/libxrandr and x11/libxrender, shared (`libXrandr.so.2` and
  `libXrender.so.1` are NEEDED). They must be on the box before amixdisplay;
  they come with the RandR session's install.
- An Xzz9000 with RandR for live changes. Without it, the app only saves.
- `cde-apps.manifest`: `require x11/libxrandr` added.

## 5. Pass 2: the kernel step (agreed, separate, not started)

Its own step-by-step kernel boot: a fresh HDF backup first, then the new
kernel at `-m #128`.

- `ZZ9KIOC_GETMODES` (19): rows `{w, h, hz100, id, pixclk, flags}` from the
  driver's table, which gets refresh and pixel-clock columns (section 2's
  values). Readable by anyone, with no side effects.
- A second 1920x1080 row: firmware id 7, 50.5 Hz. `ZZ9KIOC_SETMODEHZ` (20)
  takes `{w, h, hz100, color_mode, scale_mode}` and picks the nearest rate;
  plain SETMODE stays as it is.
- Raise `ZZ9K_KMAP_FB` and `ZZ9K_VT_SIZE` past 4 MB, so that 1920x1200 and
  2560x1440 keep the console mirror and Alt+F switching (Q3).
- Then the X server (the RandR session's patch, or a follow-up to it) gives
  each RandR size the kernel's rates. The panel needs no change: the Refresh
  menu fills itself, and 1080p50 appears.
- The large modes: today RandR leaves out frames taller than 1100 rows,
  because soft3D's back buffer starts at row 1100. Offering 1920x1200 and
  1600x1200 needs soft3D to move its buffer below the frame. The user chose
  "experimental" for them (Q3); that applies once they can be offered at all.

## 6. Testing on the box (agreed: the user at the monitor)

There is no EDID, so only the user can see whether a mode shows. Every step
goes through `amix-box`, never while a player runs, and only when the user
says so:

1. Install libXrandr/libXrender and the RandR Xzz9000 (the RandR session's
   step; it needs one X restart).
2. `install.sh` for amixdisplay, then Restart Workspace Manager.
3. With the user watching: open Display; for each size Apply, check the
   picture, the front panel position and that Go Back/timeout returns; Keep
   one; log out and in (the user's default); set a login screen mode and log
   out (`-system`).
4. 640x512 is not listed (Q5): the app leaves it out, unless the server is
   running in it.

## 7. Packaging (oi-userland rules)

- `components/desktop/cde-apps/amixdisplay/` beside the other CDE apps; our
  own source, so no upstream download and no `patches/`. `build.sh` builds it
  by default, `.gitignore` keeps the two binaries out of git, and the
  licence is `cde-apps.license`.
- `install.sh` installs as `.new` and then `mv`, with no backups (rule).
  It appends the Xsetup line only once, and creates Xsetup as the keymap
  install does when it is missing.

## 8. Not done, and why

- **Depth change:** the HDMI path is RGB565 (section 3). If a deeper server
  ever exists, a depth change needs a server restart. Then the first draft's
  flow applies: write `rtg-mode.new`, kill the server, and in Xzz9000-dt
  promote `.new` to `.try` and drop an unconfirmed `.try` on the next start.
  An Xsetup `amixdisplay -confirm` keeps `.try`, or reverts by killing the
  server. Note: `/var/dt/Xpid` is dtlogin's pid, not the server's.
- **startx-z3 sessions:** `amixdisplay -restore` is not yet run from
  `xinitrc-rtg`, and startx-rtg still checks its own 8-name list. Both
  belong to the RandR/X session's files.
- **mwm/twm/olwm sessions:** only dtwm is restarted. Other window managers
  keep the old geometry until they restart.

## 9. Decisions (user, 2026-11-07: "yes to all")

- Q1: the app first (pass 1), the kernel step separately (pass 2).
- Q2: setuid, so every user can store defaults.
- Q2b: the user's own default applies to their own session. With RandR this
  is a live switch at login (`-restore`), with no second X restart. The login
  screen uses the system mode.
- Q3: large modes shown as "experimental" once the kernel step allows them
  (see 5).
- Q4: mode tests with the user at the monitor, one by one.
- Q5: drop 640x512.
