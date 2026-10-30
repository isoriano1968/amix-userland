/*
 * vplay -- a small video player for AMIX (Amiga UNIX) on a 68060, built on
 * FFmpeg 4.4's libavformat/libavcodec/libswresample, X11R6.3 with MIT-SHM
 * and the Paula /dev/audio driver.
 *
 *   vplay [-x2] [-noaudio] [-stereo] [-stats] [-nosync] [-noaccel] file
 *
 * One thread (AMIX has none): the main loop reads packets, keeps about a
 * second of decoded sound, feeds /dev/audio without blocking (O_NDELAY),
 * decodes one video frame at a time and shows it when it is due.
 *
 * Timing is a wall clock anchored at the first frame.  Sound is written at
 * the time its timestamps say: samples that are already late are dropped,
 * gaps are filled with silence.  So sound and picture stay together even
 * when the 68060 cannot keep up; video frames that are late are dropped,
 * and if it keeps falling behind the decoder skips non-reference frames
 * and the loop filter, and at worst everything up to the next keyframe.
 * -nosync shows every frame as fast as it decodes and plays no sound.
 *
 * Video: YUV 4:2:0 is converted to the X server's 16-bit RGB565 by table
 * lookups (one per colour component and pixel), optionally doubled in the
 * same pass (-x2).  On the Z3660, with the ZZ9K-SOFT3D extension in the
 * X server, frames are converted straight into off-screen video memory
 * (two buffers from row 2208, clear of soft3D's back buffer at 1100) and
 * the server blits them into the window through its clip list: the 68060
 * never copies a frame.  Otherwise (or with -noaccel) they go into an
 * MIT-SHM image; AMIX allows at most 320 KB per shared segment, so a big
 * window is sent in bands.  Other pixel formats go through swscale to
 * 4:2:0 first.
 *
 * Keys: q/Esc quit, space pause, Left/Right -/+10 s, Down/Up -/+60 s,
 * s statistics on/off.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <X11/Xlibint.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef VPLAY_S3D
#include "libs3d/s3d.h"
#endif
#ifdef VPLAY_ZZMPEG
#include "zzmpeg.h"
#include "zzav.h"
#endif

#include <sys/select.h>
#ifndef __linux__
/* in AMIX's libc, declared in none of its headers */
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif

/* two RGB565 pixels in one 32-bit store, the first at the lower address */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define PAIR(a, b)	((uint32_t)(b) << 16 | (a))
#else
#define PAIR(a, b)	((uint32_t)(a) << 16 | (b))
#endif

/* ---- time ----------------------------------------------------------------- */

static double now_s(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

/* ---- options and state ---------------------------------------------------- */

static int opt_paula;			/* ARM mode: the sound through Paula, not the board's HDMI */
static int opt_scale = 1, opt_noaudio, opt_stereo, opt_stats, opt_nosync, opt_noaccel, opt_nos3d;
static int opt_noarm;
/* MPEG-1 system streams: the Z3660's ARM decodes and shows the picture
   (zzmpeg.c), vplay plays the sound and keeps the overlay on the window */
static const char *net_url0 = "";	/* the file or URL given */
static int use_arm;
static int use_av;			/* H.264 (+ AAC) on the Z3660's core 1: zzav.c */
static int av_scale = 1;		/* the window's size in core 1 mode, not fullscreen */
static int av_seek_on;			/* a seek for av_play() */
static double av_seek_t;
static double net_t0;			/* amixcast's stream: where it started in the file */
static double net_c0 = -1.0;		/* ... and its first time stamp */

static AVFormatContext *fmt;
static int vidx = -1, aidx = -1;
static AVCodecContext *vdec, *adec;
static double vtb, atb;			/* stream time bases in seconds */
static double start_pts;		/* the file's first timestamp */

/* the clock: media time t plays at wall time base + t */
static double base;
static int anchored;			/* base valid (cleared at start and after a seek) */
static int paused;
static double pause_mt;			/* media time when paused */

/* ---- packet queues -------------------------------------------------------- */

struct pq {
	AVPacket *pkt[512];
	int head, n;
};
static struct pq vq, aq;
static int eof_read;			/* av_read_frame hit the end */
static double t_read;			/* time reading the file */

static int pq_put(struct pq *q, AVPacket *p)
{
	if (q->n == 512)
		return -1;
	q->pkt[(q->head + q->n++) % 512] = p;
	return 0;
}

static AVPacket *pq_get(struct pq *q)
{
	AVPacket *p;

	if (!q->n)
		return NULL;
	p = q->pkt[q->head];
	q->head = (q->head + 1) % 512;
	q->n--;
	return p;
}

static void pq_flush(struct pq *q)
{
	AVPacket *p;

	while ((p = pq_get(q)))
		av_packet_free(&p);
}

/* one packet from the file into its queue; 0 at the end */
static int read_one(void)
{
	AVPacket *p;
	int r;

	double t0;

	if (eof_read)
		return 0;
	p = av_packet_alloc();
	t0 = now_s();
	r = av_read_frame(fmt, p);
	t_read += now_s() - t0;
	if (r < 0) {
		av_packet_free(&p);
		eof_read = 1;
		return 0;
	}
	if (p->stream_index == vidx && pq_put(&vq, p) == 0)
		return 1;
	if (p->stream_index == aidx && adec && pq_put(&aq, p) == 0)
		return 1;
	av_packet_free(&p);
	return 1;
}

/* ---- audio ---------------------------------------------------------------- */

/* the AMIX audio driver's ioctls (usr/sys/amiga/driver/audio.h, port kernel) */
#define AUIOC		('U' << 8)
#define AUDIO_VOLUME	(AUIOC | 1)
#define AUDIO_PERIOD	(AUIOC | 2)
#define AUDIO_STEREO	(AUIOC | 3)
#define PAULA_PAL_CLOCK	3546895.0

#define ARING	(256 * 1024)		/* decoded sound, signed 8-bit */
#define AFILL	1.0			/* keep this much decoded (s) */
#define ALEAD	0.6			/* write at most this far ahead (s) */
#define ACHUNK	4096			/* bytes per write: under the driver's 8 KB slot */

static int afd = -1;
static int arate, ach = 1;		/* device rate and channels */
static int amrate;			/* samples per second of media time in the ring */
static uint32_t rs_step;
static int rs_len;
static uint32_t rs_pos;
static SwrContext *swr;
static signed char *aring;
static int aring_len;			/* bytes */
static double aring_pts;		/* media time of aring[0] */
static double dev_end;			/* wall time the device runs dry */
static int adec_eof;
static double t_audio, t_adec;		/* time writing and decoding sound */
static long a_wrote, a_silence, a_dropped, a_refused;	/* bytes, for VPLAY_DEBUG */

static double aring_secs(void)
{
	return amrate ? (double)aring_len / ach / amrate : 0;
}

static void audio_open(void)
{
	int period, vol = 63, one = 1, src = adec->sample_rate;
	int64_t inlayout;

	afd = open("/dev/audio", O_WRONLY | O_NDELAY);
	if (afd < 0) {
		fprintf(stderr, "vplay: /dev/audio: %s; no sound\n", strerror(errno));
		return;
	}
	if (opt_stereo && ioctl(afd, AUDIO_STEREO, &one) == 0)
		ach = 2;
	period = (int)(PAULA_PAL_CLOCK / src + 0.5);
	if (period < 124)		/* Paula's minimum: at most 28.6 kHz */
		period = 124;
	if (ioctl(afd, AUDIO_PERIOD, &period) < 0 || ioctl(afd, AUDIO_VOLUME, &vol) < 0) {
		fprintf(stderr, "vplay: /dev/audio ioctl: %s; no sound\n", strerror(errno));
		close(afd);
		afd = -1;
		return;
	}
	arate = (int)(PAULA_PAL_CLOCK / period + 0.5);
	amrate = arate;
	/*
	 * swresample only converts the sample format and the channels, at the
	 * source rate: its resampling filter cost 15-30 % of the 68060.  The
	 * rate is changed by rs_feed below: each output sample is the average
	 * of the input samples it spans (a box filter), a few additions per
	 * sample, exactly at Paula's rate.
	 */
	rs_step = (uint32_t)(((uint64_t)src << 16) / arate);
	inlayout = adec->channel_layout ? (int64_t)adec->channel_layout :
	    av_get_default_channel_layout(adec->channels);
	swr = swr_alloc_set_opts(NULL, ach == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO,
	    AV_SAMPLE_FMT_S16, src, inlayout, adec->sample_fmt, src, 0, NULL);
	if (!swr || swr_init(swr) < 0) {
		fprintf(stderr, "vplay: cannot convert this sound; no sound\n");
		close(afd);
		afd = -1;
		return;
	}
	aring = malloc(ARING);
	if (opt_stats)
		fprintf(stderr, "vplay: sound %d Hz %d channel%s -> Paula %d Hz (period %d) %s\n",
		    src, adec->channels, adec->channels > 1 ? "s" : "", arate, period,
		    ach == 2 ? "stereo" : "mono");
}

/* the rate converter: 16-bit input at the source rate, 16.16 position */
static int16_t *rs_buf;
static int rs_cap;
/* rs_len: input frames held; rs_pos: where the next output starts (16.16);
   rs_step: input frames per output frame (16.16) */

/* convert what rs_buf holds into the ring, as signed 8-bit at Paula's rate */
static int rs_feed(void)
{
	int c, made = 0, room = (ARING - aring_len) / ach;
	signed char *out = aring + aring_len;

	while (made < room) {
		uint32_t a = rs_pos >> 16, b = (rs_pos + rs_step) >> 16;

		if (b <= a)
			b = a + 1;		/* upsampling: repeat */
		if ((int)b > rs_len)
			break;
		for (c = 0; c < ach; c++) {
			const int16_t *in = rs_buf + a * ach + c;
			long sum = 0;
			uint32_t i;

			for (i = a; i < b; i++, in += ach)
				sum += *in;
			*out++ = (signed char)((sum / (long)(b - a)) >> 8);
		}
		rs_pos += rs_step;
		made++;
	}
	/* keep the input not yet used */
	c = (int)(rs_pos >> 16);
	if (c > rs_len)
		c = rs_len;
	memmove(rs_buf, rs_buf + c * ach, (size_t)(rs_len - c) * ach * 2);
	rs_len -= c;
	rs_pos -= (uint32_t)c << 16;
	aring_len += made * ach;
	return made;
}

/* one decoded sound frame into the ring */
static void audio_add(AVFrame *f)
{
	int n;
	uint8_t *in;
	double pts;

	if (f->best_effort_timestamp == AV_NOPTS_VALUE)
		pts = aring_pts + aring_secs();
	else
		pts = f->best_effort_timestamp * atb;
	n = swr_get_out_samples(swr, f->nb_samples);
	if (rs_len + n > rs_cap) {
		rs_cap = rs_len + n + 4096;
		rs_buf = realloc(rs_buf, (size_t)rs_cap * ach * 2);
	}
	in = (uint8_t *)(rs_buf + rs_len * ach);
	n = swr_convert(swr, &in, n, (const uint8_t **)f->extended_data, f->nb_samples);
	if (n <= 0)
		return;
	if (!aring_len)		/* the ring starts with what rs_buf held before this frame */
		aring_pts = pts - (double)rs_len / adec->sample_rate;
	rs_len += n;
	rs_feed();
}

static void audio_drop(int bytes)
{
	if (bytes > aring_len)
		bytes = aring_len;
	memmove(aring, aring + bytes, aring_len - bytes);
	aring_len -= bytes;
	aring_pts += (double)bytes / ach / amrate;
}

/* decode sound packets until AFILL seconds are ready */
static void audio_decode(void)
{
	static AVFrame *f;
	AVPacket *p;

	double t0;

	if (!adec || afd < 0)
		return;
	if (!f)
		f = av_frame_alloc();
	t0 = now_s();
	while (aring_secs() < AFILL && !adec_eof) {
		int r = avcodec_receive_frame(adec, f);

		if (r == 0) {
			audio_add(f);
			av_frame_unref(f);
			continue;
		}
		if (r == AVERROR_EOF) {
			adec_eof = 1;
			break;
		}
		if ((p = pq_get(&aq))) {
			avcodec_send_packet(adec, p);
			av_packet_free(&p);
		} else if (eof_read) {
			avcodec_send_packet(adec, NULL);
		} else
			break;
	}
	t_adec += now_s() - t0;
}

/* write what the device takes; never blocks */
static void audio_pump(void)
{
	static signed char silence[ACHUNK];
	double t0 = now_s(), now, want;
	int n, w;

	if (afd < 0 || !anchored || paused)
		return;
	for (;;) {
		now = now_s();
		if (dev_end < now)
			dev_end = now;		/* ran dry (or just started) */
		if (dev_end - now > ALEAD || !aring_len)
			break;
		want = base + aring_pts;	/* when aring[0] should be heard */
		if (want < dev_end - 0.08) {	/* late: skip to the present */
			n = (int)((dev_end - want) * amrate) * ach;
			audio_drop(n ? n : ach);
			a_dropped += n ? n : ach;
			continue;
		}
		if (want > dev_end + 0.08) {	/* early: fill the gap with silence */
			n = (int)((want - dev_end) * arate) * ach;
			if (n > ACHUNK)
				n = ACHUNK;
			w = write(afd, silence, n);
			if (w <= 0) {
				a_refused++;
				break;
			}
			a_silence += w;
			dev_end += (double)w / ach / arate;
			continue;
		}
		n = aring_len < ACHUNK ? aring_len : ACHUNK;
		w = write(afd, aring, n);
		if (w <= 0) {			/* flow-controlled */
			a_refused++;
			break;
		}
		a_wrote += w;
		w -= w % ach;
		audio_drop(w);
		dev_end += (double)w / ach / arate;
	}
	t_audio += now_s() - t0;
}

/* ---- display -------------------------------------------------------------- */

static Display *dpy;
static Window win;
static GC gc;
static Visual *vis;
static Atom wm_delete, mwm_hints;
static int scr, vw, vh;			/* video size */
static int ww, wh;			/* window size */
static int dx, dy, dw, dh;		/* the picture in the window (aspect kept) */
static int fullscreen;
static Cursor blank;			/* an invisible pointer */
static int cursor_hidden;
static double last_motion;		/* the pointer last moved (wall time) */
static int save_x, save_y, save_w, save_h;	/* the window before fullscreen */
static XImage *img;			/* MIT-SHM band (or plain image) */
static XShmSegmentInfo si;
static int band_rows;			/* picture rows per band */
static int use_shm, have_shm, shm_failed;
static int use_vram;			/* this picture size goes through video memory */
static int vram_ok, vram_maxrows;	/* video memory mapped, rows per buffer */
static int *xmap, xmap_w;		/* picture column -> source column */
static AVFrame *last;			/* the picture on screen, for redrawing */
static int use_s3d;			/* scaled by soft3D on the Z3660's ARM */
#ifdef VPLAY_S3D
static int s3d_state = -1;		/* -1 not tried, 0 unavailable, 1 running */
static struct s3d_tex *tex;		/* the video frame, RGBA, in board memory */
static int s3d_w, s3d_h;		/* the drawing area set up */
#endif

#define SHMMAX	327680L			/* AMIX's largest shared segment */

static int shm_error(Display *d, XErrorEvent *e)
{
	(void)d; (void)e;
	shm_failed = 1;
	return 0;
}

static int vram_open(void);
static int vram_stride_bytes(void);

/* fit the picture into the window, aspect kept, centred */
static void layout(void)
{
	if ((long)ww * vh >= (long)wh * vw) {
		dh = wh;
		dw = (int)((long)wh * vw / vh);
	} else {
		dw = ww;
		dh = (int)((long)ww * vh / vw);
	}
	dw &= ~1;
	dh &= ~1;
	if (dw < 2)
		dw = 2;
	if (dh < 2)
		dh = 2;
	dx = (ww - dw) / 2;
	dy = (wh - dh) / 2;
}

static void image_free(void)
{
	if (!img)
		return;
	if (use_shm) {
		XShmDetach(dpy, &si);
		XSync(dpy, False);
		shmdt(si.shmaddr);
		img->data = NULL;
	}
	XDestroyImage(img);		/* frees a plain image's buffer too */
	img = NULL;
	use_shm = 0;
}

/* the output for the current picture size: video memory, or an image band */
static void output_setup(void)
{
	static const unsigned short one = 1;
	int (*old)(Display *, XErrorEvent *);
	long bpl = (long)dw * 2;

	use_s3d = 0;
#ifdef VPLAY_S3D
	/*
	 * Larger than the video: the 68060 writes the frame once as a texture
	 * and soft3D on the ARM draws it at the window's size.  Writing the
	 * scaled picture itself would cost the 68060 every screen pixel.
	 */
	if (!opt_noaccel && !opt_nos3d && (dw > vw || dh > vh)) {
		if (s3d_state < 0) {
			s3d_verbose(opt_stats);
			s3d_state = s3d_init(dpy, "vplay") == 0;
		}
		if (s3d_state > 0 && (s3d_w != ww || s3d_h != wh)) {
			if (s3d_resize(ww, wh) == 0) {
				s3d_w = ww;
				s3d_h = wh;
				s3d_clear_color(0, 0, ww, wh, 0);
			} else
				s3d_state = 0;
		}
		if (s3d_state > 0 && !tex) {
			unsigned char *zero = calloc((size_t)vw * vh, 4);

			tex = zero ? s3d_tex_new(vw, vh, 4, zero) : NULL;
			free(zero);
			if (!tex)
				s3d_state = 0;
		}
		use_s3d = s3d_state > 0;
	}
	if (use_s3d) {
		image_free();
		use_vram = 0;
		return;
	}
#endif
	use_vram = vram_ok && dh <= vram_maxrows && bpl <= (long)vram_stride_bytes();
	if (use_vram) {
		image_free();
		return;
	}
	if (img && img->width == dw && band_rows == (dh < SHMMAX / bpl ? dh : (int)(SHMMAX / bpl) & ~3))
		return;
	image_free();
	band_rows = (int)(SHMMAX / bpl) & ~3;	/* whole 2x2 blocks per band, even at 2x */
	if (band_rows > dh)
		band_rows = dh;
	if (have_shm && band_rows >= 4) {
		img = XShmCreateImage(dpy, vis, 16, ZPixmap, NULL, &si, dw, band_rows);
		si.shmid = img ? shmget(IPC_PRIVATE, (size_t)img->bytes_per_line * band_rows,
		    IPC_CREAT | 0600) : -1;
		if (si.shmid >= 0) {
			si.shmaddr = img->data = (char *)shmat(si.shmid, 0, 0);
			if (si.shmaddr != (char *)-1) {
				si.readOnly = True;
				shm_failed = 0;
				old = XSetErrorHandler(shm_error);
				XShmAttach(dpy, &si);
				XSync(dpy, False);
				XSetErrorHandler(old);
				shmctl(si.shmid, IPC_RMID, 0);	/* goes when both detach */
				use_shm = !shm_failed;
				if (!use_shm) {
					shmdt(si.shmaddr);
					have_shm = 0;
				}
			} else
				shmctl(si.shmid, IPC_RMID, 0);
		}
		if (!use_shm && img) {
			img->data = NULL;
			XDestroyImage(img);
			img = NULL;
		}
	}
	if (!img) {
		band_rows = dh;
		img = XCreateImage(dpy, vis, 16, ZPixmap, 0, malloc((size_t)bpl * dh), dw, dh, 16, (int)bpl);
	}
	img->byte_order = *(const char *)&one ? LSBFirst : MSBFirst;	/* native shorts */
}

static void say_output(void)
{
	if (opt_stats)
		fprintf(stderr, "vplay: %dx%d window, picture %dx%d, %s\n", ww, wh, dw, dh,
		    use_s3d ? "a texture, scaled by soft3D on the Z3660's ARM" : use_vram ?
		    "converted into video memory, blitted by the card (ZZ9K-SOFT3D)" :
		    use_shm ? "MIT-SHM" : "XPutImage");
}

static void x_open(const char *title, int w, int h)
{
	XVisualInfo tmpl, *vi;
	XSetWindowAttributes a;
	XSizeHints hints;
	int n;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "vplay: cannot open display \"%s\"\n", XDisplayName(NULL));
		exit(1);
	}
	scr = DefaultScreen(dpy);
	vis = DefaultVisual(dpy, scr);
	tmpl.visualid = XVisualIDFromVisual(vis);
	vi = XGetVisualInfo(dpy, VisualIDMask, &tmpl, &n);
	if (!vi || DefaultDepth(dpy, scr) != 16 || vi->red_mask != 0xf800 ||
	    vi->green_mask != 0x07e0 || vi->blue_mask != 0x001f) {
		fprintf(stderr, "vplay: needs a 16-bit RGB565 screen\n");
		exit(1);
	}
	XFree(vi);
	ww = w;
	wh = h;
	a.background_pixel = BlackPixel(dpy, scr);
	a.event_mask = KeyPressMask | ButtonPressMask | PointerMotionMask | ExposureMask | StructureNotifyMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, ww, wh, 0, 16, InputOutput, vis,
	    CWBackPixel | CWEventMask, &a);
	hints.flags = PSize | PMinSize;
	hints.width = ww;
	hints.height = wh;
	hints.min_width = 32;
	hints.min_height = 32;
	XSetWMNormalHints(dpy, win, &hints);
	XStoreName(dpy, win, title);
	wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	mwm_hints = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);
	gc = XCreateGC(dpy, win, 0, NULL);
	{
		static char nothing[1];
		Pixmap pm = XCreateBitmapFromData(dpy, win, nothing, 1, 1);
		XColor black;

		memset(&black, 0, sizeof black);
		blank = XCreatePixmapCursor(dpy, pm, pm, &black, &black, 0, 0);
		XFreePixmap(dpy, pm);
	}
	have_shm = XShmQueryExtension(dpy);
	vram_ok = vram_open();
	layout();
	output_setup();
	say_output();
	XMapWindow(dpy, win);
	XFlush(dpy);
}

/*
 * Fullscreen under dtwm (no EWMH): take the frame away with _MOTIF_WM_HINTS
 * and cover the screen; the window is unmapped and mapped again so the
 * window manager reads the hint.  The place goes into the size hints before
 * the map and is asked for again once mapped: a resize sent while dtwm
 * still takes the window on is dropped (only the frame went).
 */
static void set_fullscreen(int on)
{
	long hint[5] = { 2, 0, 0, 0, 0 };	/* MWM_HINTS_DECORATIONS, none */
	XSizeHints sh;
	Window child;
	XEvent e;
	int x, y, w, h, i;

	if (on == fullscreen)
		return;
#ifdef VPLAY_ZZMPEG
	if (use_av)			/* the picture grows with the window */
		zzav_scale(on ? 4 : av_scale);
#endif
	if (on) {
		XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &save_x, &save_y, &child);
		save_w = ww;
		save_h = wh;
		x = y = 0;
		w = DisplayWidth(dpy, scr);
		h = DisplayHeight(dpy, scr);
	} else {
		x = save_x;
		y = save_y;
		w = save_w;
		h = save_h;
	}
	XUnmapWindow(dpy, win);
	XSync(dpy, False);
	while (XCheckTypedWindowEvent(dpy, win, UnmapNotify, &e))
		;
	if (on)
		XChangeProperty(dpy, win, mwm_hints, mwm_hints, 32, PropModeReplace, (unsigned char *)hint, 5);
	else
		XDeleteProperty(dpy, win, mwm_hints);
	memset(&sh, 0, sizeof sh);
	sh.flags = USPosition | USSize | PMinSize;
	sh.x = x;
	sh.y = y;
	sh.width = w;
	sh.height = h;
	sh.min_width = 32;
	sh.min_height = 32;
	XSetWMNormalHints(dpy, win, &sh);
	XMoveResizeWindow(dpy, win, x, y, w, h);
	XMapRaised(dpy, win);
	XSync(dpy, False);
	for (i = 0; i < 100 && !XCheckTypedWindowEvent(dpy, win, MapNotify, &e); i++)
		usleep(10000);		/* up to a second for dtwm */
	XMoveResizeWindow(dpy, win, x, y, w, h);
	XFlush(dpy);
	fullscreen = on;
	last_motion = now_s();
	if (!on && cursor_hidden) {
		XUndefineCursor(dpy, win);
		cursor_hidden = 0;
	}
}

/* in fullscreen the pointer goes after a second without moving, as in ffplay */
static void cursor_check(void)
{
	if (fullscreen && !cursor_hidden && now_s() - last_motion > 1.0) {
		XDefineCursor(dpy, win, blank);
		XFlush(dpy);
		cursor_hidden = 1;
	}
}

/* ---- Z3660: off-screen video memory and the ZZ9K-SOFT3D blit -------------- */

/* the AMIX zz9000 driver (zz9000.h) */
#define ZZ9K_IOC		('Z' << 8)
#define ZZ9KIOC_GETFBINFO	(ZZ9K_IOC | 11)
struct zz9k_fbinfo {
	unsigned long fb_offset, fb_size;
	unsigned short width, height, color_mode, depth, bytes_per_pixel, stride;
	unsigned long red_mask, green_mask, blue_mask, alpha_mask;
	unsigned short red_shift, green_shift, blue_shift, alpha_shift;
};
/* the X server's ZZ9K-SOFT3D Swap: copy rows of off-screen VRAM into a
   window, through its clip list, with the card's blitter */
#define X_ZZ9KSwap	0
typedef struct {
	CARD8 reqType;
	CARD8 s3dReqType;
	CARD16 length B16;
	CARD32 window B32;
	INT16 srcX B16, srcY B16;
	INT16 x B16, y B16;
	CARD16 width B16, height B16;
} xZZ9KSwapReq;
#define sz_xZZ9KSwapReq 20

#define VRAM_ROW	2208		/* page-aligned at a 3840-byte stride */
#define VRAM_END	0xC00000UL	/* soft3D's textures start here */
static int xmajor, vram_stride, vram_cur;
static unsigned char *vram[2];		/* two pictures, alternately */
static int vram_row[2];

static int vram_stride_bytes(void)
{
	return vram_stride;
}

/* map two picture buffers in off-screen video memory, as large as fits */
static int vram_open(void)
{
	struct zz9k_fbinfo fbi;
	int ev, err, fd, rows, i;
	unsigned long len, end;

	if (opt_noaccel || !XQueryExtension(dpy, "ZZ9K-SOFT3D", &xmajor, &ev, &err))
		return 0;
	fd = open("/dev/zz9000", O_RDWR);
	if (fd < 0)
		return 0;
	if (ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0 || fbi.bytes_per_pixel != 2) {
		close(fd);
		return 0;
	}
	vram_stride = fbi.stride;
	/* both buffers below soft3D's textures (device 0xC00000) and in the framebuffer */
	end = fbi.fb_size < VRAM_END ? fbi.fb_size : VRAM_END;
	rows = (int)((end / vram_stride - VRAM_ROW) / 2) & ~15;	/* keeps buffer 2 page-aligned */
	if (rows < 16) {
		close(fd);
		return 0;
	}
	len = (unsigned long)rows * vram_stride;
	for (i = 0; i < 2; i++) {
		vram_row[i] = VRAM_ROW + i * rows;
		vram[i] = (unsigned char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		    (off_t)((unsigned long)vram_row[i] * vram_stride));
		if (vram[i] == (unsigned char *)-1) {
			close(fd);
			return 0;
		}
	}
	close(fd);				/* the mappings stay */
	vram_maxrows = rows;
	return 1;
}

static void vram_swap(int row)
{
	xZZ9KSwapReq *req;

	LockDisplay(dpy);
	GetReq(ZZ9KSwap, req);
	req->reqType = xmajor;
	req->s3dReqType = X_ZZ9KSwap;
	req->window = win;
	req->srcX = 0;
	req->srcY = row;
	req->x = dx;
	req->y = dy;
	req->width = dw;
	req->height = dh;
	UnlockDisplay(dpy);
	SyncHandle();
	XFlush(dpy);
}

/* ---- YUV 4:2:0 -> RGB565 -------------------------------------------------- */

#define TOFF 384			/* index offset of the clamp tables */
static unsigned short rtab[1024], gtab[1024], btab[1024];
static int ytab[256], rvtab[256], gutab[256], gvtab[256], butab[256];
static int tables_full = -1;		/* which range the tables are for */

static void yuv_tables(int full)
{
	int i, c;
	double ys = full ? 1.0 : 255.0 / 219, cs = full ? 1.0 : 255.0 / 224, yo = full ? 0 : 16;

	if (tables_full == full)
		return;
	tables_full = full;
	for (i = 0; i < 1024; i++) {
		c = i - TOFF;
		c = c < 0 ? 0 : c > 255 ? 255 : c;
		rtab[i] = (unsigned short)((c >> 3) << 11);
		gtab[i] = (unsigned short)((c >> 2) << 5);
		btab[i] = (unsigned short)(c >> 3);
	}
	for (i = 0; i < 256; i++) {		/* BT.601 */
		ytab[i] = (int)((i - yo) * ys + 0.5 + TOFF) - TOFF;
		rvtab[i] = (int)((i - 128) * cs * 1.402 + (i >= 128 ? 0.5 : -0.5));
		gutab[i] = (int)((i - 128) * cs * 0.344136 + (i >= 128 ? 0.5 : -0.5));
		gvtab[i] = (int)((i - 128) * cs * 0.714136 + (i >= 128 ? 0.5 : -0.5));
		butab[i] = (int)((i - 128) * cs * 1.772 + (i >= 128 ? 0.5 : -0.5));
	}
}

/*
 * Exact 1x or 2x: source rows [sy, sy + rows) (rows even).  Two source rows
 * share one chroma row; each 2x2 block computes its chroma terms once.
 */
static void yuv_exact(AVFrame *f, int scale, int sy, int rows, unsigned char *dst, int stride)
{
	int x, y, w2 = vw / 2;

	for (y = sy; y < sy + rows; y += 2) {
		const unsigned char *y0 = f->data[0] + (long)y * f->linesize[0];
		const unsigned char *y1 = y0 + f->linesize[0];
		const unsigned char *u = f->data[1] + (long)(y / 2) * f->linesize[1];
		const unsigned char *v = f->data[2] + (long)(y / 2) * f->linesize[2];

		if (scale == 1) {
			uint32_t *d0 = (uint32_t *)dst, *d1 = (uint32_t *)(dst + stride);

			for (x = 0; x < w2; x++) {
				int cv = *v++, cu = *u++, a, b;
				const unsigned short *r = rtab + TOFF + rvtab[cv];
				const unsigned short *g = gtab + TOFF - gutab[cu] - gvtab[cv];
				const unsigned short *bl = btab + TOFF + butab[cu];

				a = ytab[*y0++]; b = ytab[*y0++];
				*d0++ = PAIR(r[a] | g[a] | bl[a], r[b] | g[b] | bl[b]);
				a = ytab[*y1++]; b = ytab[*y1++];
				*d1++ = PAIR(r[a] | g[a] | bl[a], r[b] | g[b] | bl[b]);
			}
			dst += 2 * stride;
		} else {			/* 2x: each pixel twice, each row twice */
			uint32_t *d0 = (uint32_t *)dst, *d2 = (uint32_t *)(dst + 2 * stride);

			for (x = 0; x < w2; x++) {
				int cv = *v++, cu = *u++, a;
				uint32_t p;
				const unsigned short *r = rtab + TOFF + rvtab[cv];
				const unsigned short *g = gtab + TOFF - gutab[cu] - gvtab[cv];
				const unsigned short *bl = btab + TOFF + butab[cu];

				a = ytab[*y0++]; p = r[a] | g[a] | bl[a]; *d0++ = p << 16 | p;
				a = ytab[*y0++]; p = r[a] | g[a] | bl[a]; *d0++ = p << 16 | p;
				a = ytab[*y1++]; p = r[a] | g[a] | bl[a]; *d2++ = p << 16 | p;
				a = ytab[*y1++]; p = r[a] | g[a] | bl[a]; *d2++ = p << 16 | p;
			}
			memcpy(dst + stride, dst, (size_t)dw * 2);
			memcpy(dst + 3 * stride, dst + 2 * stride, (size_t)dw * 2);
			dst += 4 * stride;
		}
	}
}

/* one picture pixel at source column sx of a luma/chroma row */
#define PIX(yr, ur, vr, sx) ({ int a_ = ytab[(yr)[sx]], u_ = (ur)[(sx) >> 1], v_ = (vr)[(sx) >> 1]; \
	rtab[TOFF + a_ + rvtab[v_]] | gtab[TOFF + a_ - gutab[u_] - gvtab[v_]] | btab[TOFF + a_ + butab[u_]]; })

/*
 * Any other size: nearest pixel.  Picture rows [r0, r0 + n) into dst; a
 * row that maps to the same source row as the one before is copied.
 */
static void yuv_scaled(AVFrame *f, int r0, int n, unsigned char *dst, int stride)
{
	int x, r, sy, prev = -1;

	if (xmap_w != dw) {
		free(xmap);
		xmap = malloc(sizeof *xmap * dw);
		for (x = 0; x < dw; x++)
			xmap[x] = (int)(((2L * x + 1) * vw) / (2L * dw));
		xmap_w = dw;
	}
	for (r = r0; r < r0 + n; r++, dst += stride) {
		const unsigned char *yr, *ur, *vr;
		uint32_t *d = (uint32_t *)dst;

		sy = (int)(((2L * r + 1) * vh) / (2L * dh));
		if (sy == prev) {
			memcpy(dst, dst - stride, (size_t)dw * 2);
			continue;
		}
		prev = sy;
		yr = f->data[0] + (long)sy * f->linesize[0];
		ur = f->data[1] + (long)(sy / 2) * f->linesize[1];
		vr = f->data[2] + (long)(sy / 2) * f->linesize[2];
		for (x = 0; x < dw; x += 2)
			*d++ = PAIR(PIX(yr, ur, vr, xmap[x]), PIX(yr, ur, vr, xmap[x + 1]));
	}
}

/* picture rows [r0, r0 + n) */
static void yuv_rows(AVFrame *f, int r0, int n, unsigned char *dst, int stride)
{
	if (dw == vw && dh == vh && !(r0 & 1) && !(n & 1))
		yuv_exact(f, 1, r0, n, dst, stride);
	else if (dw == 2 * vw && dh == 2 * vh && !(r0 & 3) && !(n & 3))
		yuv_exact(f, 2, r0 / 2, n / 2, dst, stride);
	else
		yuv_scaled(f, r0, n, dst, stride);
}

#ifdef VPLAY_S3D
/* RGBA texels for soft3D: bytes R G B A, written as one big-endian long */
static uint32_t r32[1024], g32[1024], b32[1024];

static void rgba_tables(void)
{
	int i, c;

	for (i = 0; i < 1024; i++) {
		c = i - TOFF;
		c = c < 0 ? 0 : c > 255 ? 255 : c;
		r32[i] = (uint32_t)c << 24;
		g32[i] = (uint32_t)c << 16;
		b32[i] = (uint32_t)c << 8 | 0xff;
	}
}

/* the whole frame, 1:1, into the texture (rows of vw texels) */
static void yuv_rgba(AVFrame *f, uint32_t *dst)
{
	int x, y, w2 = vw / 2;

	for (y = 0; y < vh; y += 2) {
		const unsigned char *y0 = f->data[0] + (long)y * f->linesize[0];
		const unsigned char *y1 = y0 + f->linesize[0];
		const unsigned char *u = f->data[1] + (long)(y / 2) * f->linesize[1];
		const unsigned char *v = f->data[2] + (long)(y / 2) * f->linesize[2];
		uint32_t *d0 = dst + (long)y * vw, *d1 = d0 + vw;

		for (x = 0; x < w2; x++) {
			int cv = *v++, cu = *u++, a;
			const uint32_t *r = r32 + TOFF + rvtab[cv];
			const uint32_t *g = g32 + TOFF - gutab[cu] - gvtab[cv];
			const uint32_t *bl = b32 + TOFF + butab[cu];

			a = ytab[*y0++]; *d0++ = r[a] | g[a] | bl[a];
			a = ytab[*y0++]; *d0++ = r[a] | g[a] | bl[a];
			a = ytab[*y1++]; *d1++ = r[a] | g[a] | bl[a];
			a = ytab[*y1++]; *d1++ = r[a] | g[a] | bl[a];
		}
	}
}

static double t_conv, t_put;

/* the frame as a textured rectangle over the picture area, then presented */
static void show_s3d(AVFrame *f, double t0)
{
	static int have_tables;
	struct s3d_state st;
	struct s3d_vertex v[4];
	unsigned char *px;
	int i;

	if (!have_tables) {
		rgba_tables();
		have_tables = 1;
	}
	s3d_tex_wait();				/* the last frame is drawn */
	px = s3d_tex_pixels(tex);
	if (!px)
		return;
	yuv_rgba(f, (uint32_t *)px);
	s3d_tex_touch(tex);
	t_conv += now_s() - t0;
	t0 = now_s();
	memset(&st, 0, sizeof st);
	st.tex = tex;
	st.blend = S3D_REPLACE;
	st.zmode = S3D_ZMODE(0, S3D_Z_ALWAYS);
	st.texenv = S3D_ENV_REPLACE;
	s3d_set_state(&st);
	for (i = 0; i < 4; i++) {
		v[i].x = (float)(dx + ((i == 1 || i == 2) ? dw : 0));
		v[i].y = (float)(dy + (i >= 2 ? dh : 0));
		v[i].z = 0.5f;
		v[i].w = 1.0f;
		v[i].u = (i == 1 || i == 2) ? 1.0f : 0.0f;
		v[i].v = i >= 2 ? 1.0f : 0.0f;
		v[i].r = v[i].g = v[i].b = v[i].a = 255;
	}
	s3d_tri(&v[0], &v[1], &v[2]);
	s3d_tri(&v[0], &v[2], &v[3]);
	s3d_swap(dpy, win);
	t_put += now_s() - t0;
}
#else
static double t_conv, t_put;
#endif

static struct SwsContext *sws;
static AVFrame *conv;			/* 4:2:0 copy of frames in other formats */

static void show(AVFrame *f)
{
	int y, rows, full = f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P;
	double t0 = now_s();

	if (f != last) {			/* keep it for redrawing */
		av_frame_unref(last);
		av_frame_ref(last, f);
	}
	if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
		if (!conv) {
			conv = av_frame_alloc();
			conv->format = AV_PIX_FMT_YUV420P;
			conv->width = vw;
			conv->height = vh;
			av_frame_get_buffer(conv, 0);
		}
		sws = sws_getCachedContext(sws, f->width, f->height, f->format, vw, vh,
		    AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		sws_scale(sws, (const uint8_t *const *)f->data, f->linesize, 0, f->height,
		    conv->data, conv->linesize);
		f = conv;
	}
	yuv_tables(full);
#ifdef VPLAY_S3D
	if (use_s3d) {
		show_s3d(f, t0);
		return;
	}
#endif
	if (use_vram) {
		/* the other buffer: the blitter may still be copying the last one */
		vram_cur ^= 1;
		yuv_rows(f, 0, dh, vram[vram_cur], vram_stride);
		t_conv += now_s() - t0;
		t0 = now_s();
		XSync(dpy, False);		/* the previous blit has been started */
		vram_swap(vram_row[vram_cur]);
		t_put += now_s() - t0;
		return;
	}
	for (y = 0; y < dh; y += rows) {
		rows = band_rows;
		if (rows > dh - y)
			rows = dh - y;
		yuv_rows(f, y, rows, (unsigned char *)img->data, img->bytes_per_line);
		t_conv += now_s() - t0;
		t0 = now_s();
		if (use_shm) {
			XShmPutImage(dpy, win, gc, img, 0, 0, dx, dy + y, dw, rows, False);
			XSync(dpy, False);	/* the server has read the band */
		} else {
			XPutImage(dpy, win, gc, img, 0, 0, dx, dy + y, dw, rows);
			XFlush(dpy);
		}
		t_put += now_s() - t0;
		t0 = now_s();
	}
}

/* the window changed size: new layout, new output, the picture again */
static void resized(int w, int h)
{
	if (w == ww && h == wh)
		return;
	ww = w;
	wh = h;
	layout();
	output_setup();
	say_output();
	XClearWindow(dpy, win);
	if (last && last->data[0])
		show(last);
}

/* ---- video ---------------------------------------------------------------- */

static AVFrame *vf;			/* the next frame to show */
static int vf_ready, vdec_eof;
static double vf_pts, vframe_dur = 0.04;
static long n_shown, n_dropped;
static double t_vdec, lag;
static enum AVDiscard skip_level = AVDISCARD_DEFAULT;

static void set_skip(enum AVDiscard l)
{
	if (l == skip_level)
		return;
	skip_level = l;
	vdec->skip_frame = l;
	vdec->skip_loop_filter = l == AVDISCARD_DEFAULT ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	if (opt_stats)
		fprintf(stderr, "vplay: decoder skips %s\n", l == AVDISCARD_DEFAULT ? "nothing" :
		    l == AVDISCARD_NONREF ? "non-reference frames and the loop filter" :
		    "everything up to the next keyframe");
}

/* try to get the next frame from the decoder; 1 when vf is ready */
static int video_decode(void)
{
	AVPacket *p;
	double t0 = now_s();
	int r;

	for (;;) {
		r = avcodec_receive_frame(vdec, vf);
		if (r == 0) {
			int64_t ts = vf->best_effort_timestamp;

			vf_pts = ts == AV_NOPTS_VALUE ? vf_pts + vframe_dur : ts * vtb;
			vf_ready = 1;
			break;
		}
		if (r == AVERROR_EOF) {
			vdec_eof = 1;
			break;
		}
		if ((p = pq_get(&vq))) {
			avcodec_send_packet(vdec, p);
			av_packet_free(&p);
		} else if (eof_read) {
			avcodec_send_packet(vdec, NULL);
		} else
			break;			/* needs another packet from the file */
	}
	t_vdec += now_s() - t0;
	return vf_ready;
}

/* ---- seeking -------------------------------------------------------------- */

static void seek_to(double t)
{
	if (use_arm)
		return;			/* the firmware's decoder cannot seek */
	if (use_av) {			/* av_play() opens core 1's player anew there */
		av_seek_t = t;
		av_seek_on = 1;
		return;
	}
	if (t < start_pts)
		t = start_pts;
	if (av_seek_frame(fmt, -1, (int64_t)(t * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0)
		return;
	pq_flush(&vq);
	pq_flush(&aq);
	if (vdec)
		avcodec_flush_buffers(vdec);
	if (adec)
		avcodec_flush_buffers(adec);
	if (vf_ready)
		av_frame_unref(vf);
	vf_ready = vdec_eof = adec_eof = eof_read = 0;
	aring_len = 0;
	rs_len = 0;
	rs_pos = 0;
	if (swr)
		swr_init(swr);
	anchored = 0;
	set_skip(AVDISCARD_DEFAULT);
}

static double media_now(void)
{
#ifdef VPLAY_ZZMPEG
	if (use_av)
		return net_t0 + zzav_clock() - (net_c0 >= 0.0 ? net_c0 : 0.0);
#endif
	return paused ? pause_mt : now_s() - base;
}

/* ---- the info window (key i) ---------------------------------------------- */

static Window infow;
static GC infogc;
static XFontStruct *infofont;
static char info_head[1600];		/* the file and its streams, from the start */
static const char *decoder_name = "the 68060";

static void info_prepare(const char *file)
{
	char *o = info_head;
	unsigned i;

	o += sprintf(o, "%s\n%s, %.1f s, %.0f kbit/s\n", file, fmt->iformat->long_name ? fmt->iformat->long_name :
	    fmt->iformat->name, fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0.0, fmt->bit_rate / 1000.0);
	for (i = 0; i < fmt->nb_streams && o < info_head + sizeof info_head - 200; i++) {
		AVStream *st = fmt->streams[i];
		AVCodecParameters *c = st->codecpar;
		const char *prof = avcodec_profile_name(c->codec_id, c->profile);

		if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
			AVRational fr = av_guess_frame_rate(fmt, st, NULL);

			o += sprintf(o, "video: %s%s%s, %dx%d, %.3g fps, %.0f kbit/s\n", avcodec_get_name(c->codec_id),
			    prof ? " " : "", prof ? prof : "", c->width, c->height, fr.den ? av_q2d(fr) : 0.0,
			    c->bit_rate / 1000.0);
		} else if (c->codec_type == AVMEDIA_TYPE_AUDIO)
			o += sprintf(o, "audio: %s%s%s, %d Hz, %d channels, %.0f kbit/s\n", avcodec_get_name(c->codec_id),
			    prof ? " " : "", prof ? prof : "", c->sample_rate, c->channels, c->bit_rate / 1000.0);
	}
}

static void info_draw(void)
{
	char text[2400], *line, *next;
	int y;

	if (!infow)
		return;
	strcpy(text, info_head);
	sprintf(text + strlen(text), "decoded by %s\n", decoder_name);
#ifdef VPLAY_ZZMPEG
	if (use_av) {
		char buf[512];

		zzav_stats(buf, sizeof buf);
		/* "core 1: ..., ... dropped, decode .. us, show .. us, lag .. ms, skip ..; sound ..." in two lines */
		if ((next = strchr(buf, ';')) != NULL) {
			*next = 0;
			sprintf(text + strlen(text), "%s\n%s\n", buf, next + 2);
		} else
			sprintf(text + strlen(text), "%s\n", buf);
	} else
#endif
		sprintf(text + strlen(text), "%ld frames shown, %ld dropped, lag %.0f ms%s\n", n_shown, n_dropped,
		    lag * 1000, paused ? ", paused" : "");
	XClearWindow(dpy, infow);
	y = infofont->ascent + 6;
	for (line = text; *line; line = next) {
		next = strchr(line, '\n');
		if (!next)
			next = line + strlen(line);
		/* long lines (the core 1 report) wrap at ~76 characters, at a comma */
		while (next - line > 76) {
			char *cut = line + 76;

			while (cut > line + 20 && *cut != ',' && *cut != ' ')
				cut--;
			XDrawString(dpy, infow, infogc, 6, y, line, (int)(cut - line + 1));
			y += infofont->ascent + infofont->descent + 1;
			line = cut + 1;
			while (*line == ' ')
				line++;
		}
		XDrawString(dpy, infow, infogc, 6, y, line, (int)(next - line));
		y += infofont->ascent + infofont->descent + 1;
		if (*next)
			next++;
	}
}

static void info_toggle(void)
{
	XSizeHints sh;

	if (infow) {
		XDestroyWindow(dpy, infow);
		infow = 0;
		return;
	}
	if (!infofont && !(infofont = XLoadQueryFont(dpy, "fixed")))
		return;
	infow = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 40, 40, 6 + 78 * infofont->max_bounds.width,
	    12 * (infofont->ascent + infofont->descent + 1) + 10, 1, BlackPixel(dpy, scr), WhitePixel(dpy, scr));
	XStoreName(dpy, infow, "vplay info");
	sh.flags = PPosition;
	sh.x = sh.y = 40;
	XSetWMNormalHints(dpy, infow, &sh);
	XSetWMProtocols(dpy, infow, &wm_delete, 1);
	XSelectInput(dpy, infow, ExposureMask | KeyPressMask);
	if (!infogc) {
		infogc = XCreateGC(dpy, infow, 0, NULL);
		XSetFont(dpy, infogc, infofont->fid);
		XSetForeground(dpy, infogc, BlackPixel(dpy, scr));
	}
	XMapRaised(dpy, infow);
}

/* ---- events --------------------------------------------------------------- */

static int quit;

static int muted;

static void toggle_pause(void)
{
	if (use_arm)
		return;			/* nor pause */
#ifdef VPLAY_ZZMPEG
	if (use_av) {
		paused = !paused;
		zzav_pause(paused);
		return;
	}
#endif
	if (!paused) {
		pause_mt = media_now();
		paused = 1;
	} else {
		base = now_s() - pause_mt;
		dev_end = 0;
		paused = 0;
	}
}

static void arm_place(void);

/* window size as a multiple of the video's, kept on the screen */
static void size_window(double k)
{
#ifdef VPLAY_ZZMPEG
	/* core 1 writes the picture 1:1 or at half size (the overlay does not
	   scale): the window follows the picture */
	if (use_av) {
		int ow, oh, vsc;

		av_scale = k > 1.0 ? 2 : 1;
		if (fullscreen)
			set_fullscreen(0);
		zzav_half(k < 1.0);
		zzav_scale(av_scale);
		zzav_size(&ow, &oh, &vsc);
		if (fullscreen)
			arm_place();	/* centred in the full screen */
		else
			XResizeWindow(dpy, win, ow, oh * vsc);
		return;
	}
#endif
	int w = (int)(vw * k + 0.5), h = (int)(vh * k + 0.5);

	if (fullscreen)
		set_fullscreen(0);
	if (w > DisplayWidth(dpy, scr))
		w = DisplayWidth(dpy, scr);
	if (h > DisplayHeight(dpy, scr))
		h = DisplayHeight(dpy, scr);
	XResizeWindow(dpy, win, w < 32 ? 32 : w, h < 32 ? 32 : h);
}

/*
 * ffplay's keys: q/Esc quit, f fullscreen, p/space pause, m mute,
 * Left/Right -/+10 s, Down/Up -/+60 s, PgDn/PgUp -/+600 s; left double-click
 * fullscreen, right click seeks to that fraction of the file.  Ours as well:
 * 1 original size, 2 double, h half, i the info window, I statistics on stderr.
 */
static void arm_place(void);

static void events(void)
{
	static Time last_click;
	XEvent e;
	KeySym k;
	char c;

	while (dpy && XPending(dpy)) {
		XNextEvent(dpy, &e);
		switch (e.type) {
		case ClientMessage:
			if ((Atom)e.xclient.data.l[0] == wm_delete) {
				if (infow && e.xclient.window == infow)
					info_toggle();	/* the info window closed, not the player */
				else
					quit = 1;
			}
			continue;
		case ConfigureNotify:
			if (getenv("VPLAY_DEBUG"))
				fprintf(stderr, "vplay: ConfigureNotify %dx%d at %d,%d send_event %d\n",
				    e.xconfigure.width, e.xconfigure.height, e.xconfigure.x, e.xconfigure.y,
				    e.xconfigure.send_event);
			resized(e.xconfigure.width, e.xconfigure.height);
			arm_place();
			continue;
		case Expose:
			if (infow && e.xexpose.window == infow) {
				if (e.xexpose.count == 0)
					info_draw();
				continue;
			}
			if (e.xexpose.count == 0 && last && last->data[0] && (paused || !vf_ready))
				show(last);
			continue;
		case MotionNotify:
			last_motion = now_s();
			if (cursor_hidden) {
				XUndefineCursor(dpy, win);
				cursor_hidden = 0;
			}
			continue;
		case ButtonPress:
			if (e.xbutton.button == Button1) {
				if (e.xbutton.time - last_click < 400)
					set_fullscreen(!fullscreen);
				last_click = e.xbutton.time;
			} else if (e.xbutton.button == Button3 && fmt->duration > 0 && ww > 0)
				seek_to(start_pts + fmt->duration / (double)AV_TIME_BASE * e.xbutton.x / ww);
			continue;
		case KeyPress:
			break;
		default:
			continue;
		}
		k = NoSymbol;
		XLookupString(&e.xkey, &c, 1, &k, NULL);
		switch (k) {
		case XK_q: case XK_Q: case XK_Escape:
			quit = 1;
			break;
		case XK_f: case XK_F:
			set_fullscreen(!fullscreen);
			break;
		case XK_p: case XK_P: case XK_space:
			toggle_pause();
			break;
		case XK_m: case XK_M:
#ifdef VPLAY_ZZMPEG
			if (use_av) {
				muted = !muted;
				zzav_volume(muted ? 0 : 256);
				break;
			}
#endif
			if (afd >= 0) {
				int vol = muted ? 63 : 0;

				ioctl(afd, AUDIO_VOLUME, &vol);
				muted = !muted;
			}
			break;
		case XK_1: size_window(1); break;
		case XK_2: size_window(2); break;
		case XK_h: case XK_H: size_window(0.5); break;
		case XK_i:
			info_toggle();
			break;
		case XK_I:			/* the statistics on stderr */
			opt_stats = !opt_stats;
			break;
		case XK_Left:  seek_to(media_now() - 10); break;
		case XK_Right: seek_to(media_now() + 10); break;
		case XK_Down:  seek_to(media_now() - 60); break;
		case XK_Up:    seek_to(media_now() + 60); break;
		case XK_Next:  seek_to(media_now() - 600); break;
		case XK_Prior: seek_to(media_now() + 600); break;
		}
	}
	if (dpy)
		cursor_check();
	{
		static double last_info;

		if (infow && now_s() - last_info >= 1.0) {
			last_info = now_s();
			info_draw();
		}
	}
}

/* the overlay in the middle of the window (it does not scale) */
static void arm_place(void)
{
#ifdef VPLAY_ZZMPEG
	Window child;
	int x, y;

	if ((!use_arm && !use_av) || !dpy)
		return;
	XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child);
	if (use_av) {
		int ow, oh, vsc;

		zzav_size(&ow, &oh, &vsc);
		zzav_place(x + (ww - ow) / 2, y + (wh - oh * vsc) / 2);
		return;
	}
	zzm_place(x + (ww - vw) / 2, y + (wh - vh * zzm_vscale()) / 2);
#endif
}

/* sleep until t (wall time) or an X event, whichever first */
static void wait_until(double t)
{
	struct timeval tv;
	fd_set fds;
	double d = t - now_s();
	int fd = dpy ? ConnectionNumber(dpy) : -1;

	if (d <= 0)
		return;
	if (d > 0.02)
		d = 0.02;		/* keep the sound fed */
	tv.tv_sec = 0;
	tv.tv_usec = (long)(d * 1e6);
	FD_ZERO(&fds);
	if (fd >= 0)
		FD_SET(fd, &fds);
	select(fd + 1, &fds, NULL, NULL, &tv);
}

/* ---- statistics ----------------------------------------------------------- */

static void stats(void)
{
	static double last;
	static long shown0, dropped0, cpu0;
	double now = now_s(), span;
	struct tms tm;
	long cpu;

	times(&tm);
	cpu = (long)(tm.tms_utime + tm.tms_stime);
	if (!last) {
		last = now;
		cpu0 = cpu;
		return;
	}
	span = now - last;
	if (span < 2.0)
		return;
	if (opt_stats)
		fprintf(stderr, "vplay: %5.1f fps shown, %4.1f dropped, lag %4.0f ms; cpu %3.0f%%: video %2.0f "
		    "convert %2.0f put %2.0f sound %2.0f+%.0f file %2.0f\n",
		    (n_shown - shown0) / span, (n_dropped - dropped0) / span, lag * 1000,
		    (cpu - cpu0) * 100.0 / sysconf(_SC_CLK_TCK) / span,
		    t_vdec * 100 / span, t_conv * 100 / span, t_put * 100 / span,
		    t_adec * 100 / span, t_audio * 100 / span, t_read * 100 / span);
	if (getenv("VPLAY_DEBUG"))
		fprintf(stderr, "vplay: sound wrote %ld silence %ld dropped %ld refused %ld; ring %d bytes from %.2f, "
		    "clock %.2f, device ahead %.2f\n", a_wrote, a_silence, a_dropped, a_refused, aring_len, aring_pts,
		    now - base, dev_end - now);
	a_wrote = a_silence = a_dropped = a_refused = 0;
	last = now;
	shown0 = n_shown;
	dropped0 = n_dropped;
	cpu0 = cpu;
	t_vdec = t_conv = t_put = t_audio = t_adec = t_read = 0;
}

/* ---- main ----------------------------------------------------------------- */

static AVCodecContext *open_decoder(int idx)
{
	AVStream *st = fmt->streams[idx];
	AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
	AVCodecContext *d;

	if (!c)
		return NULL;
	d = avcodec_alloc_context3(c);
	avcodec_parameters_to_context(d, st->codecpar);
	d->thread_count = 1;
	if (avcodec_open2(d, c, NULL) < 0) {
		avcodec_free_context(&d);
		return NULL;
	}
	return d;
}

static void usage(void)
{
	fprintf(stderr, "usage: vplay [options] file\n"
	    "  -fs           start in fullscreen\n"
	    "  -x W, -y H    window size (the other side follows the aspect ratio)\n"
	    "  -x2           window twice the video's size\n"
	    "  -ss SECONDS   start there\n"
	    "  -an           no sound (also -noaudio)\n"
	    "  -stereo       stereo through Paula's locked channel pair\n"
	    "  -stats        statistics every 2 seconds (key i)\n"
	    "  -nosync       every frame, as fast as it decodes, no sound\n"
	    "  -noaccel      MIT-SHM instead of the Z3660's blitter and soft3D\n"
	    "  -nos3d        scale on the 68060 instead of with soft3D\n"
	    "  -noarm        MPEG-1 files decoded on the 68060, not on the Z3660's ARM\n"
	    "  -paula        MPEG-1 on the ARM: sound through Paula, not the board's HDMI\n"
	    "keys: q quit, f fullscreen, p/space pause, m mute, Left/Right -/+10 s,\n"
	    "      Down/Up -/+60 s, PgDn/PgUp -/+600 s, 1 original size, 2 double, h half,\n"
	    "      i info window, I statistics; double-click fullscreen, right click seeks\n");
	exit(2);
}

#ifdef VPLAY_ZZMPEG
static void arm_signal(int sig)
{
	zzm_close();			/* the overlay off */
	zzav_close();
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}
#endif

#ifdef VPLAY_ZZMPEG
#define NET_LEAD 3.0			/* seconds of a stream's sound before it starts */
#define NET_WAIT 8.0			/* ... or after this long */
#define NET_T "&t="			/* amixcast's URL: the start time */

/* the same URL from t seconds on */
static void net_url(char *out, int n, double t)
{
	const char *p = strstr(net_url0, NET_T), *e;
	int head = p ? (int)(p - net_url0) : (int)strlen(net_url0);

	e = p ? strchr(p + 1, '&') : NULL;
	snprintf(out, (size_t)n, "%.*s%s&t=%d", head, net_url0, e ? e : "", (int)t);
}

/* core 1's player once more, at t seconds: 1 if it runs */
static int av_reopen(double t)
{
	char url[1024];
	int v, a, i, x, y;
	Window child;

	if (t < 0.0)
		t = 0.0;
	zzav_close();
	if (strstr(net_url0, "/stream?")) {	/* amixcast starts a new stream */
		net_url(url, sizeof url, t);
		avformat_close_input(&fmt);
		if (avformat_open_input(&fmt, url, NULL, NULL) < 0)
			return 0;
		fmt->probesize = 256 * 1024;
		if (avformat_find_stream_info(fmt, NULL) < 0)
			return 0;
		net_t0 = t;
		net_c0 = -1.0;
	} else {
		if (t < start_pts)
			t = start_pts;
		if (av_seek_frame(fmt, -1, (int64_t)(t * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0)
			return 0;
	}
	v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	a = opt_noaudio || opt_paula ? -1 : av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child);
	if (v < 0 || zzav_open(fmt, v, a, x, y, 0, 0, opt_stats) != 0)
		return 0;
	for (i = 0; i < (int)fmt->nb_streams; i++)
		fmt->streams[i]->discard = zzav_stream(i) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
	if (muted)
		zzav_volume(0);
	arm_place();
	return 1;
}

/* core 1 decodes: hand it the packets, keep the window, until it has played everything */
static void av_play(void)
{
	AVPacket *p = av_packet_alloc();
	int have = 0, eof = 0, n;
	double t_stat = now_s();
	char buf[512];
	/* a stream: core 1 waits until NET_LEAD seconds of it are in the ring,
	   or the sound starts with nothing behind it and has gaps */
	const int net = fmt->url && strstr(fmt->url, "://") != NULL;
	int hold = net;
	double t_hold = now_s();
	double first = -1.0, last = -1.0;

	if (hold)
		zzav_pause(1);
	while (!quit && p) {
		events();
		if (quit)
			break;
		if (av_seek_on) {
			av_seek_on = 0;
			if (have)
				av_packet_unref(p);
			have = eof = 0;
			if (!(net && !strstr(net_url0, "/stream?")) && !av_reopen(av_seek_t))
				break;
			hold = net;
			t_hold = now_s();
			first = last = -1.0;
			if (hold)
				zzav_pause(1);
			else
				zzav_pause(paused);
		}
		for (n = 0; n < 64 && !eof; n++) {
			if (!have) {
				if (av_read_frame(fmt, p) < 0) {
					eof = 1;
					zzav_eof();
					break;
				}
				if (!zzav_stream(p->stream_index)) {
					av_packet_unref(p);
					continue;
				}
				have = 1;
			}
			if (!zzav_send(p))
				break;		/* the ring is full: core 1 is ahead enough */
			if (p->pts != AV_NOPTS_VALUE &&
			    fmt->streams[p->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				last = p->pts * av_q2d(fmt->streams[p->stream_index]->time_base);
				if (first < 0.0) {
					first = last;
					if (net)
						net_c0 = first;
				}
			}
			av_packet_unref(p);
			have = 0;
		}
		if (hold && (eof || (first >= 0.0 && last - first >= NET_LEAD) ||
			     now_s() - t_hold >= NET_WAIT)) {
			hold = 0;
			zzav_pause(paused);
		}
		if (zzav_done())
			break;
		if (opt_stats && now_s() - t_stat >= 2) {
			zzav_stats(buf, sizeof buf);
			fprintf(stderr, "vplay: %.1f s buffered, %s\n", last - zzav_clock(), buf);
			t_stat = now_s();
		}
		wait_until(now_s() + 0.02);
	}
	if (opt_stats) {
		zzav_stats(buf, sizeof buf);
		fprintf(stderr, "vplay: %s\n", buf);
	}
	zzav_counts(&n_shown, &n_dropped);	/* the final line counts core 1's */
	av_packet_free(&p);
}
#endif

int main(int argc, char **argv)
{
	const char *file = NULL, *title;
	int i, opt_fs = 0, opt_w = 0, opt_h = 0;
	double opt_ss = 0;

	last = av_frame_alloc();
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-x2"))
			opt_scale = 2;
		else if (!strcmp(argv[i], "-fs"))
			opt_fs = 1;
		else if (!strcmp(argv[i], "-x") && i + 1 < argc)
			opt_w = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-y") && i + 1 < argc)
			opt_h = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-ss") && i + 1 < argc)
			opt_ss = atof(argv[++i]);
		else if (!strcmp(argv[i], "-autoexit"))
			;			/* ffplay's; vplay always ends with the file */
		else if (!strcmp(argv[i], "-noaudio") || !strcmp(argv[i], "-an"))
			opt_noaudio = 1;
		else if (!strcmp(argv[i], "-stereo"))
			opt_stereo = 1;
		else if (!strcmp(argv[i], "-stats"))
			opt_stats = 1;
		else if (!strcmp(argv[i], "-noaccel"))
			opt_noaccel = 1;
		else if (!strcmp(argv[i], "-nos3d"))
			opt_nos3d = 1;
		else if (!strcmp(argv[i], "-noarm"))
			opt_noarm = 1;
		else if (!strcmp(argv[i], "-paula"))
			opt_paula = 1;
		else if (!strcmp(argv[i], "-nosync"))
			opt_nosync = opt_noaudio = 1;
		else if (argv[i][0] == '-' || file)
			usage();
		else
			file = argv[i];
	}
	if (!file)
		usage();
	signal(SIGPIPE, SIG_IGN);
	av_log_set_level(opt_stats ? AV_LOG_WARNING : AV_LOG_ERROR);

	net_url0 = file;
	{
		const char *p = strstr(file, NET_T);

		if (p && strstr(file, "/stream?"))
			net_t0 = atof(p + 3);
	}
	if (avformat_open_input(&fmt, file, NULL, NULL) < 0) {
		fprintf(stderr, "vplay: cannot open %s\n", file);
		return 1;
	}
	fmt->probesize = 256 * 1024;
	if (avformat_find_stream_info(fmt, NULL) < 0) {
		fprintf(stderr, "vplay: %s: no stream information\n", file);
		return 1;
	}
	vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	aidx = opt_noaudio ? -1 : av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	if (vidx >= 0 && !(vdec = open_decoder(vidx))) {
		fprintf(stderr, "vplay: no decoder for the video\n");
		vidx = -1;
	}
	if (aidx >= 0 && !(adec = open_decoder(aidx))) {
		fprintf(stderr, "vplay: no decoder for the sound\n");
		aidx = -1;
	}
	if (vidx < 0 && aidx < 0) {
		fprintf(stderr, "vplay: %s: nothing to play\n", file);
		return 1;
	}
	start_pts = fmt->start_time == AV_NOPTS_VALUE ? 0 : fmt->start_time / (double)AV_TIME_BASE;
	info_prepare(file);
	if (opt_stats)
		av_dump_format(fmt, 0, file, 0);

#ifdef VPLAY_ZZMPEG
	if (vdec && !opt_noarm && !opt_nosync && vdec->codec_id == AV_CODEC_ID_MPEG1VIDEO &&
	    !strcmp(fmt->iformat->name, "mpeg")) {
		use_arm = 1;
		opt_noaccel = opt_nos3d = 1;	/* vplay draws nothing: keep VRAM and soft3D free */
	}
	/* H.264 is core 1's if its player is there (zzav_open says) */
	if (vdec && !use_arm && !opt_noarm && !opt_nosync && vdec->codec_id == AV_CODEC_ID_H264)
		opt_noaccel = opt_nos3d = 1;
#endif
	if (vdec) {
		AVStream *st = fmt->streams[vidx];
		AVRational fr = av_guess_frame_rate(fmt, st, NULL);

		vtb = av_q2d(st->time_base);
		if (fr.num && fr.den)
			vframe_dur = av_q2d(av_inv_q(fr));
		vw = vdec->width & ~1;
		vh = vdec->height & ~1;
		vf = av_frame_alloc();
		title = strrchr(file, '/') ? strrchr(file, '/') + 1 : file;
		{
			int w = vw * opt_scale, h = vh * opt_scale;

			if (opt_w && opt_h) {
				w = opt_w;
				h = opt_h;
			} else if (opt_w) {
				w = opt_w;
				h = (int)((long)opt_w * vh / vw);
			} else if (opt_h) {
				h = opt_h;
				w = (int)((long)opt_h * vw / vh);
			}
			x_open(title, w, h);
		}
		if (opt_fs)
			set_fullscreen(1);
	}
#ifdef VPLAY_ZZMPEG
	if (use_arm) {
		Window child;
		int x, y;

		XSync(dpy, False);
		XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child);
		if (zzm_open(file, x + (ww - vw) / 2, y + (wh - vh) / 2, opt_stats) == 0) {
			decoder_name = "the Z3660 firmware's MPEG-1 decoder (ARM core 0)";
			atexit(zzm_close);
			signal(SIGINT, arm_signal);
			signal(SIGTERM, arm_signal);
			signal(SIGHUP, arm_signal);
			if (fmt->duration > 0 && avio_size(fmt->pb) > 0)
				zzm_rate((long)(avio_size(fmt->pb) * (double)AV_TIME_BASE / fmt->duration * 1.1));
			/* the ARM decodes: its packets are skipped by the demuxer */
			fmt->streams[vidx]->discard = AVDISCARD_ALL;
			avcodec_free_context(&vdec);
			vidx = -1;
			/* the board plays the sound on HDMI as well, in sync: the 68060 leaves it */
			if (adec && !opt_paula) {
				avcodec_free_context(&adec);
				fmt->streams[aidx]->discard = AVDISCARD_ALL;
				aidx = -1;
			}
			if (zzm_vscale() > 1 && !fullscreen)	/* the overlay shows it taller */
				XResizeWindow(dpy, win, ww, wh * zzm_vscale());
			XClearWindow(dpy, win);
		} else
			use_arm = 0;		/* not on this machine: the 68060 decodes */
	}
	if (vdec && !use_arm && !opt_noarm && !opt_nosync && vdec->codec_id == AV_CODEC_ID_H264) {
		Window child;
		int x, y, ow, oh, vsc;

		XSync(dpy, False);
		XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child);
		if (zzav_open(fmt, vidx, opt_paula ? -1 : aidx, x, y, 0, 0, opt_stats) == 0) {
			use_av = 1;
			decoder_name = "the Z3660's ARM core 1 (picture in the overlay, sound on HDMI)";
			atexit(zzav_close);
			signal(SIGINT, arm_signal);
			signal(SIGTERM, arm_signal);
			signal(SIGHUP, arm_signal);
			for (i = 0; i < (int)fmt->nb_streams; i++)
				if (!zzav_stream(i))
					fmt->streams[i]->discard = AVDISCARD_ALL;
			avcodec_free_context(&vdec);
			if (adec) {		/* core 1 plays the sound (AAC), or there is none */
				avcodec_free_context(&adec);
				aidx = -1;
			}
			zzav_size(&ow, &oh, &vsc);
			if (!fullscreen)
				XResizeWindow(dpy, win, ow, oh * vsc);
			XClearWindow(dpy, win);
		}
	}
#endif
	if (adec) {
		atb = av_q2d(fmt->streams[aidx]->time_base);
		audio_open();
		if (afd < 0) {			/* no sound after all: ignore its packets */
			avcodec_free_context(&adec);
			aidx = -1;
		}
	}

	if (opt_ss > 0)
		seek_to(start_pts + opt_ss);

#ifdef VPLAY_ZZMPEG
	if (use_av) {
		av_play();
		zzav_close();
		quit = 1;
	}
#endif
	while (!quit) {
		double now;

		events();
		if (quit)
			break;
		if (paused) {
			wait_until(now_s() + 0.05);
			continue;
		}
		stats();
		audio_pump();
#ifdef VPLAY_ZZMPEG
		if (use_arm && zzm_pump())
			continue;
#endif

		/* keep the queues and the sound ring filled */
		if ((vdec && !vf_ready && !vq.n) || (adec && afd >= 0 && !aq.n && aring_secs() < AFILL)) {
			if (read_one())
				continue;
		}
		audio_decode();

		if (vdec && !vf_ready && !vdec_eof) {
			if (!video_decode())
				continue;
		}

		/* the clock starts with the first frame (or the first sound) */
		if (!anchored) {
			/* fill the sound first: decoding it after the clock starts makes the first frames late */
			if (adec && afd >= 0 && aring_secs() < AFILL / 2 && !adec_eof && !(eof_read && !aq.n)) {
				if (!read_one())
					audio_decode();
				continue;
			}
#ifdef VPLAY_ZZMPEG
			/* the sound starts with the ARM's first picture */
			if (use_arm) {
				int pw, ph;

				if (!zzm_size(&pw, &ph)) {
					if (zzm_done())
						break;	/* the board showed nothing */
					wait_until(now_s() + 0.02);
					continue;
				}
				/* the sound where the ARM's picture is: its time counts
				   from the first frame, the file's from start_pts */
				base = now_s() - (start_pts + zzm_time());
				if (getenv("VPLAY_DEBUG"))
					fprintf(stderr, "vplay: ARM picture up: sound from %.3f (%d bytes held), video %.3f, start %.3f\n",
					    aring_pts, aring_len, zzm_time(), start_pts);
				anchored = 1;
				dev_end = 0;
				continue;
			}
#endif
			/* a 0.2 s head start: the window maps and the first frames are the costliest */
			if (vf_ready)
				base = now_s() + 0.2 - vf_pts;
			else if (aring_len)
				base = now_s() + 0.2 - aring_pts;
			else if (eof_read && vdec_eof)
				break;
			else
				continue;
			anchored = 1;
			dev_end = 0;
		}

		if (vf_ready) {
			now = now_s();
			lag = now - (base + vf_pts);
			if (opt_nosync || lag >= 0) {
				/*
				 * Late by more than a frame: drop it -- unless nothing was shown
				 * for a while, or the decoder is down to keyframes: then it is
				 * the only picture there will be for a while (ordinary files
				 * have a keyframe every few seconds, x264's default every 10).
				 */
				if (!opt_nosync && lag > vframe_dur * 1.5 && n_dropped % 8 != 7 &&
				    skip_level != AVDISCARD_NONKEY && !vf->key_frame) {
					n_dropped++;
				} else {
					show(vf);
					n_shown++;
				}
				av_frame_unref(vf);
				vf_ready = 0;
				if (opt_nosync)
					base = now_s() - vf_pts;
				else if (lag > 1.0)
					set_skip(AVDISCARD_NONKEY);
				else if (lag > 0.25 && skip_level == AVDISCARD_DEFAULT)
					set_skip(AVDISCARD_NONREF);
				else if (lag < 0.3 && skip_level == AVDISCARD_NONKEY)
					set_skip(AVDISCARD_NONREF);	/* caught up with a keyframe */
				else if (lag < 0.08)
					set_skip(AVDISCARD_DEFAULT);
				continue;
			}
			wait_until(base + vf_pts);
			continue;
		}

#ifdef VPLAY_ZZMPEG
		if (use_arm) {			/* the picture is the ARM's: wait for both to end */
			if (zzm_done() && (afd < 0 || (eof_read && !aring_len && (adec_eof || !adec) &&
			    now_s() >= dev_end)))
				break;
			/* the sound's packets come between the video's (dropped): a
			   sleep per packet would read too few of them */
			if (adec && afd >= 0 && !eof_read && aring_secs() < AFILL)
				continue;
			wait_until(now_s() + 0.02);
			continue;
		}
#endif
		/* no more video: let the sound finish */
		if ((!vdec || vdec_eof) && eof_read) {
			if (afd < 0 || (!aring_len && (adec_eof || !adec) && now_s() >= dev_end))
				break;
		}
		wait_until(now_s() + 0.01);	/* nothing to do right now */
	}

	if (afd >= 0) {
		double d = dev_end - now_s();

		if (!quit && d > 0)
			usleep((unsigned long)(d * 1e6));
		close(afd);
	}
	if (use_shm) {
		XShmDetach(dpy, &si);
		shmdt(si.shmaddr);
	}
	if (dpy)
		XCloseDisplay(dpy);
	if (opt_stats)
		fprintf(stderr, "vplay: %ld frames shown, %ld dropped\n", n_shown, n_dropped);
	return 0;
}
