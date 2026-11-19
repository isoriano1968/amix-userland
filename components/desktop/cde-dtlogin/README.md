# desktop/cde-dtlogin: how CDE starts on AMIX

The CDE desktop (desktop/cde) on the machine's graphics card, in two ways:

- **From the console:** `startcde` (in `/usr/bin`) starts the X server on
  the card the AMIX Installer chose (`/etc/X11/amix-card`: `cirrus` for the
  Picasso II, `zz9000`, `z3660`) and a CDE session for the user who typed it.
  `RTG_MODE=800x600 startcde` picks a mode where the card has several.
- **Graphical login (dtlogin):** installed, but **off**. As root:

      /usr/dt/bin/dtconfig -e        # on at the next boot (S99dtlogin)
      /etc/init.d/dtlogin start      # and now
      /usr/dt/bin/dtconfig -d        # off again
      /usr/dt/bin/dtconfig -status

  `/etc/dt/nodtlogin` skips the boot start, so a broken X setup cannot lock
  the console out. Before removing this package, run `dtconfig -d`.

| file | what |
|---|---|
| `/usr/bin/startcde` | the console start (`files/startcde`): xinit with CDE's `xinitrc.amix` and `Xamix-dt` |
| `/usr/dt/bin/Xamix-dt` | the X server as dtlogin and startcde start it: memory limits, fonts, the card's mode |
| `/usr/dt/bin/dtconfig` | dtlogin on and off at boot, as on Solaris |
| `/etc/init.d/dtlogin` | the boot script; not linked into `rc2.d` until `dtconfig -e` |
| `/etc/dt/config/Xconfig`, `Xservers` | dtlogin's configuration: desktop/cde's Xconfig, one local server `:0` through `Xamix-dt` |

X server log: `/var/dt/Xamix.log`; session log: `$HOME/.dt/startlog`.
