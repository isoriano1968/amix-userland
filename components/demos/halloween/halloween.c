/*
 * halloween.c -- the Halloween intro for AMIX: the 060 only asks for frames,
 * every pixel is computed on the Z3660's core 1 (spook.c in amix-gate's
 * firmware/core1/zzgl), to Halloween Bass by dilik.  libmikmod mixes
 * here and the sound goes to the board's HDMI through zzgl's stream; what is
 * heard sets the pumpkin's beat.
 *
 *   halloween [module]     ESC or q quits
 *
 * HALLOWEEN_SECONDS=n stops after n seconds and prints the frame rate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "zzgl_enc.h"
#include "zzgl_platform.h"
#include <mikmod.h>

#define W 640
#define H 480

#define BEATS 64
#define HEARD 0.5f                     /* the driver mixes this far ahead of the speaker */

static Display *dpy;
static MODULE *module;
static struct { float t, beat; } beats[BEATS];       /* what was mixed when */
static int beat_n;

static float
beat_now(void)
{
   if (!module || !module->sngspd)
      return 0;
   return ((module->patpos & 3) + (float)module->vbtick / module->sngspd) / 4;
}


/* the module on the HDMI: 0, or the reason on stderr */
static int
start_music(const char *file)
{
   MikMod_RegisterDriver(&drv_hdmi);
   MikMod_RegisterAllLoaders();
   md_device = MikMod_DriverFromAlias((CHAR *)"hdmi");
   if (MikMod_Init("")) {
      fprintf(stderr, "halloween: %s\n", MikMod_strerror(MikMod_errno));
      return -1;
   }
   module = Player_Load((CHAR *)file, 32, 0);
   if (!module) {
      fprintf(stderr, "halloween: %s: %s\n", file, MikMod_strerror(MikMod_errno));
      return -1;
   }
   module->wrap = 1;
   Player_Start(module);
   return 0;
}

static float
now(void)
{
   static struct timeval t0;
   struct timeval t;

   gettimeofday(&t, NULL);
   if (!t0.tv_sec)
      t0 = t;
   return (float)(t.tv_sec - t0.tv_sec) + (float)(t.tv_usec - t0.tv_usec) * 1e-6f;
}

static void
quit(int code)
{
   if (module) {
      Player_Stop();
      MikMod_Exit();
   }
   zzgl_platform_close();
   exit(code);
}

static void
on_signal(int s)
{
   (void)s;
   quit(1);
}

int
main(int argc, char **argv)
{
   const char *file = argc > 1 ? argv[1] : "halloween_bass.it";
   const char *e = getenv("HALLOWEEN_SECONDS");
   const float seconds = e ? (float)atof(e) : 0;
   XSetWindowAttributes swa;
   XSizeHints hints;
   XEvent ev;
   Window win, child;
   int scr, x, y;
   long frames = 0;
   float t0, next_look = 0;

   signal(SIGTERM, on_signal);
   signal(SIGINT, on_signal);
   dpy = XOpenDisplay(NULL);
   if (!dpy) {
      fprintf(stderr, "halloween: cannot open the X display\n");
      return 1;
   }
   scr = DefaultScreen(dpy);
   swa.border_pixel = 0;
   swa.background_pixel = BlackPixel(dpy, scr);
   swa.event_mask = KeyPressMask | StructureNotifyMask | ExposureMask;
   win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, CopyFromParent, InputOutput, CopyFromParent,
                       CWBorderPixel | CWBackPixel | CWEventMask, &swa);
   hints.flags = PSize | PMinSize | PMaxSize;
   hints.width = hints.min_width = hints.max_width = W;
   hints.height = hints.min_height = hints.max_height = H;
   XSetStandardProperties(dpy, win, "Happy Halloween", "Halloween", None, NULL, 0, &hints);
   XMapWindow(dpy, win);
   do
      XNextEvent(dpy, &ev);
   while (ev.type != MapNotify);
   if (!XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &x, &y, &child))
      x = y = 0;
   if (zzgl_platform_open(dpy, win, x, y, W, H) != 0) {
      fprintf(stderr, "halloween: no GL on the Z3660's core 1\n");
      return 1;
   }
   start_music(file);
   t0 = now();
   for (;;) {
      const float t = now() - t0;

      while (XPending(dpy)) {
         XNextEvent(dpy, &ev);
         if (ev.type == KeyPress) {
            const KeySym k = XLookupKeysym(&ev.xkey, 0);

            if (k == XK_Escape || k == XK_q)
               quit(0);
         }
      }
      /* follow the window: dtwm moves its frame without telling us */
      if (t >= next_look) {
         int nx, ny;

         next_look = t + 0.2f;
         if (XTranslateCoordinates(dpy, win, RootWindow(dpy, scr), 0, 0, &nx, &ny, &child) &&
             (nx != x || ny != y)) {
            x = nx;
            y = ny;
            zzgl_platform_place(x, y);
         }
      }
      if (module)
         MikMod_Update();
      /* the beat of what is heard now, mixed a little while ago */
      {
         float heard = t / 0.42f - (int)(t / 0.42f);
         int k;

         beats[beat_n % BEATS].t = t;
         beats[beat_n % BEATS].beat = beat_now();
         beat_n++;
         if (module)
            for (k = 1; k < BEATS && k <= beat_n; k++)
               if (beats[(beat_n - k) % BEATS].t <= t - HEARD) {
                  heard = beats[(beat_n - k) % BEATS].beat;
                  break;
               }
         zzgl_effect(t, heard);
      }
      zzgl_platform_frame();
      frames++;
      if (seconds > 0 && t >= seconds) {
         printf("halloween: %ld frames in %.1f s: %.1f fps\n", frames, t, frames / t);
         quit(0);
      }
   }
}
