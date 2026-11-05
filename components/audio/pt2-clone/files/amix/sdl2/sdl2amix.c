/*
 * sdl2amix.c -- sdl2-amix: the part of SDL 2 that pt2-clone uses, for AMIX
 * (SVR4.0, Amiga 3000) on X11R6.3 and the kernel audio driver.
 *
 * Video: one X window; the ARGB8888 "texture" is converted to the X visual
 *   and scaled by an integer factor into a client-side XImage.  Only the
 *   rows and columns that changed since the last present are converted and
 *   sent: through a MIT-SHM image when the server has the extension (a tile
 *   of at most SHMMAX bytes -- 327680 on AMIX, 256 rows of a 640-pixel
 *   16-bit window -- reused band by band), else with XPutImage.
 *   PT2_NOSHM=1 forces XPutImage.
 * Input: X key/button/motion/focus events become SDL events.  Keys are
 *   mapped by position: the AMIX X server's keymap is US-labelled per Amiga
 *   raw key, so the unshifted keysym of a keycode names the physical key.
 * Audio: the Z3660's HDMI when core 1 runs the GL server's program
 *   (zzhdmi_board.c, from library/sdl2: 16-bit stereo at 24 kHz, or 48 with
 *   PT2_HDMI_RATE=48000, written into core 1's stream at the offset of its
 *   time, PT2_HDMI_LEAD_MS ahead, default 400); PT2_HDMI=0 skips it.  Else
 *   /dev/audio with AUDIO_STEREO (a sample-locked Paula pair), signed
 *   8-bit, at the Paula clock / period.  No threads on AMIX: the callback is
 *   run from the program's own calls (polling events, presenting, delays)
 *   whenever the stream head accepts more (non-blocking writes).
 * Threads: SDL_CreateThread runs the function to completion.
 */
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include "zzhdmi_board.h"

/* ---- errors, hints, misc ----------------------------------------------- */

static char errbuf[256] = "";

static int seterr(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsprintf(errbuf, fmt, ap);
	va_end(ap);
	return -1;
}

const char *SDL_GetError(void) { return errbuf; }
SDL_bool SDL_SetHint(const char *n, const char *v) { (void)n; (void)v; return SDL_TRUE; }
int SDL_setenv(const char *n, const char *v, int o) { (void)o; return putenv(strcat(strcat(strcpy(malloc(strlen(n) + strlen(v) + 2), n), "="), v)); }
SDL_bool SDL_HasSSE(void) { return SDL_FALSE; }
SDL_bool SDL_HasSSE2(void) { return SDL_FALSE; }
void SDL_EnableScreenSaver(void) { }
void SDL_GetVersion(SDL_version *v) { SDL_VERSION(v); }

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *msg, SDL_Window *w)
{
	(void)flags; (void)w;
	fprintf(stderr, "%s: %s\n", title, msg);
	return 0;
}

/* ---- threads: synchronous ------------------------------------------------ */

struct SDL_Thread { int rc; };
static struct SDL_Thread thread_dummy;

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
	(void)name;
	thread_dummy.rc = fn(data);
	return &thread_dummy;
}
void SDL_DetachThread(SDL_Thread *t) { (void)t; }
int SDL_SetThreadPriority(SDL_ThreadPriority p) { (void)p; return 0; }

/* ---- time ----------------------------------------------------------------- */

static void audio_pump(void);

/* PT2_FPS=1: frames per second and where the time went, every 5 s */
static Uint64 prof_audio, prof_present;
static Uint64 prof_mix, prof_mixed;	/* callback time, output samples mixed */
static int prof_freq;			/* the audio rate, for them */
static unsigned prof_missed;	/* changed rows nobody marked (found by the full compare) */
static Uint64 now_us(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (Uint64)tv.tv_sec * 1000000 + tv.tv_usec;
}

Uint64 SDL_GetPerformanceFrequency(void) { return 1000000; }

Uint64 SDL_GetPerformanceCounter(void)
{
	static Uint64 lastpump;
	struct timeval tv;
	Uint64 now;

	gettimeofday(&tv, NULL);
	now = (Uint64)tv.tv_sec * 1000000 + tv.tv_usec;
	if (now - lastpump > 8000) {	/* pt2's frame wait polls this */
		lastpump = now;
		audio_pump();
	}
	return now;
}

void SDL_Delay(Uint32 ms)
{
	Uint64 end = SDL_GetPerformanceCounter() + (Uint64)ms * 1000;

	for (;;) {
		Uint64 now = SDL_GetPerformanceCounter();
		Uint64 left;

		if (now >= end)
			break;
		left = end - now;
		usleep(left > 4000 ? 4000 : (unsigned)left);
		audio_pump();
	}
}

/* ---- video ---------------------------------------------------------------- */

static Display *dpy;
static int scr;
static Visual *vis;
static int depth, bpp;			/* bits per pixel of the XImage */
static int rshift, gshift, bshift, rbits, gbits, bbits;
static Atom wm_delete;

struct SDL_Window {
	Window xw;
	GC gc;
	int w, h;
	Uint32 flags;
	XImage *img;
	int full;			/* redraw everything at the next present */
	XImage *shm;			/* MIT-SHM tile, or NULL */
	XShmSegmentInfo si;
	int shm_rows;			/* window rows the tile holds at full width */
	long shm_bytes;			/* its size */
	int shm_busy;			/* sent, maybe not yet read by the server */
};
struct SDL_Renderer { SDL_Window *win; SDL_Texture *tex; };
struct SDL_Texture { int w, h; const Uint32 *pix; Uint32 *prev; };	/* pix: the caller's own buffer */

static SDL_Window *thewin;
static Uint8 *dirty_rows;	/* SDL_AMIX_SetDirtyRows */
static int ndirty_rows;
static int mouse_x, mouse_y;
static Uint32 mouse_buttons;
static Uint16 modstate;

static void mask_shift(unsigned long m, int *shift, int *bits)
{
	*shift = 0;
	*bits = 0;
	if (!m)
		return;
	while (!(m & 1)) { m >>= 1; ++*shift; }
	while (m & 1) { m >>= 1; ++*bits; }
}

int SDL_Init(Uint32 flags)
{
	if ((flags & SDL_INIT_VIDEO) && !dpy) {
		XVisualInfo tmpl, *vi;
		int n;

		dpy = XOpenDisplay(NULL);
		if (!dpy)
			return seterr("cannot open X display \"%s\"", XDisplayName(NULL));
		scr = DefaultScreen(dpy);
		vis = DefaultVisual(dpy, scr);
		depth = DefaultDepth(dpy, scr);
		tmpl.visualid = XVisualIDFromVisual(vis);
		vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
		if (!vi || vi->class != TrueColor || (depth != 15 && depth != 16 && depth != 24))
			return seterr("need a TrueColor visual of depth 15, 16 or 24 (have depth %d)", depth);
		mask_shift(vi->red_mask, &rshift, &rbits);
		mask_shift(vi->green_mask, &gshift, &gbits);
		mask_shift(vi->blue_mask, &bshift, &bbits);
		XFree(vi);
		bpp = depth > 16 ? 32 : 16;
		wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	}
	return 0;
}

static void audio_close(void);

void SDL_Quit(void)
{
	audio_close();
	if (dpy) {
		XCloseDisplay(dpy);
		dpy = NULL;
	}
}

/* PT2_SCALE (default 2): the integer upscale pt2-clone's auto-fit picks */
static int want_scale(void)
{
	const char *e = getenv("PT2_SCALE");
	int s = e ? atoi(e) : 2;
	return s < 1 ? 1 : s > 6 ? 6 : s;
}

int SDL_GetDesktopDisplayMode(int di, SDL_DisplayMode *m)
{
	int s = want_scale();
	(void)di;
	memset(m, 0, sizeof *m);
	/* sized so that pt2's auto-fit (w >= 320*s, h >= (255+64)*s) lands on s */
	m->w = 320 * s + 319;
	m->h = (255 + 64) * s + 318;
	if (dpy) {
		if (m->w > DisplayWidth(dpy, scr)) m->w = DisplayWidth(dpy, scr);
		if (m->h > DisplayHeight(dpy, scr)) m->h = DisplayHeight(dpy, scr);
	}
	m->refresh_rate = 50;		/* no vsync: pt2 times frames itself */
	return 0;
}

int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }

static int shm_failed;

static int shm_error(Display *d, XErrorEvent *e)
{
	(void)d; (void)e;
	shm_failed = 1;
	return 0;
}

/* the MIT-SHM tile: as many full window rows as one segment may hold */
static void shm_init(SDL_Window *win)
{
	int bpl = win->w * (bpp / 8), rows, (*old)(Display *, XErrorEvent *);
	long maxbytes = 327680;		/* AMIX's SHMMAX */
	const char *e = getenv("PT2_NOSHM");

	if ((e && atoi(e) > 0) || !XShmQueryExtension(dpy))
		return;
	rows = (int)(maxbytes / bpl);
	if (rows > win->h)
		rows = win->h;
	if (rows < 1)
		return;
	win->shm = XShmCreateImage(dpy, vis, depth, ZPixmap, NULL, &win->si, win->w, rows);
	if (!win->shm)
		return;
	win->si.shmid = shmget(IPC_PRIVATE, (size_t)win->shm->bytes_per_line * rows, IPC_CREAT | 0600);
	if (win->si.shmid < 0)
		goto fail;
	win->si.shmaddr = win->shm->data = (char *)shmat(win->si.shmid, 0, 0);
	if (win->si.shmaddr == (char *)-1) {
		shmctl(win->si.shmid, IPC_RMID, 0);
		goto fail;
	}
	win->si.readOnly = True;
	shm_failed = 0;
	old = XSetErrorHandler(shm_error);
	XShmAttach(dpy, &win->si);
	XSync(dpy, False);
	XSetErrorHandler(old);
	shmctl(win->si.shmid, IPC_RMID, 0);	/* goes when both have detached */
	if (shm_failed) {
		shmdt(win->si.shmaddr);
		goto fail;
	}
	{
		static const Uint16 one = 1;
		win->shm->byte_order = *(const char *)&one ? LSBFirst : MSBFirst;
	}
	win->shm_rows = rows;
	win->shm_bytes = (long)win->shm->bytes_per_line * rows;
	return;
fail:
	win->shm->data = NULL;
	XDestroyImage(win->shm);
	win->shm = NULL;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	SDL_Window *win;
	XSizeHints hints;
	XSetWindowAttributes a;

	(void)x; (void)y;
	if (!dpy && SDL_Init(SDL_INIT_VIDEO) < 0)
		return NULL;
	win = calloc(1, sizeof *win);
	win->w = w;
	win->h = h;
	a.background_pixel = BlackPixel(dpy, scr);
	a.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask | ExposureMask | FocusChangeMask | StructureNotifyMask;
	win->xw = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0, depth, InputOutput, vis,
	    CWBackPixel | CWEventMask, &a);
	hints.flags = PMinSize | PMaxSize;
	hints.min_width = hints.max_width = w;
	hints.min_height = hints.max_height = h;
	XSetWMNormalHints(dpy, win->xw, &hints);
	XStoreName(dpy, win->xw, title && *title ? title : "pt2-clone");
	XSetWMProtocols(dpy, win->xw, &wm_delete, 1);
	win->gc = XCreateGC(dpy, win->xw, 0, NULL);
	win->img = XCreateImage(dpy, vis, depth, ZPixmap, 0, malloc((size_t)w * h * (bpp / 8)), w, h, bpp, 0);
	{
		/* we write native words: MSBFirst on the 68060, LSBFirst on a PC */
		static const Uint16 one = 1;
		win->img->byte_order = *(const char *)&one ? LSBFirst : MSBFirst;
	}
	shm_init(win);
	win->flags = (flags & SDL_WINDOW_HIDDEN) ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
	win->full = 1;
	if (!(flags & SDL_WINDOW_HIDDEN))
		XMapWindow(dpy, win->xw);
	thewin = win;
	return win;
}

void SDL_DestroyWindow(SDL_Window *w)
{
	if (!w)
		return;
	XDestroyImage(w->img);
	if (w->shm) {
		XShmDetach(dpy, &w->si);
		XSync(dpy, False);
		shmdt(w->si.shmaddr);
		w->shm->data = NULL;
		XDestroyImage(w->shm);
	}
	XFreeGC(dpy, w->gc);
	XDestroyWindow(dpy, w->xw);
	if (thewin == w)
		thewin = NULL;
	free(w);
}

void SDL_ShowWindow(SDL_Window *w) { XMapRaised(dpy, w->xw); XFlush(dpy); }
void SDL_RaiseWindow(SDL_Window *w) { XRaiseWindow(dpy, w->xw); }
void SDL_RestoreWindow(SDL_Window *w) { XMapRaised(dpy, w->xw); }
void SDL_SetWindowTitle(SDL_Window *w, const char *t) { XStoreName(dpy, w->xw, t); }
Uint32 SDL_GetWindowFlags(SDL_Window *w) { return w ? w->flags : 0; }
void SDL_GetWindowSize(SDL_Window *w, Sint32 *x, Sint32 *y) { if (x) *x = w->w; if (y) *y = w->h; }
void SDL_GL_GetDrawableSize(SDL_Window *w, Sint32 *x, Sint32 *y) { SDL_GetWindowSize(w, x, y); }
int SDL_SetWindowFullscreen(SDL_Window *w, Uint32 f) { (void)w; (void)f; return 0; }
SDL_bool SDL_GetWindowWMInfo(SDL_Window *w, SDL_SysWMinfo *i) { (void)w; (void)i; return SDL_FALSE; }

void SDL_GetWindowPosition(SDL_Window *w, Sint32 *x, Sint32 *y)
{
	Window child;
	int rx = 0, ry = 0;	/* XTranslateCoordinates wants int */

	XTranslateCoordinates(dpy, w->xw, RootWindow(dpy, scr), 0, 0, &rx, &ry, &child);
	if (x) *x = rx;
	if (y) *y = ry;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int i, Uint32 f)
{
	SDL_Renderer *r = calloc(1, sizeof *r);
	(void)i; (void)f;
	r->win = w;
	return r;
}
void SDL_DestroyRenderer(SDL_Renderer *r) { free(r); }
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r, SDL_BlendMode m) { (void)r; (void)m; return 0; }
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 a, Uint8 b, Uint8 c, Uint8 d) { (void)r; (void)a; (void)b; (void)c; (void)d; return 0; }
int SDL_RenderClear(SDL_Renderer *r) { (void)r; return 0; }

SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 fmt, int access, int w, int h)
{
	SDL_Texture *t;
	(void)r; (void)access;
	if (fmt != SDL_PIXELFORMAT_ARGB8888) {
		seterr("only ARGB8888 textures");
		return NULL;
	}
	t = calloc(1, sizeof *t);
	t->w = w;
	t->h = h;
	t->pix = NULL;
	t->prev = calloc((size_t)w * h, 4);
	return t;
}
void SDL_DestroyTexture(SDL_Texture *t) { if (t) { free(t->prev); free(t); } }
int SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode m) { (void)t; (void)m; return 0; }

/* No copy: memory is slow here (~17 MB/s), and pt2 updates the whole texture
** from a buffer it keeps until the next frame, so present reads it directly. */
int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rc, const void *pixels, int pitch)
{
	(void)rc;
	if (pitch != t->w * 4)
		return seterr("texture pitch must be its width");
	t->pix = (const Uint32 *)pixels;
	return 0;
}

void SDL_AMIX_SetDirtyRows(Uint8 *rows, int n) { dirty_rows = rows; ndirty_rows = n; }

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *s, const SDL_Rect *d)
{
	(void)s; (void)d;
	r->tex = t;
	return 0;
}

/* convert and scale texture rows y0..y1, columns x0..x1 into img, texture
   column xbase and row ybase at the image's first column and row */
static void blit(XImage *img, SDL_Texture *t, int sc, int x0, int x1, int y0, int y1, int xbase, int ybase)
{
	int y, x, k;

	/* the usual case, RGB565 (AMIX's 16-bit screen) at twice the size:
	   constant shifts, and each doubled pixel stored as one long */
	if (bpp == 16 && sc == 2 && rshift == 11 && gshift == 5 && bshift == 0 &&
	    rbits == 5 && gbits == 6 && bbits == 5) {
		for (y = y0; y <= y1; y++) {
			const Uint32 *src = t->pix + (size_t)y * t->w + x0;
			char *row = img->data + (size_t)(y - ybase) * 2 * img->bytes_per_line + (size_t)(x0 - xbase) * 4;
			Uint32 *d = (Uint32 *)row;
			int n = x1 - x0 + 1;

			while (n-- > 0) {
				Uint32 p = *src++;
				Uint32 v = (p >> 8 & 0xf800) | (p >> 5 & 0x07e0) | (p >> 3 & 0x001f);
				*d++ = v << 16 | v;
			}
			memcpy(row + img->bytes_per_line, row, (size_t)(x1 - x0 + 1) * 4);
		}
		return;
	}
	for (y = y0; y <= y1; y++) {
		const Uint32 *src = t->pix + (size_t)y * t->w;
		char *row = img->data + (size_t)(y - ybase) * sc * img->bytes_per_line - (size_t)xbase * sc * (bpp / 8);

		if (bpp == 16) {
			Uint16 *d = (Uint16 *)row + x0 * sc;
			for (x = x0; x <= x1; x++) {
				Uint32 p = src[x];
				Uint16 v = (Uint16)(((p >> (24 - rbits)) & ((1 << rbits) - 1)) << rshift |
				    ((p >> (16 - gbits)) & ((1 << gbits) - 1)) << gshift |
				    ((p >> (8 - bbits)) & ((1 << bbits) - 1)) << bshift);
				for (k = 0; k < sc; k++)
					*d++ = v;
			}
		} else {
			Uint32 *d = (Uint32 *)row + x0 * sc;
			for (x = x0; x <= x1; x++) {
				Uint32 p = src[x];
				Uint32 v = ((p >> 16) & 255) << rshift | ((p >> 8) & 255) << gshift | (p & 255) << bshift;
				for (k = 0; k < sc; k++)
					*d++ = v;
			}
		}
		for (k = 1; k < sc; k++)
			memcpy(row + k * img->bytes_per_line + x0 * sc * (bpp / 8),
			    row + x0 * sc * (bpp / 8), (size_t)(x1 - x0 + 1) * sc * (bpp / 8));
	}
}

/* texture rows y0..y1, columns x0..x1 to the window */
static void put_band(SDL_Window *w, SDL_Texture *t, int sc, int x0, int x1, int y0, int y1)
{
	int c0, c1, per;

	if (!w->shm) {
		blit(w->img, t, sc, x0, x1, y0, y1, 0, 0);
		XPutImage(dpy, w->xw, w->gc, w->img, x0 * sc, y0 * sc, x0 * sc, y0 * sc,
		    (x1 - x0 + 1) * sc, (y1 - y0 + 1) * sc);
		return;
	}
	/* The server copies straight (its PutImage, move16 on the Z3660) only
	   when the whole image is sent from 0,0; anything else goes through a
	   scratch pixmap.  So the tile is reshaped for every chunk to exactly
	   the band's width and the chunk's rows. */
	{
		int bw = (x1 - x0 + 1) * sc, bpl = bw * (bpp / 8);
		per = (int)(w->shm_bytes / ((long)bpl * sc));	/* texture rows that fit */
		if (per < 1)
			per = 1;
		for (c0 = y0; c0 <= y1; c0 = c1 + 1) {
			c1 = c0 + per - 1;
			if (c1 > y1)
				c1 = y1;
			if (w->shm_busy) {	/* the server may still be reading it */
				XSync(dpy, False);
				w->shm_busy = 0;
			}
			w->shm->width = bw;
			w->shm->height = (c1 - c0 + 1) * sc;
			w->shm->bytes_per_line = bpl;
			blit(w->shm, t, sc, x0, x1, c0, c1, x0, c0);
			XShmPutImage(dpy, w->xw, w->gc, w->shm, 0, 0, x0 * sc, c0 * sc,
			    bw, (c1 - c0 + 1) * sc, False);
			w->shm_busy = 1;
		}
	}
}

void SDL_RenderPresent(SDL_Renderer *r)
{
	SDL_Window *w = r->win;
	SDL_Texture *t = r->tex;
	int sc, y, ry0 = -1, rx0 = 0, rx1 = 0, all;
	Uint64 p0 = now_us();
	static unsigned frameno;

	if (!t || !w || !t->pix)
		return;
	sc = w->w / t->w;
	if (sc < 1)
		sc = 1;
	/* compare every row now and then: a draw that forgot to mark its rows
	** still reaches the screen, within 16 frames */
	all = w->full || !dirty_rows || ndirty_rows < t->h || (++frameno & 15) == 0;
	/* changed rows, merged into bands with the union of their columns */
	for (y = 0; y <= t->h; y++) {
		int x0 = -1, x1 = -1;

		if (y < t->h) {
			const Uint32 *a = t->pix + (size_t)y * t->w;
			Uint32 *b = t->prev + (size_t)y * t->w;
			if (w->full) {
				x0 = 0;
				x1 = t->w - 1;
			} else if (!all && !dirty_rows[y]) {
				x0 = -1;	/* not drawn since the last present */
			} else {
				for (x0 = 0; x0 < t->w && a[x0] == b[x0]; x0++)
					;
				if (x0 < t->w)
					for (x1 = t->w - 1; a[x1] == b[x1]; x1--)
						;
				else
					x0 = -1;
			}
			if (x0 >= 0)	/* remember what is on the screen now */
				memcpy(b + x0, a + x0, (size_t)(x1 - x0 + 1) * 4);
			if (dirty_rows && y < ndirty_rows) {
				if (x0 >= 0 && !w->full && !dirty_rows[y]) {
					prof_missed++;
					if (getenv("PT2_DIRTYDEBUG"))
						fprintf(stderr, "sdl2-amix: unmarked row %d, x %d..%d\n", y, x0, x1);
				}
				dirty_rows[y] = 0;
			}
		}
		if (x0 >= 0) {
			if (ry0 < 0) {
				ry0 = y;
				rx0 = x0;
				rx1 = x1;
			} else {
				if (x0 < rx0) rx0 = x0;
				if (x1 > rx1) rx1 = x1;
			}
		} else if (ry0 >= 0) {
			put_band(w, t, sc, rx0, rx1, ry0, y - 1);
			ry0 = -1;
		}
	}
	w->full = 0;
	XFlush(dpy);
	prof_present += now_us() - p0;
	audio_pump();
	if (getenv("PT2_FPS")) {
		static Uint64 t0;
		static int frames;
		Uint64 now = now_us();
		if (!t0)
			t0 = now;
		frames++;
		if (now - t0 >= 5000000) {
			double tot = (double)(now - t0);
			fprintf(stderr, "sdl2-amix: %.1f fps; audio %.0f%%, present %.0f%%, program %.0f%%%s\n",
			    frames * 1e6 / tot, prof_audio * 100 / tot, prof_present * 100 / tot,
			    (tot - prof_audio - prof_present) * 100 / tot, prof_missed ? " (unmarked rows!)" : "");
			if (prof_mixed)
				fprintf(stderr, "sdl2-amix: mixing %.1f us per output sample, %.0f%% of real time; %.0f%% of the output rate mixed\n",
				    (double)prof_mix / prof_mixed, (double)prof_mix / prof_mixed * prof_freq / 1e4,
				    prof_mixed * 1e8 / ((double)prof_freq * tot));
			prof_mix = prof_mixed = 0;
			if (prof_missed)
				fprintf(stderr, "sdl2-amix: %u changed rows were not marked dirty\n", prof_missed);
			prof_missed = 0;
			t0 = now;
			frames = 0;
			prof_audio = prof_present = 0;
		}
	}
}

/* Surfaces exist only for pt2's "hardware" mouse pointer: SDL_CreateColorCursor
** turns one into an X cursor, which the X server moves by itself -- a pointer
** drawn into the picture could only move as fast as frames reach the screen.
** X core cursors have two colours: the darker and lighter halves of the image. */
typedef struct { SDL_Surface s; Uint32 key; int haskey; } Surface;
static SDL_PixelFormat fmt32 = { SDL_PIXELFORMAT_ARGB8888, 32, 4 };
struct SDL_Cursor { Cursor xc; };
static SDL_Cursor default_cursor;	/* xc 0: the window's parent's cursor */
static SDL_Cursor *cur_cursor;

SDL_Surface *SDL_CreateRGBSurface(Uint32 f, int w, int h, int d, Uint32 a, Uint32 b, Uint32 c, Uint32 e)
{
	Surface *s;
	(void)f; (void)a; (void)b; (void)c; (void)e;
	if (d != 32) {
		seterr("only 32-bit surfaces");
		return NULL;
	}
	s = calloc(1, sizeof *s);
	s->s.format = &fmt32;
	s->s.w = w;
	s->s.h = h;
	s->s.pitch = w * 4;
	s->s.pixels = calloc((size_t)w * h, 4);
	return &s->s;
}
void SDL_FreeSurface(SDL_Surface *s) { if (s) { free(s->pixels); free(s); } }
int SDL_LockSurface(SDL_Surface *s) { (void)s; return 0; }
void SDL_UnlockSurface(SDL_Surface *s) { (void)s; }
Uint32 SDL_MapRGB(const SDL_PixelFormat *f, Uint8 r, Uint8 g, Uint8 b) { (void)f; return (Uint32)r << 16 | g << 8 | b; }
int SDL_SetColorKey(SDL_Surface *s, int f, Uint32 k) { ((Surface *)s)->key = k; ((Surface *)s)->haskey = f; return 0; }
int SDL_SetSurfaceRLE(SDL_Surface *s, int f) { (void)s; (void)f; return 0; }
int SDL_SetSurfaceBlendMode(SDL_Surface *s, SDL_BlendMode m) { (void)s; (void)m; return 0; }

SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *surf, int hx, int hy)
{
	Surface *s = (Surface *)surf;
	int w = surf->w, h = surf->h, bpl = (w + 7) / 8, x, y, n = 0, ndark = 0, nlight = 0;
	unsigned long sum = 0, dark[3] = { 0, 0, 0 }, light[3] = { 0, 0, 0 };
	unsigned long thresh;
	char *src, *mask;
	Pixmap ps, pm;
	XColor fg, bg;
	SDL_Cursor *c;

	if (!dpy || !thewin)
		return NULL;
	src = calloc((size_t)bpl * h, 1);
	mask = calloc((size_t)bpl * h, 1);
	for (y = 0; y < h; y++)		/* mean brightness of the opaque pixels */
		for (x = 0; x < w; x++) {
			Uint32 p = ((Uint32 *)surf->pixels)[y * w + x];
			if (s->haskey && p == s->key)
				continue;
			sum += ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255);
			n++;
		}
	thresh = n ? sum / n : 0;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			Uint32 p = ((Uint32 *)surf->pixels)[y * w + x];
			unsigned long l = ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255);
			if (s->haskey && p == s->key)
				continue;
			mask[y * bpl + x / 8] |= 1 << (x & 7);	/* XBM: LSB first */
			if (l <= thresh) {
				src[y * bpl + x / 8] |= 1 << (x & 7);	/* foreground: the dark half */
				dark[0] += (p >> 16) & 255; dark[1] += (p >> 8) & 255; dark[2] += p & 255;
				ndark++;
			} else {
				light[0] += (p >> 16) & 255; light[1] += (p >> 8) & 255; light[2] += p & 255;
				nlight++;
			}
		}
	memset(&fg, 0, sizeof fg);
	memset(&bg, 0, sizeof bg);
	if (ndark) { fg.red = dark[0] / ndark * 257; fg.green = dark[1] / ndark * 257; fg.blue = dark[2] / ndark * 257; }
	if (nlight) { bg.red = light[0] / nlight * 257; bg.green = light[1] / nlight * 257; bg.blue = light[2] / nlight * 257; }
	else bg.red = bg.green = bg.blue = 0xffff;
	ps = XCreateBitmapFromData(dpy, thewin->xw, src, w, h);
	pm = XCreateBitmapFromData(dpy, thewin->xw, mask, w, h);
	c = calloc(1, sizeof *c);
	c->xc = XCreatePixmapCursor(dpy, ps, pm, &fg, &bg, hx, hy);
	XFreePixmap(dpy, ps);
	XFreePixmap(dpy, pm);
	free(src);
	free(mask);
	return c;
}

SDL_Cursor *SDL_GetDefaultCursor(void) { return &default_cursor; }

void SDL_SetCursor(SDL_Cursor *c)
{
	if (!c)			/* SDL: "redraw" -- nothing to do for an X cursor */
		return;
	cur_cursor = c;
	if (thewin) {
		if (c->xc)
			XDefineCursor(dpy, thewin->xw, c->xc);
		else
			XUndefineCursor(dpy, thewin->xw);
	}
}

void SDL_FreeCursor(SDL_Cursor *c)
{
	if (c && c != &default_cursor) {
		if (cur_cursor == c)
			cur_cursor = NULL;
		XFreeCursor(dpy, c->xc);
		free(c);
	}
}

int SDL_ShowCursor(int toggle)
{
	static Cursor blank;
	static int shown = 1;

	if (toggle < 0 || !thewin)
		return shown;
	if (!toggle && !blank) {
		static char zero[8];
		Pixmap pm = XCreateBitmapFromData(dpy, thewin->xw, zero, 8, 8);
		XColor c;
		memset(&c, 0, sizeof c);
		blank = XCreatePixmapCursor(dpy, pm, pm, &c, &c, 0, 0);
		XFreePixmap(dpy, pm);
	}
	if (toggle) {
		if (cur_cursor && cur_cursor->xc)
			XDefineCursor(dpy, thewin->xw, cur_cursor->xc);
		else
			XUndefineCursor(dpy, thewin->xw);
	} else
		XDefineCursor(dpy, thewin->xw, blank);
	shown = toggle != 0;
	return shown;
}

/* ---- input ---------------------------------------------------------------- */

#define EVQ 64
static SDL_Event evq[EVQ];
static int evhead, evtail;
static int textinput;

static void push(const SDL_Event *e)
{
	int n = (evtail + 1) % EVQ;
	if (n != evhead) {
		evq[evtail] = *e;
		evtail = n;
	}
}
int SDL_PushEvent(SDL_Event *e) { push(e); return 1; }
Uint8 SDL_EventState(Uint32 t, int s) { (void)t; (void)s; return SDL_ENABLE; }
void SDL_StartTextInput(void) { textinput = 1; }
void SDL_StopTextInput(void) { textinput = 0; }
SDL_Keymod SDL_GetModState(void) { return (SDL_Keymod)modstate; }

static SDL_Scancode keysym_scancode(KeySym k)
{
	if (k >= XK_a && k <= XK_z) return (SDL_Scancode)(SDL_SCANCODE_A + (k - XK_a));
	if (k >= XK_A && k <= XK_Z) return (SDL_Scancode)(SDL_SCANCODE_A + (k - XK_A));
	if (k >= XK_1 && k <= XK_9) return (SDL_Scancode)(SDL_SCANCODE_1 + (k - XK_1));
	if (k >= XK_F1 && k <= XK_F12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (k - XK_F1));
	if (k >= XK_KP_1 && k <= XK_KP_9) return (SDL_Scancode)(SDL_SCANCODE_KP_1 + (k - XK_KP_1));
	switch (k) {
	case XK_0: return SDL_SCANCODE_0;
	case XK_Return: return SDL_SCANCODE_RETURN;
	case XK_Escape: return SDL_SCANCODE_ESCAPE;
	case XK_BackSpace: return SDL_SCANCODE_BACKSPACE;
	case XK_Tab: return SDL_SCANCODE_TAB;
	case XK_space: return SDL_SCANCODE_SPACE;
	case XK_minus: return SDL_SCANCODE_MINUS;
	case XK_equal: return SDL_SCANCODE_EQUALS;
	case XK_bracketleft: return SDL_SCANCODE_LEFTBRACKET;
	case XK_bracketright: return SDL_SCANCODE_RIGHTBRACKET;
	case XK_backslash: return SDL_SCANCODE_BACKSLASH;
	case XK_semicolon: return SDL_SCANCODE_SEMICOLON;
	case XK_apostrophe: return SDL_SCANCODE_APOSTROPHE;
	case XK_grave: return SDL_SCANCODE_GRAVE;
	case XK_comma: return SDL_SCANCODE_COMMA;
	case XK_period: return SDL_SCANCODE_PERIOD;
	case XK_slash: return SDL_SCANCODE_SLASH;
	case XK_Caps_Lock: return SDL_SCANCODE_CAPSLOCK;
	case XK_Insert: return SDL_SCANCODE_INSERT;
	case XK_Home: return SDL_SCANCODE_HOME;
	case XK_Prior: return SDL_SCANCODE_PAGEUP;
	case XK_Delete: return SDL_SCANCODE_DELETE;
	case XK_End: return SDL_SCANCODE_END;
	case XK_Next: return SDL_SCANCODE_PAGEDOWN;
	case XK_Right: return SDL_SCANCODE_RIGHT;
	case XK_Left: return SDL_SCANCODE_LEFT;
	case XK_Down: return SDL_SCANCODE_DOWN;
	case XK_Up: return SDL_SCANCODE_UP;
	case XK_Num_Lock: return SDL_SCANCODE_NUMLOCKCLEAR;
	case XK_KP_Divide: return SDL_SCANCODE_KP_DIVIDE;
	case XK_KP_Multiply: return SDL_SCANCODE_KP_MULTIPLY;
	case XK_KP_Subtract: return SDL_SCANCODE_KP_MINUS;
	case XK_KP_Add: return SDL_SCANCODE_KP_PLUS;
	case XK_KP_Enter: return SDL_SCANCODE_KP_ENTER;
	case XK_KP_0: return SDL_SCANCODE_KP_0;
	case XK_KP_Decimal: return SDL_SCANCODE_KP_PERIOD;
	case XK_less: return SDL_SCANCODE_NONUSBACKSLASH;
	case XK_Help: return SDL_SCANCODE_F11;	/* no F11 on the Amiga */
	case XK_Control_L: return SDL_SCANCODE_LCTRL;
	case XK_Control_R: return SDL_SCANCODE_RCTRL;
	case XK_Shift_L: return SDL_SCANCODE_LSHIFT;
	case XK_Shift_R: return SDL_SCANCODE_RSHIFT;
	case XK_Alt_L: return SDL_SCANCODE_LALT;
	case XK_Alt_R: case XK_Mode_switch: return SDL_SCANCODE_RALT;
	case XK_Meta_L: case XK_Super_L: return SDL_SCANCODE_LGUI;	/* left Amiga */
	case XK_Meta_R: case XK_Super_R: return SDL_SCANCODE_RGUI;	/* right Amiga */
	default: return SDL_SCANCODE_UNKNOWN;
	}
}

static Uint16 scancode_mod(SDL_Scancode s)
{
	switch (s) {
	case SDL_SCANCODE_LSHIFT: return KMOD_LSHIFT;
	case SDL_SCANCODE_RSHIFT: return KMOD_RSHIFT;
	case SDL_SCANCODE_LCTRL: return KMOD_LCTRL;
	case SDL_SCANCODE_RCTRL: return KMOD_RCTRL;
	case SDL_SCANCODE_LALT: return KMOD_LALT;
	case SDL_SCANCODE_RALT: return KMOD_RALT;
	case SDL_SCANCODE_LGUI: return KMOD_LGUI;
	case SDL_SCANCODE_RGUI: return KMOD_RGUI;
	default: return 0;
	}
}

static void x_events(void)
{
	SDL_Event e;
	XEvent xe;

	if (!dpy)
		return;
	while (XPending(dpy)) {
		XNextEvent(dpy, &xe);
		memset(&e, 0, sizeof e);
		switch (xe.type) {
		case KeyPress:
		case KeyRelease: {
			KeySym pos = XKeycodeToKeysym(dpy, xe.xkey.keycode, 0), ks;
			char txt[8];
			int n;

			/* X auto-repeat sends release+press pairs: pt2 repeats keys itself */
			if (xe.type == KeyRelease && XPending(dpy)) {
				XEvent nx;
				XPeekEvent(dpy, &nx);
				if (nx.type == KeyPress && nx.xkey.keycode == xe.xkey.keycode && nx.xkey.time == xe.xkey.time) {
					XNextEvent(dpy, &nx);
					break;
				}
			}
			e.key.keysym.scancode = keysym_scancode(pos);
			n = XLookupString(&xe.xkey, txt, sizeof txt - 1, &ks, NULL);
			txt[n > 0 ? n : 0] = 0;
			if (pos >= XK_A && pos <= XK_Z)
				e.key.keysym.sym = (SDL_Keycode)(pos - XK_A + 'a');
			else if (pos >= 0x20 && pos < 0x7f)
				e.key.keysym.sym = (SDL_Keycode)pos;
			else if (pos == XK_Return)
				e.key.keysym.sym = SDLK_RETURN;
			else if (pos == XK_Escape)
				e.key.keysym.sym = SDLK_ESCAPE;
			else
				e.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(e.key.keysym.scancode);
			if (xe.type == KeyPress)
				modstate |= scancode_mod(e.key.keysym.scancode);
			else
				modstate &= ~scancode_mod(e.key.keysym.scancode);
			modstate = (Uint16)((modstate & ~KMOD_CAPS) | ((xe.xkey.state & LockMask) ? KMOD_CAPS : 0));
			e.key.keysym.mod = modstate;
			e.type = xe.type == KeyPress ? SDL_KEYDOWN : SDL_KEYUP;
			push(&e);
			if (xe.type == KeyPress && textinput && n == 1 && (unsigned char)txt[0] >= 0x20 && txt[0] != 0x7f) {
				memset(&e, 0, sizeof e);
				e.type = SDL_TEXTINPUT;
				e.text.text[0] = txt[0];
				push(&e);
			}
			break;
		}
		case ButtonPress:
		case ButtonRelease:
			mouse_x = xe.xbutton.x;
			mouse_y = xe.xbutton.y;
			if (xe.xbutton.button == Button4 || xe.xbutton.button == Button5) {
				if (xe.type == ButtonPress) {
					e.type = SDL_MOUSEWHEEL;
					e.wheel.y = xe.xbutton.button == Button4 ? 1 : -1;
					push(&e);
				}
				break;
			}
			e.button.button = xe.xbutton.button == Button1 ? SDL_BUTTON_LEFT :
			    xe.xbutton.button == Button3 ? SDL_BUTTON_RIGHT : SDL_BUTTON_MIDDLE;
			if (xe.type == ButtonPress)
				mouse_buttons |= SDL_BUTTON(e.button.button);
			else
				mouse_buttons &= ~SDL_BUTTON(e.button.button);
			e.type = xe.type == ButtonPress ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
			e.button.x = mouse_x;
			e.button.y = mouse_y;
			push(&e);
			break;
		case MotionNotify:
			mouse_x = xe.xmotion.x;
			mouse_y = xe.xmotion.y;
			e.type = SDL_MOUSEMOTION;
			e.motion.x = mouse_x;
			e.motion.y = mouse_y;
			push(&e);
			break;
		case FocusIn:
		case FocusOut:
			if (thewin) {
				if (xe.type == FocusIn)
					thewin->flags |= SDL_WINDOW_INPUT_FOCUS;
				else
					thewin->flags &= ~SDL_WINDOW_INPUT_FOCUS;
			}
			e.type = SDL_WINDOWEVENT;
			e.window.event = xe.type == FocusIn ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
			push(&e);
			modstate &= KMOD_CAPS;	/* releases we will not see */
			break;
		case MapNotify:
		case UnmapNotify:
			if (thewin) {
				thewin->flags &= ~(SDL_WINDOW_SHOWN | SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED);
				thewin->flags |= xe.type == MapNotify ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED;
				thewin->full = 1;
			}
			e.type = SDL_WINDOWEVENT;
			e.window.event = xe.type == MapNotify ? SDL_WINDOWEVENT_SHOWN : SDL_WINDOWEVENT_HIDDEN;
			push(&e);
			break;
		case Expose:
			if (thewin)
				thewin->full = 1;
			break;
		case ClientMessage:
			if ((Atom)xe.xclient.data.l[0] == wm_delete) {
				e.type = SDL_QUIT;
				push(&e);
			}
			break;
		}
	}
}

int SDL_PollEvent(SDL_Event *e)
{
	audio_pump();
	if (evhead == evtail)
		x_events();
	if (evhead == evtail)
		return 0;
	if (getenv("PT2_EVDEBUG"))
		fprintf(stderr, "sdl2-amix: event 0x%x key %d sym %d button %d x %d y %d\n", (unsigned)evq[evhead].type,
		    evq[evhead].key.keysym.scancode, (int)evq[evhead].key.keysym.sym, evq[evhead].button.button,
		    (int)evq[evhead].button.x, (int)evq[evhead].button.y);
	if (e)
		*e = evq[evhead];
	evhead = (evhead + 1) % EVQ;
	return 1;
}

/* The pointer as it is NOW (XQueryPointer), as SDL does: pt2 reads it at the
** start of a frame, before that frame's events, so the position of the last
** motion event seen would be a frame late -- a click arriving together with
** its motion landed where the pointer had been. */
static Uint32 query_pointer(int *wx, int *wy, int *rx, int *ry)
{
	Window root, child;
	unsigned int mask;
	Uint32 b = 0;

	*wx = mouse_x;
	*wy = mouse_y;
	*rx = *ry = 0;
	if (!dpy || !thewin)
		return mouse_buttons;
	if (!XQueryPointer(dpy, thewin->xw, &root, &child, rx, ry, wx, wy, &mask))
		return mouse_buttons;
	mouse_x = *wx;
	mouse_y = *wy;
	if (mask & Button1Mask) b |= SDL_BUTTON(SDL_BUTTON_LEFT);
	if (mask & Button2Mask) b |= SDL_BUTTON(SDL_BUTTON_MIDDLE);
	if (mask & Button3Mask) b |= SDL_BUTTON(SDL_BUTTON_RIGHT);
	return b;
}

Uint32 SDL_GetMouseState(Sint32 *x, Sint32 *y)
{
	int wx, wy, rx, ry;
	Uint32 b = query_pointer(&wx, &wy, &rx, &ry);

	if (x) *x = wx;
	if (y) *y = wy;
	return b;
}

Uint32 SDL_GetGlobalMouseState(Sint32 *x, Sint32 *y)
{
	int wx, wy, rx, ry;
	Uint32 b = query_pointer(&wx, &wy, &rx, &ry);

	if (x) *x = rx;
	if (y) *y = ry;
	return b;
}

void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y)
{
	if (!w)
		w = thewin;
	if (w) {
		XWarpPointer(dpy, None, w->xw, 0, 0, 0, 0, x, y);
		mouse_x = x;
		mouse_y = y;
	}
}

/* ---- audio ---------------------------------------------------------------- */

/* the AMIX audio driver's ioctls (usr/sys/amiga/driver/audio.h + port) */
#define AUIOC        ('U' << 8)
#define AUDIO_VOLUME (AUIOC | 1)
#define AUDIO_PERIOD (AUIOC | 2)
#define AUDIO_STEREO (AUIOC | 3)
#define PAULA_PAL_CLOCK 3546895UL

static int afd = -1, apaused = 1, alocked, astereo;
static int anull;			/* no /dev/audio (PT2_NULLAUDIO=1): consume in real time */
static int anull_forced;		/* SDL_AMIX_SetNullAudio(1): the program drives the sound itself */
/* before SDL_OpenAudioDevice: no /dev/audio; the callback still runs, paced
** by the clock (a program playing through /dev/paula, which excludes
** /dev/audio, keeps its replayer timing that way) */
void SDL_AMIX_SetNullAudio(int on) { anull_forced = on; }
static Uint64 anull_start, anull_done;
static SDL_AudioSpec aspec;
static Sint16 *abuf16;			/* one callback's worth */
static signed char *abuf8;		/* converted, waiting to be written */
static int a8len, a8off;
static Uint64 a_t0, a_frames;	/* frames produced since a_t0 */
#define AUDIO_LEAD_US 200000	/* produce at most this far ahead of real time */
#define AUDIO_BUDGET_US 12000	/* and spend at most this long per pump */

/* HDMI: core 1 plays the stream; times are stream bytes (frames * 4) */
static int ahdmi;			/* open on the HDMI */
static int hdmi_probed = -1;		/* zzhdmi_present(), asked once */
static unsigned long long h_mixed;	/* written up to */
static unsigned long h_lead, h_safe;	/* kept ahead; behind this core 1 has read past */
static unsigned h_under;		/* times it fell behind */

static int hdmi_wanted(void)
{
	const char *e = getenv("PT2_HDMI");

	if (anull_forced || getenv("PT2_NULLAUDIO") || (e && atoi(e) == 0))
		return 0;
	if (hdmi_probed < 0)
		hdmi_probed = zzhdmi_present();
	return hdmi_probed;
}

/* sdl2-amix extension: the sound will go to the HDMI (so not to /dev/paula) */
int SDL_AMIX_HdmiAudio(void) { return hdmi_wanted(); }

static int hdmi_open(const SDL_AudioSpec *want)
{
	const char *r = getenv("PT2_HDMI_RATE"), *l = getenv("PT2_HDMI_LEAD_MS");
	const int div = r && atoi(r) >= 36000 ? 1 : 2;
	const unsigned long bpms = 48000UL / div * 4 / 1000;
	unsigned long lead_ms = l ? (unsigned long)atol(l) : 400;

	aspec = *want;
	aspec.freq = 48000 / div;
	aspec.silence = 0;
	aspec.size = (Uint32)aspec.samples * 4;
	if (lead_ms < ZZHDMI_CORE1_LEAD_MS + 80)
		lead_ms = ZZHDMI_CORE1_LEAD_MS + 80;
	h_lead = bpms * lead_ms;
	if (h_lead + aspec.size > ZZHDMI_RING_BYTES - bpms * 50)
		h_lead = ZZHDMI_RING_BYTES - bpms * 50 - aspec.size;
	h_safe = bpms * (ZZHDMI_CORE1_LEAD_MS + 30);
	if (zzhdmi_open(div, h_lead + aspec.size) < 0)
		return -1;
	h_mixed = h_safe;
	h_under = 0;
	abuf16 = malloc(aspec.size);
	ahdmi = 1;
	apaused = 1;
	fprintf(stderr, "sdl2-amix: sound on the HDMI through core 1, %d Hz (PT2_HDMI=0: Paula)\n", aspec.freq);
	return 0;
}

/* the HDMI's share of audio_pump: mix while less than h_lead is written ahead */
static void hdmi_pump(Uint64 pstart)
{
	unsigned long long now = zzhdmi_pos();

	if (h_mixed < now + h_safe) {
		/* behind core 1 (a long frame, a module loading): on from the
		** present, over silence -- the whole ring, it is a lap or more old */
		zzhdmi_silence(0, ZZHDMI_RING_BYTES);
		h_mixed = (now + h_lead) & ~3ULL;
		h_under++;
		if (getenv("PT2_FPS"))
			fprintf(stderr, "sdl2-amix: HDMI sound fell behind (%u)\n", h_under);
		return;
	}
	while (h_mixed - now < h_lead && now_us() - pstart <= AUDIO_BUDGET_US) {
		Uint64 m0 = now_us();

		aspec.callback(aspec.userdata, (Uint8 *)abuf16, (int)aspec.size);
		prof_mix += now_us() - m0;
		prof_mixed += aspec.samples;
		prof_freq = aspec.freq;
		zzhdmi_write(h_mixed, abuf16, aspec.size);
		h_mixed += aspec.size;
		now = zzhdmi_pos();
	}
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int cap, const SDL_AudioSpec *want, SDL_AudioSpec *have, int allowed)
{
	int period, vol = 63, one = 1;
	const char *e = getenv("PT2_PAULA_PERIOD");

	(void)dev; (void)cap; (void)allowed;
	if (afd >= 0 || ahdmi) {
		seterr("audio already open");
		return 0;
	}
	if (want->format != AUDIO_S16 || want->channels != 2) {
		seterr("only signed 16-bit stereo");
		return 0;
	}
	if (hdmi_wanted() && hdmi_open(want) == 0) {
		if (have)
			*have = aspec;
		return 1;
	}
	afd = (anull_forced || getenv("PT2_NULLAUDIO")) ? -1 : open("/dev/audio", O_WRONLY | O_NDELAY);
	if (afd < 0 && (anull_forced || getenv("PT2_NULLAUDIO"))) {
		/* testing without sound: the callback still runs at the real rate */
		anull = 1;
		afd = open("/dev/null", O_WRONLY);
	}
	if (afd < 0) {
		seterr("/dev/audio: %s", strerror(errno));
		return 0;
	}
	astereo = anull || ioctl(afd, AUDIO_STEREO, &one) == 0;	/* else: mixed down to mono */
	period = e ? atoi(e) : 128;	/* 27710 Hz; 124 is Paula's minimum */
	if (period < 124)
		period = 124;
	if (!anull && (ioctl(afd, AUDIO_PERIOD, &period) < 0 || ioctl(afd, AUDIO_VOLUME, &vol) < 0)) {
		seterr("/dev/audio ioctl: %s", strerror(errno));
		close(afd);
		afd = -1;
		return 0;
	}
	aspec = *want;
	aspec.freq = (int)(PAULA_PAL_CLOCK / period);
	aspec.silence = 0;
	aspec.size = (Uint32)aspec.samples * 4;
	abuf16 = malloc(aspec.size);
	abuf8 = malloc(aspec.samples * 2);
	a8len = a8off = 0;
	apaused = 1;
	if (have)
		*have = aspec;
	return 1;
}

static void audio_close(void)
{
	if (ahdmi) {
		zzhdmi_close();
		ahdmi = 0;
	}
	if (afd >= 0) {
		close(afd);
		afd = -1;
	}
	free(abuf16);
	free(abuf8);
	abuf16 = NULL;
	abuf8 = NULL;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID d) { (void)d; audio_close(); }
void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int p)
{
	(void)d;
	if (ahdmi && apaused && !p)
		h_mixed = (zzhdmi_pos() + h_safe) & ~3ULL;	/* on from now, just ahead of core 1 */
	apaused = p;
	if (!p)
		audio_pump();
}
void SDL_LockAudioDevice(SDL_AudioDeviceID d) { (void)d; alocked++; }
void SDL_UnlockAudioDevice(SDL_AudioDeviceID d) { (void)d; if (alocked) alocked--; }
int SDL_GetNumAudioDrivers(void) { return 1; }
const char *SDL_GetAudioDriver(int i) { (void)i; return "amix"; }
int SDL_GetNumAudioDevices(int c) { (void)c; return 1; }
const char *SDL_GetAudioDeviceName(int i, int c) { (void)i; (void)c; return ahdmi ? "Z3660 HDMI (core 1)" : "Paula (/dev/audio)"; }

/* run the callback whenever the stream head takes more; never blocks */
static void audio_pump(void)
{
	static int inpump;
	int i, n;

	Uint64 pstart;

	if ((afd < 0 && !ahdmi) || apaused || alocked || inpump)
		return;
	inpump = 1;
	pstart = now_us();
	if (ahdmi) {
		hdmi_pump(pstart);
		prof_audio += now_us() - pstart;
		inpump = 0;
		return;
	}
	if (anull) {
		struct timeval tv;
		Uint64 now, due;

		gettimeofday(&tv, NULL);
		now = (Uint64)tv.tv_sec * 1000000 + tv.tv_usec;
		if (!anull_start)
			anull_start = now;
		due = (now - anull_start) * aspec.freq / 1000000 + aspec.samples * 2;	/* a little ahead */
		while (anull_done < due) {
			aspec.callback(aspec.userdata, (Uint8 *)abuf16, (int)aspec.size);
			anull_done += aspec.samples;
		}
		prof_audio += now_us() - pstart;
		inpump = 0;
		return;
	}
	for (;;) {
		if (a8off >= a8len) {
			/* Never more than AUDIO_LEAD_US ahead of real time: if mixing is
			** slower than playback, the sound stutters but the program still
			** gets to draw and to read the keyboard. */
			Uint64 now = now_us();
			if (!a_t0 || now - a_t0 > 100000 + (a_frames * 1000000 / aspec.freq)) {
				a_t0 = now;	/* (re)start, also after an underrun */
				a_frames = 0;
			}
			if (a_frames * 1000000 / aspec.freq > now - a_t0 + AUDIO_LEAD_US)
				break;
			/* If mixing is slower than playback, never let it take the
			** whole frame: the sound breaks up, but the program still draws
			** and reads the mouse and keyboard (Stop must work). */
			if (now - pstart > AUDIO_BUDGET_US)
				break;
			{
				Uint64 m0 = now_us();
				aspec.callback(aspec.userdata, (Uint8 *)abuf16, (int)aspec.size);
				prof_mix += now_us() - m0;
				prof_mixed += aspec.samples;
				prof_freq = aspec.freq;
			}
			a_frames += aspec.samples;
			n = aspec.samples;
			if (astereo) {
				for (i = 0; i < 2 * n; i++)
					abuf8[i] = (signed char)(abuf16[i] >> 8);
				a8len = 2 * n;
			} else {
				for (i = 0; i < n; i++)
					abuf8[i] = (signed char)((abuf16[2 * i] + abuf16[2 * i + 1]) >> 9);
				a8len = n;
			}
			a8off = 0;
		}
		n = write(afd, abuf8 + a8off, (unsigned)(a8len - a8off));
		if (n <= 0)		/* flow-controlled (O_NDELAY: 0 or EAGAIN) */
			break;
		a8off += n;
	}
	prof_audio += now_us() - pstart;
	inpump = 0;
}
