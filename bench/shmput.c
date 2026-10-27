/* shmput.c -- time XShmPutImage of a 512x320 16-bit image into a window at
 * eight consecutive root x positions.  (2026-10-25)  Settles whether
 * Xzz9000's move16 PutImage path is reached and pays: the segment and the
 * screen rows are 16-byte aligned, so source and screen share their phase
 * only when x % 8 == 0.  (The image cannot start inside the segment instead:
 * AMIX's SHMMAX is 327680, exactly this image.)
 *
 *   shmput [x]        x .. x+7, default 64; override-redirect, no WM moves
 *
 * Build (gcc14-cross-amix): m68k-elf-gcc -O2 -m68060 -o shmput shmput.c
 *   -L<mesa31-amix>/shm-static -lXext -lX11 -lshmsys -lsocket -lnsl
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#define W 512
#define H 320
#define N 30

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char **argv)
{
    Display *dpy = XOpenDisplay(0);
    int x = argc > 1 ? atoi(argv[1]) : 64, scr, p, i;
    XSetWindowAttributes a;
    XShmSegmentInfo si;
    XImage *img;
    Window win;
    GC gc;
    char *base;
    double t0, put;

    if (!dpy) { printf("no display\n"); return 1; }
    if (!XShmQueryExtension(dpy)) { printf("no MIT-SHM\n"); return 1; }
    scr = DefaultScreen(dpy);
    a.override_redirect = True;
    a.background_pixel = BlackPixel(dpy, scr);
    win = XCreateWindow(dpy, RootWindow(dpy, scr), x, 40, W, H, 0, CopyFromParent,
                        InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &a);
    XMapRaised(dpy, win);
    gc = XCreateGC(dpy, win, 0, 0);
    img = XShmCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr),
                          ZPixmap, 0, &si, W, H);
    si.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * H, IPC_CREAT | 0777);
    if (si.shmid < 0) { perror("shmget"); return 1; }
    si.shmaddr = base = img->data = (char *) shmat(si.shmid, 0, 0);
    si.readOnly = False;
    XShmAttach(dpy, &si);
    XSync(dpy, False);
    shmctl(si.shmid, IPC_RMID, 0);
    for (i = 0; i < img->bytes_per_line * H; i++)
        base[i] = (char)(i * 7);
    printf("bpl %d, depth %d\n", img->bytes_per_line, img->depth);

    for (p = 0; p < 8; p++) {
        XMoveWindow(dpy, win, x + p, 40);
        XShmPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H, False);
        XSync(dpy, False);
        t0 = now();
        for (i = 0; i < N; i++)
            XShmPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H, False);
        XSync(dpy, False);
        put = (now() - t0) / N;
        printf("x=%d%s: %6.2f ms per put\n", x + p, (x + p) % 8 == 0 ? " (aligned)" : "", put);
        fflush(stdout);
    }
    t0 = now();
    for (i = 0; i < N; i++)
        XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
    XSync(dpy, False);
    printf("plain XPutImage: %6.2f ms per put\n", (now() - t0) / N);
    XShmDetach(dpy, &si);
    shmdt(base);
    XCloseDisplay(dpy);
    return 0;
}
