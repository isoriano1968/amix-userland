# iortcw for AMIX

Return to Castle Wolfenstein single player on an A3000 with a Z3660: the GL
runs on the board's second ARM core through zzgl, the game code as native
modules (`<name>.sp.m68k.so`). `make prep` fetches a pinned iortcw and applies
`patches/`; `files/amix` has the build (`build.py`, `hostbuild.py` for the PC),
the AMIX layer and `wolf.sh`.

The game's data is not here and may not be shared: use your own copy.
`trimpaks.py` reduces its paks to a few levels for the A3000's disk. Start a
level with `+spmap`: `+devmap` is multiplayer, with no AI and no scripts.

iortcw is under the GPL version 3, with the additional terms in
`iortcw.license`; so are the changes here.
