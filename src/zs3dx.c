/*
 * zs3dx.c -- the client half of the X server's ZZ9K-SOFT3D extension
 * (xzz9000-z3/local-soft3d-swap.patch): the swap of a frame soft3D drew
 * off-screen goes through the server, which copies only the visible parts
 * of the window.  Kept apart from zsoft3d.c for Xlibint.h.
 */
#include <X11/Xlibint.h>

#define X_ZZ9KSwap 0
typedef struct {
    CARD8  reqType;
    CARD8  s3dReqType;
    CARD16 length B16;
    CARD32 window B32;
    INT16  srcX B16, srcY B16;
    INT16  x B16, y B16;
    CARD16 width B16, height B16;
} xZZ9KSwapReq;
#define sz_xZZ9KSwapReq 20

/* the extension's major opcode, 0 if the server has none */
int s3d_xquery(Display *dpy)
{
    int major, ev, err;

    return XQueryExtension(dpy, "ZZ9K-SOFT3D", &major, &ev, &err) ? major : 0;
}

/* copy (sx,sy) w x h of VRAM into the window at (0,0); returns once the
   server has done it, so the next frame cannot overwrite it first */
void s3d_xswap(Display *dpy, int major, Window win, int sx, int sy, int w, int h)
{
    xZZ9KSwapReq *req;

    LockDisplay(dpy);
    GetReq(ZZ9KSwap, req);
    req->reqType = major;
    req->s3dReqType = X_ZZ9KSwap;
    req->window = win;
    req->srcX = sx;
    req->srcY = sy;
    req->x = 0;
    req->y = 0;
    req->width = w;
    req->height = h;
    UnlockDisplay(dpy);
    SyncHandle();
    XSync(dpy, False);
}
