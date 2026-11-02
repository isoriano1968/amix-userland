# ioquake3 for AMIX

Quake III Arena on an A3000 with a Z3660: GL through zzgl on the board's second
ARM core (`ioquake3.zzgl`, linked to `libzzgl.so.1`), or soft3D, with sound through
Paula or HDMI. `make prep` fetches a
pinned ioquake3 and applies `patches/`; `files/amix` has the build (`build.py`,
`hostbuild*.py` for the PC), the AMIX layer and `q3.sh`.

The game's data is not here: use your own copy, or the demo from id Software.

ioquake3 is under the GPL version 2; so are the changes here.
