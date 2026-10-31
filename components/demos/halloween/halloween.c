/*
 * halloween.c -- the Halloween intro for AMIX: the 060 only asks for frames,
 * every pixel is computed on the Z3660's core 1 (spook.c in amix-gate's
 * firmware/core1/zzgl), to Halloween Bass by dilik.  libmikmod mixes
 * here and the sound goes to the board's HDMI through zzgl's stream; what is
 * heard sets the pumpkin's beat.
 *
 *   halloween [module]     ESC or q quits
 *
 * HALLOWEEN_SECONDS=n stops after n seconds and prints the frame rate;
 * HALLOWEEN_DUMP=file keeps the mix (16-bit stereo, big-endian, 24 kHz).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "zzgl_enc.h"
#include "zzgl_platform.h"
#include "mikmod_internals.h"

#define W 640
#define H 480

#define RATE 24000
#define BYTES_S (RATE * 4)
#define LEAD (BYTES_S / 2)             /* mixed ahead of what is heard; core 1 reads 1/8 s ahead too */
#define CHUNK 1024                     /* bytes mixed in one go: 1/96 s */
#define BEATS 256

static Display *dpy;
static MODULE *module;
static unsigned long mixed;            /* stream bytes written */
static int pause_us;                   /* HALLOWEEN_PAUSE=us between frames; -1 no picture */
static int tone;                       /* HALLOWEEN_TONE=1: 440 Hz instead of the module */
static FILE *dump;                     /* HALLOWEEN_DUMP=file: the mix as written */
static float beats[BEATS];             /* where in the beat each chunk was mixed */

static float
beat_now(void)
{
   if (!module || !module->sngspd)
      return 0;
   return ((module->patpos & 3) + (float)module->vbtick / module->sngspd) / 4;
}

static void
HD_Update(void)
{
   static short buf[CHUNK / 2];
   const unsigned long want = zzgl_sound_pos() + LEAD;

   while (mixed < want) {
      if (tone) {
         static float a;
         int i;

         for (i = 0; i < CHUNK / 2; i += 2) {
            buf[i] = buf[i + 1] = (short)(3000 * sin(a));
            a += 2 * 3.14159265f * 440 / RATE;
            if (a > 6.2831853f)
               a -= 6.2831853f;
         }
      } else {
         VC_WriteBytes((SBYTE *)buf, CHUNK);
      }
      zzgl_sound_write(mixed, buf, CHUNK);
      if (dump)
         fwrite(buf, 1, CHUNK, dump);
      beats[mixed / CHUNK % BEATS] = beat_now();
      mixed += CHUNK;
   }
}

static BOOL HD_IsThere(void) { return 1; }

static int
HD_Init(void)
{
   md_mixfreq = RATE;
   md_mode |= DMODE_16BITS | DMODE_STEREO | DMODE_SOFT_MUSIC;
   mixed = zzgl_sound_pos();
   return VC_Init();
}

static MDRIVER drv_hdmi = {
   NULL, "HDMI", "The board's HDMI through zzgl", 255, 255, "hdmi", NULL, HD_IsThere,
   VC_SampleLoad, VC_SampleUnload, VC_SampleSpace, VC_SampleLength, HD_Init, VC_Exit, NULL,
   VC_SetNumVoices, VC_PlayStart, VC_PlayStop, HD_Update, NULL, VC_VoiceSetVolume,
   VC_VoiceGetVolume, VC_VoiceSetFrequency, VC_VoiceGetFrequency, VC_VoiceSetPanning,
   VC_VoiceGetPanning, VC_VoicePlay, VC_VoiceStop, VC_VoiceStopped, VC_VoiceGetPosition,
   VC_VoiceRealVolume
};

/* the module on the HDMI: 0, or the reason on stderr */
static int
start_music(const char *file)
{
   if (zzgl_sound_open(2) < 0) {
      fprintf(stderr, "halloween: no HDMI sound\n");
      return -1;
   }
   MikMod_RegisterDriver(&drv_hdmi);
   MikMod_RegisterAllLoaders();
   md_device = 1;
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
      zzgl_sound_close();
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
   tone = getenv("HALLOWEEN_TONE") != NULL;
   if ((e = getenv("HALLOWEEN_PAUSE")))
      pause_us = atoi(e);
   if ((e = getenv("HALLOWEEN_DUMP")))
      dump = fopen(e, "wb");
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
      if (pause_us < 0) {             /* tests: no picture */
         usleep(10000);
         frames++;
         if (t >= seconds)
            quit(0);
         continue;
      }
      zzgl_effect(t, module ? beats[zzgl_sound_pos() / CHUNK % BEATS] : t / 0.517f - (int)(t / 0.517f));
      zzgl_platform_frame();
      if (pause_us)
         usleep(pause_us);
      frames++;
      if (seconds > 0 && t >= seconds) {
         printf("halloween: %ld frames in %.1f s: %.1f fps\n", frames, t, frames / t);
         quit(0);
      }
   }
}
