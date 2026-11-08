/*
 * c1q3 -- Quake III Arena (ioquake3) on the Z3660's core 1 from AMIX: the
 * whole engine and its GL (Mesa softpipe) run on the board's second ARM core
 * (z3660ARM_Core_q3.bin); the 060 only holds the X window the picture lies
 * over, passes keyboard and mouse on, and serves the files.
 *
 *   c1q3 [options] [engine arguments: +set ..., +map q3dm1, +exec ...]
 *     -base DIR        the game data (default /opt/ioq3): DIR/GAME/... is /base/GAME/...
 *     -game NAME       the game directory (default demoq3, as com_basegame)
 *     -home DIR        the user's files (default $HOME/.q3a): /home/..., written ones come back
 *     -width W -height H   the picture (default 640x480)
 *     -at X Y          the window's place on the screen
 *     -volume N        0-256
 *     -exec SECS CMD   a console command SECS after the engine started (up to 8;
 *                      e.g. -exec 200 "setviewpos 1052 1432 24 135" -exec 300 quit)
 *     -core FILE       the core image (default /root/z3660ARMCores/z3660ARM_Core_q3.bin; root only)
 *     -v               core 1's messages (the engine's console) on stderr
 *
 * Installed setuid root (the board is root's): it maps the board, loads
 * the core and starts the overlay and the HDMI sound as root, then runs as
 * the caller before it opens the display or any file.  Core 1 runs one job
 * at a time: while the game runs there is no GL, H.264 or crypto offload
 * for other programs (sshd falls back to the 060).
 *
 * Every pak and file of the game directory goes into core 1's memory at the
 * start (the demo's pak0.pk3 is 46 MB); so do the user's files, and what
 * the engine writes there (q3config.cfg, screenshots) comes back, under
 * the home directory only.  F10 quits (bound by c1q3), as does closing the
 * window.  A status line goes to standard output every second:
 *   status FRAMES FRAME_MS_AVG FRAME_MS_MAX UNDERRUNS HEAP_MB STATE
 * Exit 3: core 1 runs a game already.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "bench.h"
#include "c1q3_board.h"
#include "keycodes.h"

extern int munmap();
extern int select();                    /* AMIX: in sys/time.h only for some */

#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL
#define PAGE                4096UL

#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define REG_AUDIO_PARAM     0x1EC
#define REG_AUDIO_VAL       0x1F0
#define CMD_INIT            1
#define CMD_CLOSE           5
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00010000UL
#define SHARED_OFF          0x07B00000UL

#define DEFAULT_CORE        "/root/z3660ARMCores/z3660ARM_Core_q3.bin"
#define MAX_FILES           400
#define HOME_FILE_MAX       (8UL << 20)     /* bigger files in the home stay out */

static int fd = -1, verbose, overlay_on, ovl_x, ovl_y, pic_w = 640, pic_h = 480, vs = 1;
static struct zz9k_info info;
static volatile unsigned long *ctl, *c1ld, *pg;
static volatile unsigned char *pgb;
static unsigned char *win, *shared;
static unsigned long log_seen, in_head, in_tail_seen;
static char home[1024];
static int nfiles;
static struct { double at; const char *cmd; } execs[8];
static int nexec;

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void reg32(unsigned off, unsigned long v)
{
    struct zz9k_reg r;

    r.offset = off;
    r.value = (unsigned short)(v >> 16);
    ioctl(fd, ZZ9KIOC_SETREG, &r);
    r.offset = off + 2;
    r.value = (unsigned short)v;
    ioctl(fd, ZZ9KIOC_SETREG, &r);
}

static void *map(unsigned long off, unsigned long len)
{
    void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

    return p == (void *)-1 ? NULL : p;
}

/* core 1's messages since the last look (the log ring at the window's end) */
static void show_log(void)
{
    unsigned long head = pg[C1Q3_W_LOG_HEAD];

    if (head - log_seen > C1Q3_LOG_LEN)
        log_seen = head - C1Q3_LOG_LEN;
    for (; log_seen != head; log_seen++)
        if (verbose)
            fputc(win[C1Q3_CHUNK_MAX + log_seen % C1Q3_LOG_LEN], stderr);
}

/* the last lines of it, after a failure */
static void show_log_tail(void)
{
    unsigned long head = pg[C1Q3_W_LOG_HEAD], from = head > 2048 ? head - 2048 : 0, i;

    for (i = from; i != head; i++)
        fputc(win[C1Q3_CHUNK_MAX + i % C1Q3_LOG_LEN], stderr);
    fputc('\n', stderr);
}

static void show_sample(FILE *f)
{
    int k;

    if (!pg[C1Q3_W_SAMPLE])
        return;
    fprintf(f, "c1q3: core 1 sample %lu: pc %08lx lr %08lx sp %08lx stack",
            pg[C1Q3_W_SAMPLE], pg[C1Q3_W_SAMPLE + 1], pg[C1Q3_W_SAMPLE + 2], pg[C1Q3_W_SAMPLE + 3]);
    for (k = C1Q3_W_SAMPLE + 4; k < C1Q3_W_SAMPLE_END && pg[k]; k++)
        fprintf(f, " %08lx", pg[k]);
    fprintf(f, "\n");
    fflush(f);
}

static void set_text(const char *s)
{
    size_t i;

    for (i = 0; s[i] && i < C1Q3_TEXT_LEN - 1; i++)
        pgb[C1Q3_TEXT_OFF + i] = (unsigned char)s[i];
    pgb[C1Q3_TEXT_OFF + i] = 0;
}

static void get_text(char *s, size_t n)
{
    size_t i;

    for (i = 0; i + 1 < n && i < C1Q3_TEXT_LEN; i++)
        if (!(s[i] = (char)pgb[C1Q3_TEXT_OFF + i]))
            return;
    s[i] = 0;
}

/* a command, and its end (core 1 clears it); polls slowly: 68k reads of
   board memory stall core 0 */
static long command(unsigned long cmd, double timeout)
{
    double t0 = now();

    pg[C1Q3_W_CMD] = cmd;
    while (pg[C1Q3_W_CMD] != C1Q3_CMD_NONE) {
        if (now() - t0 > timeout)
            return -100;
        usleep(5000);
    }
    return (long)pg[C1Q3_W_RESULT];
}

/* the core, loaded afresh through armbench for every run: an idle one may
   still hold the files of a run that failed before START */
static int core_ready(const char *core)
{
    char cmd[1200];
    double t0;

    sprintf(cmd, "/opt/amix/bin/armbench %s %s", ctl[W_MAGIC] == HB_MAGIC_READY ? "-reload" : "-load", core);
    if (verbose)
        fprintf(stderr, "c1q3: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "c1q3: %s failed\n", cmd);
        return -1;
    }
    for (t0 = now(); pg[C1Q3_W_MAGIC] != C1Q3_MAGIC; usleep(50000))
        if (now() - t0 > 10.0) {
            fprintf(stderr, "c1q3: the core did not come up (stage %lu)\n", ctl[W_STAGE]);
            return -1;
        }
    if (pg[C1Q3_W_VERSION] != C1Q3_VERSION) {
        fprintf(stderr, "c1q3: the core speaks protocol %06lx, not %06lx\n", pg[C1Q3_W_VERSION],
                (unsigned long)C1Q3_VERSION);
        return -1;
    }
    log_seen = pg[C1Q3_W_LOG_HEAD];
    return 0;
}

/* --- files in --- */
static int upload(const char *path, const char *cpath, unsigned long size, int rw)
{
    int f = open(path, O_RDONLY), r;
    unsigned long off = 0, n, got;
    long fi;

    if (f < 0) {
        fprintf(stderr, "c1q3: %s: %s\n", path, strerror(errno));
        return -1;
    }
    set_text(cpath);
    pg[C1Q3_W_SIZE] = size;
    pg[C1Q3_W_FLAGS] = rw ? C1Q3_FILE_RW : 0;
    if ((fi = command(C1Q3_CMD_OPEN, 10.0)) < 0) {
        fprintf(stderr, "c1q3: core 1 has no room for %s (%lu bytes, err %lu, heap %lu KB)\n", path, size,
                pg[C1Q3_W_ERR], pg[C1Q3_W_HEAP_KB]);
        close(f);
        return -1;
    }
    while (off < size) {
        n = size - off < C1Q3_CHUNK_MAX ? size - off : C1Q3_CHUNK_MAX;
        for (got = 0; got < n; got += (unsigned long)r)
            if ((r = read(f, win + got, (unsigned)(n - got > 262144 ? 262144 : n - got))) <= 0) {
                fprintf(stderr, "c1q3: %s: short read\n", path);
                close(f);
                return -1;
            }
        pg[C1Q3_W_FILE] = (unsigned long)fi;
        pg[C1Q3_W_OFF] = off;
        pg[C1Q3_W_LEN] = n;
        if (command(C1Q3_CMD_CHUNK, 10.0) != 0) {
            fprintf(stderr, "c1q3: core 1 refused a piece of %s\n", path);
            close(f);
            return -1;
        }
        off += n;
    }
    close(f);
    nfiles++;
    return 0;
}

/* a directory tree as DIR/... -> PREFIX/...; skips what the core cannot use */
static int upload_tree(const char *dir, const char *prefix, int rw, int depth, unsigned long *bytes)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    struct stat st;
    char path[1024], cpath[512];
    int bad = 0;

    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL && !bad) {
        const char *n = e->d_name;
        size_t len = strlen(n);

        if (n[0] == '.' || (len > 3 && !strcmp(n + len - 3, ".so")) || !strcmp(n, "ioq3.pid") ||
            (len > 5 && !strcmp(n + len - 5, ".part")))
            continue;
        if (strlen(dir) + len + 2 > sizeof(path) || strlen(prefix) + len + 2 > sizeof(cpath))
            continue;
        sprintf(path, "%s/%s", dir, n);
        sprintf(cpath, "%s/%s", prefix, n);
        if (stat(path, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 6)
                bad = upload_tree(path, cpath, rw, depth + 1, bytes) < 0;
        } else if (S_ISREG(st.st_mode)) {
            if (nfiles >= MAX_FILES) {
                fprintf(stderr, "c1q3: more than %d files: %s and the rest stay out\n", MAX_FILES, path);
                break;
            }
            if (rw && (unsigned long)st.st_size > HOME_FILE_MAX) {
                fprintf(stderr, "c1q3: %s: over %lu MB, stays out\n", path, HOME_FILE_MAX >> 20);
                continue;
            }
            if (verbose)
                fprintf(stderr, "c1q3: %s (%lu bytes)\n", cpath, (unsigned long)st.st_size);
            bad = upload(path, cpath, (unsigned long)st.st_size, rw) < 0;
            *bytes += (unsigned long)st.st_size;
        }
    }
    closedir(d);
    return bad ? -1 : 0;
}

/* --- written files back, under the home directory only --- */
static int make_dirs(char *path)
{
    char *s;

    for (s = path + 1; (s = strchr(s, '/')) != NULL; s++) {
        *s = 0;
        if (mkdir(path, 0755) < 0 && errno != EEXIST) {
            *s = '/';
            return -1;
        }
        *s = '/';
    }
    return 0;
}

static void flush(void)
{
    static char part[1110];
    static int part_fd = -1;
    int guard = 0;

    while (pg[C1Q3_W_DIRTY] && guard++ < 1000) {
        char cpath[C1Q3_TEXT_LEN], path[1100];
        unsigned long size, off, len;
        long r = command(C1Q3_CMD_FLUSH, 5.0);

        if (r < 0)
            break;
        get_text(cpath, sizeof(cpath));
        size = pg[C1Q3_W_SIZE];
        off = pg[C1Q3_W_OFF];
        len = pg[C1Q3_W_LEN];
        if (strncmp(cpath, "/home/", 6) || strstr(cpath, "/../") || strstr(cpath, "//") ||
            strlen(home) + strlen(cpath) + 8 > sizeof(path)) {
            fprintf(stderr, "c1q3: core 1 wrote %s: not under /home, ignored\n", cpath);
            continue;
        }
        sprintf(path, "%s/%s", home, cpath + 6);
        if (size == C1Q3_REMOVED) {
            if (unlink(path) < 0 && errno != ENOENT)
                fprintf(stderr, "c1q3: removing %s: %s\n", path, strerror(errno));
            continue;
        }
        if (off == 0) {                 /* a file starts: into NAME.part, renamed when complete */
            if (part_fd >= 0)
                close(part_fd);
            sprintf(part, "%s.part", path);
            make_dirs(part);
            part_fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if (part_fd < 0 || lseek(part_fd, (off_t)off, SEEK_SET) < 0 ||
            write(part_fd, win, (unsigned)len) != (int)len) {
            fprintf(stderr, "c1q3: writing %s: %s\n", path, strerror(errno));
            if (part_fd >= 0)
                close(part_fd);
            part_fd = -1;
            continue;
        }
        if (r == 0) {                   /* its last piece */
            close(part_fd);
            part_fd = -1;
            if (rename(part, path) < 0)
                fprintf(stderr, "c1q3: %s: %s\n", path, strerror(errno));
            else if (verbose)
                fprintf(stderr, "c1q3: wrote %s (%lu bytes)\n", path, size);
        }
    }
}

/* --- the overlay over the window, as c1uae (place before and after INIT) --- */
static void place(int x, int y)
{
    if (x + pic_w > info.width)
        x = info.width - pic_w;
    if (y + pic_h > info.height)
        y = info.height - pic_h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    ovl_x = x;
    ovl_y = y;
    reg32(REG_OVL_XSTART, (unsigned long)x);
    reg32(REG_OVL_YSTART, (unsigned long)y);
    reg32(REG_OVL_WIDTH, (unsigned long)(pic_w > info.width ? info.width : pic_w));
    reg32(REG_OVL_HEIGHT, (unsigned long)(pic_h > info.height ? info.height : pic_h));
    pg[C1Q3_W_OVL_ROW0] = (unsigned long)(y / vs);
}

static void overlay(int on)
{
    if (on && !overlay_on) {
        vs = info.width >= 1600 && !(c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FETCH)) ? 2 : 1;
        pg[C1Q3_W_OVL_MAXW] = (unsigned long)(pic_w > info.width ? info.width : pic_w);
        pg[C1Q3_W_OVL_MAXH] = (unsigned long)(pic_h > info.height ? info.height : pic_h);
        place(ovl_x, ovl_y);
        memset(shared, 0, 96);
        reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
        reg32(REG_MPEG_PARAM0 + 4, info.base);
        reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
        reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
        reg32(REG_MPEG_CMD, CMD_INIT);
        usleep(100000);
        place(ovl_x, ovl_y);
        pg[C1Q3_W_OVL_PITCH] = (unsigned long)info.width * 4;  /* core 1 draws from now on */
        overlay_on = 1;
    } else if (!on && overlay_on) {
        pg[C1Q3_W_OVL_PITCH] = 0;       /* core 1 stops drawing first */
        usleep(100000);
        reg32(REG_MPEG_CMD, CMD_CLOSE);
        overlay_on = 0;
    }
}

/* --- input into the ring --- */
static void event(unsigned long e)
{
    if (in_head - in_tail_seen >= C1Q3_IN_N - 8) {
        in_tail_seen = pg[C1Q3_W_IN_TAIL];      /* only when it might be full */
        if (in_head - in_tail_seen >= C1Q3_IN_N - 1)
            return;                             /* core 1 is not reading: dropped */
    }
    ((volatile unsigned long *)(pgb + C1Q3_IN_OFF))[in_head % C1Q3_IN_N] = e;
    in_head++;
    pg[C1Q3_W_IN_HEAD] = in_head;
}

static void key(int k, int down)
{
    if (k > 0 && k < MAX_KEYS)
        event((unsigned long)C1Q3_EV_KEY << 24 | (unsigned long)(down != 0) << 16 | (unsigned long)k);
}

static void chars(const char *buf, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if ((unsigned char)buf[i] >= 32 && buf[i] != 127)
            event((unsigned long)C1Q3_EV_CHAR << 24 | (unsigned char)buf[i]);
}

/* X keysyms to the engine's keys (as amix_input.c, the 060 port's) */
static int key_for(KeySym k)
{
    switch (k) {
    case XK_Escape:     return K_ESCAPE;
    case XK_Return:     return K_ENTER;
    case XK_KP_Enter:   return K_KP_ENTER;
    case XK_Tab:        return K_TAB;
    case XK_BackSpace:  return K_BACKSPACE;
    case XK_space:      return K_SPACE;
    case XK_Up:         return K_UPARROW;
    case XK_Down:       return K_DOWNARROW;
    case XK_Left:       return K_LEFTARROW;
    case XK_Right:      return K_RIGHTARROW;
    case XK_Shift_L: case XK_Shift_R:       return K_SHIFT;
    case XK_Control_L: case XK_Control_R:   return K_CTRL;
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return K_ALT;
    case XK_Insert:     return K_INS;
    case XK_Delete:     return K_DEL;
    case XK_Home:       return K_HOME;
    case XK_End:        return K_END;
    case XK_Prior:      return K_PGUP;
    case XK_Next:       return K_PGDN;
    case XK_Pause:      return K_PAUSE;
    case XK_KP_Home:    return K_KP_HOME;
    case XK_KP_Up:      return K_KP_UPARROW;
    case XK_KP_Prior:   return K_KP_PGUP;
    case XK_KP_Left:    return K_KP_LEFTARROW;
    case XK_KP_Begin:   return K_KP_5;
    case XK_KP_Right:   return K_KP_RIGHTARROW;
    case XK_KP_End:     return K_KP_END;
    case XK_KP_Down:    return K_KP_DOWNARROW;
    case XK_KP_Next:    return K_KP_PGDN;
    case XK_KP_Insert:  return K_KP_INS;
    case XK_KP_Delete:  return K_KP_DEL;
    case XK_KP_Add:     return K_KP_PLUS;
    case XK_KP_Subtract: return K_KP_MINUS;
    case XK_KP_Divide:  return K_KP_SLASH;
    case XK_KP_Multiply: return K_KP_STAR;
    case XK_grave: case XK_asciitilde: case XK_asciicircum: case XK_dead_circumflex:
                        return K_CONSOLE;
    }
    if (k >= XK_F1 && k <= XK_F12)
        return K_F1 + (int)(k - XK_F1);
    if (k >= XK_A && k <= XK_Z)
        return (int)(k - XK_A + 'a');
    if (k >= 32 && k < 127)
        return (int)k;
    return 0;
}

static Display *dpy;
static Window xwin;
static Atom wm_delete;
static Cursor blank;
static int grabbed, focused = 1, mapped;

/* the window's top left on the screen */
static void where(int *x, int *y)
{
    Window child;

    if (!XTranslateCoordinates(dpy, xwin, RootWindow(dpy, DefaultScreen(dpy)), 0, 0, x, y, &child))
        *x = *y = 0;
}

static void mouse(int want)
{
    if (want && !grabbed) {
        if (!blank) {
            Pixmap p = XCreatePixmap(dpy, xwin, 1, 1, 1);
            XColor c;

            memset(&c, 0, sizeof c);
            blank = XCreatePixmapCursor(dpy, p, p, &c, &c, 0, 0);
            XFreePixmap(dpy, p);
        }
        XDefineCursor(dpy, xwin, blank);
        if (XGrabPointer(dpy, xwin, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, xwin, None, CurrentTime) != GrabSuccess)
            return;
        XWarpPointer(dpy, None, xwin, 0, 0, 0, 0, pic_w / 2, pic_h / 2);
        grabbed = 1;
    } else if (!want && grabbed) {
        XUngrabPointer(dpy, CurrentTime);
        XUndefineCursor(dpy, xwin);
        grabbed = 0;
    }
}

/* the window's events; 1: it was closed */
static int x_events(void)
{
    XEvent ev;
    KeySym k;
    char buf[16];
    int n, kk;

    while (XPending(dpy)) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case KeyPress:
        case KeyRelease:
            /* a release followed at once by a press of the same key is the
               server's autorepeat: keep the key down, repeat the character */
            if (ev.type == KeyRelease && XPending(dpy)) {
                XEvent next;

                XPeekEvent(dpy, &next);
                if (next.type == KeyPress && next.xkey.keycode == ev.xkey.keycode &&
                    next.xkey.time == ev.xkey.time) {
                    XNextEvent(dpy, &next);
                    n = XLookupString(&next.xkey, buf, sizeof buf, &k, NULL);
                    key(key_for(k), 1);
                    chars(buf, n);
                    break;
                }
            }
            n = XLookupString(&ev.xkey, buf, sizeof buf, &k, NULL);
            kk = key_for(k);
            key(kk, ev.type == KeyPress);
            if (ev.type == KeyPress && kk != K_CONSOLE)
                chars(buf, n);
            break;
        case ButtonPress:
        case ButtonRelease:
            switch (ev.xbutton.button) {
            case 1: kk = K_MOUSE1; break;
            case 2: kk = K_MOUSE3; break;
            case 3: kk = K_MOUSE2; break;
            case 4: kk = K_MWHEELUP; break;
            case 5: kk = K_MWHEELDOWN; break;
            default: kk = 0; break;
            }
            key(kk, ev.type == ButtonPress);
            break;
        case MotionNotify:
            if (grabbed) {
                int dx = ev.xmotion.x - pic_w / 2, dy = ev.xmotion.y - pic_h / 2;

                if (dx || dy) {
                    if (dx > 2047) dx = 2047;
                    if (dx < -2048) dx = -2048;
                    if (dy > 2047) dy = 2047;
                    if (dy < -2048) dy = -2048;
                    event((unsigned long)C1Q3_EV_MOUSE << 24 | ((unsigned long)dx & 0xFFF) << 12 |
                          ((unsigned long)dy & 0xFFF));
                    XWarpPointer(dpy, None, xwin, 0, 0, 0, 0, pic_w / 2, pic_h / 2);
                }
            }
            break;
        case FocusIn:
            focused = 1;
            event((unsigned long)C1Q3_EV_FOCUS << 24 | 1);
            break;
        case FocusOut:
            focused = 0;
            mouse(0);
            event((unsigned long)C1Q3_EV_FOCUS << 24);
            break;
        case ConfigureNotify:
        case MapNotify:
            if (ev.type == MapNotify) {
                mapped = 1;
                overlay(1);
            }
            if (overlay_on) {
                int x, y;

                where(&x, &y);
                if (x != ovl_x || y != ovl_y)
                    place(x, y);
            }
            break;
        case UnmapNotify:
            mapped = 0;
            mouse(0);
            overlay(0);
            break;
        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == wm_delete)
                return 1;
            break;
        }
    }
    return 0;
}

static int open_window(void)
{
    XSetWindowAttributes swa;
    XSizeHints hints;
    int scr;

    if (!(dpy = XOpenDisplay(NULL))) {
        fprintf(stderr, "c1q3: cannot open the X display\n");
        return -1;
    }
    scr = DefaultScreen(dpy);
    swa.border_pixel = 0;
    swa.background_pixel = BlackPixel(dpy, scr);
    swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    xwin = XCreateWindow(dpy, RootWindow(dpy, scr), ovl_x, ovl_y, (unsigned)pic_w, (unsigned)pic_h, 0,
                         CopyFromParent, InputOutput, CopyFromParent, CWBorderPixel | CWBackPixel | CWEventMask,
                         &swa);
    hints.flags = PSize | PMinSize | PMaxSize | USPosition;
    hints.x = ovl_x;
    hints.y = ovl_y;
    hints.width = hints.min_width = hints.max_width = pic_w;
    hints.height = hints.min_height = hints.max_height = pic_h;
    XSetStandardProperties(dpy, xwin, "Quake III (core 1)", "Quake III", None, NULL, 0, &hints);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, xwin, &wm_delete, 1);
    XMapWindow(dpy, xwin);
    XFlush(dpy);
    return 0;
}

static volatile int got_signal;
static void on_signal(int s)
{
    (void)s;
    got_signal = 1;
}

static void usage(void)
{
    fprintf(stderr, "usage: c1q3 [-base DIR] [-game NAME] [-home DIR] [-width W -height H] [-at X Y]\n"
                    "            [-volume N] [-exec SECS CMD]... [-core FILE] [-v] [engine arguments: +set ...]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *core = DEFAULT_CORE, *base = "/opt/ioq3", *game = "demoq3", *homearg = NULL;
    char cmdline[C1Q3_TEXT_LEN], path[1100];
    long volume = 256;
    unsigned long bytes = 0, last_frames = 0;
    double t0, last_poll = 0, last_status = 0, still_since = 0, started;
    int i, len, ret = 0, hang_said = 0;

    ovl_x = ovl_y = -1;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-base") && i + 1 < argc) base = argv[++i];
        else if (!strcmp(argv[i], "-game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "-home") && i + 1 < argc) homearg = argv[++i];
        else if (!strcmp(argv[i], "-width") && i + 1 < argc) pic_w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-height") && i + 1 < argc) pic_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-at") && i + 2 < argc) { ovl_x = atoi(argv[i + 1]); ovl_y = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "-volume") && i + 1 < argc) volume = atol(argv[++i]);
        else if (!strcmp(argv[i], "-exec") && i + 2 < argc && nexec < 8) {
            execs[nexec].at = atof(argv[i + 1]);
            execs[nexec++].cmd = argv[i + 2];
            i += 2;
        }
        else if (!strcmp(argv[i], "-core") && i + 1 < argc) core = argv[++i];
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else usage();
    }
    if (pic_w < 320 || pic_w > 1280 || pic_h < 200 || pic_h > 1024 || strchr(game, '/') || !game[0])
        usage();
    len = sprintf(cmdline, "+set fs_basepath /base +set fs_homepath /home +set com_basegame %s "
                  "+set vm_cgame 0 +set vm_game 0 +set vm_ui 0 +set sv_pure 0 +set r_mode -1 "
                  "+set r_customwidth %d +set r_customheight %d +set s_mixahead 0.5 +bind F10 quit "
                  /* what fits core 1's ~224 MB with a 46 MB pak, whatever a q3config
                     of the 060 build says (hunk 256, sound 16 = 50 MB there); the
                     user's +set come after */
                  "+set com_hunkmegs 64 +set com_zoneMegs 24 +set com_soundMegs 8",
                  game, pic_w, pic_h);
    for (; i < argc; i++) {
        if (len + strlen(argv[i]) + 4 >= sizeof(cmdline)) {
            fprintf(stderr, "c1q3: the engine's command line is too long\n");
            return 2;
        }
        len += sprintf(cmdline + len, strchr(argv[i], ' ') ? " \"%s\"" : " %s", argv[i]);
    }

    if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("c1q3: /dev/zz9000");
        return 1;
    }
    ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE);
    c1ld = (volatile unsigned long *)map(C1LD_OFF, PAGE);
    pg = (volatile unsigned long *)map(C1Q3_OFF, PAGE);
    win = (unsigned char *)map(C1Q3_WIN_OFF, C1Q3_WIN_SIZE);
    shared = (unsigned char *)map(SHARED_OFF, PAGE);
    if (!ctl || !c1ld || !pg || !win || !shared) {
        perror("c1q3: mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    pgb = (volatile unsigned char *)pg;
    if (ovl_x < 0) {
        ovl_x = (info.width - pic_w) / 2;
        ovl_y = (info.height - pic_h) / 2;
    }
    if (getuid() != 0 && strcmp(core, DEFAULT_CORE)) {
        fprintf(stderr, "c1q3: -core is for root only\n");
        return 2;
    }
    putenv("PATH=/bin:/usr/bin:/opt/amix/bin");
    putenv("IFS= \t\n");
    /* one game at a time: another c1q3 that still lives keeps core 1 */
    if (pg[C1Q3_W_MAGIC] == C1Q3_MAGIC && pg[C1Q3_W_OWNER] && pg[C1Q3_W_OWNER] != (unsigned long)getpid() &&
        (pg[C1Q3_W_STATE] == C1Q3_RUNNING || pg[C1Q3_W_STATE] == C1Q3_LOADING) &&
        kill((pid_t)pg[C1Q3_W_OWNER], 0) == 0) {
        fprintf(stderr, "c1q3: the game is already running (c1q3 %lu)\n", pg[C1Q3_W_OWNER]);
        return 3;
    }
    if (core_ready(core) < 0)
        return 1;
    pg[C1Q3_W_OWNER] = (unsigned long)getpid();
    /* from here on as the caller: the files and the display are theirs */
    if (setgid(getgid()) < 0 || setuid(getuid()) < 0) {
        perror("c1q3: setuid");
        return 1;
    }
    if (homearg)
        strncpy(home, homearg, sizeof(home) - 1);
    else
        sprintf(home, "%.1000s/.q3a", getenv("HOME") ? getenv("HOME") : ".");
    mkdir(home, 0755);

    t0 = now();
    sprintf(path, "%.1000s/%s", base, game);
    {
        char cp[300];

        sprintf(cp, "/base/%s", game);
        if (upload_tree(path, cp, 0, 0, &bytes) < 0 || upload_tree(home, "/home", 1, 0, &bytes) < 0) {
            pg[C1Q3_W_OWNER] = 0;
            return 1;
        }
    }
    printf("c1q3: %d files, %.1f MB in %.1f s, core 1 heap left %lu MB\n", nfiles, bytes / 1048576.0,
           now() - t0, pg[C1Q3_W_HEAP_KB] >> 10);
    fflush(stdout);

    if (open_window() < 0) {
        pg[C1Q3_W_OWNER] = 0;
        return 1;
    }
    /* sound: the HDMI ring's DMA, as vplay starts it (the board's register
       through /dev/zz9000, which stays open from root's time) */
    reg32(REG_AUDIO_PARAM, 0);
    reg32(REG_AUDIO_VAL, C1Q3_HDMI_OFF);
    pg[C1Q3_W_VOLUME] = (unsigned long)volume;
    in_head = in_tail_seen = 0;
    pg[C1Q3_W_IN_HEAD] = 0;
    pg[C1Q3_W_IN_TAIL] = 0;
    pg[C1Q3_W_OVL_PITCH] = 0;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    set_text(cmdline);
    if (verbose)
        fprintf(stderr, "c1q3: engine: %s\n", cmdline);
    if (command(C1Q3_CMD_START, 5.0) < 0 && pg[C1Q3_W_STATE] != C1Q3_RUNNING) {
        fprintf(stderr, "c1q3: core 1 did not start the engine\n");
        ret = 1;
        goto out;
    }
    started = still_since = now();
    for (;;) {
        fd_set rs;
        struct timeval tv;
        const int xfd = ConnectionNumber(dpy);
        unsigned long st;

        if (got_signal) {
            command(C1Q3_CMD_STOP, 5.0);
            break;
        }
        FD_ZERO(&rs);
        FD_SET(xfd, &rs);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        select(xfd + 1, &rs, NULL, NULL, &tv);
        if (x_events()) {               /* the window was closed: quit as the console's quit */
            command(C1Q3_CMD_STOP, 5.0);
        }
        if (now() - last_poll < 0.2)    /* board reads a few times a second at most */
            continue;
        last_poll = now();
        for (i = 0; i < nexec; i++)
            if (execs[i].cmd && now() - started >= execs[i].at) {
                set_text(execs[i].cmd);
                if (verbose)
                    fprintf(stderr, "c1q3: %s\n", execs[i].cmd);
                command(C1Q3_CMD_EXEC, 5.0);
                execs[i].cmd = NULL;
            }
        st = pg[C1Q3_W_STATE];
        mouse(mapped && focused && pg[C1Q3_W_MOUSE] && st == C1Q3_RUNNING);
        show_log();
        if (pg[C1Q3_W_DIRTY])
            flush();
        if (st == C1Q3_QUIT)
            break;
        if (st == C1Q3_FAILED) {
            fprintf(stderr, "c1q3: core 1 failed (err %lu, fault %lu at pc %08lx addr %08lx); its last words:\n",
                    pg[C1Q3_W_ERR], ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
            show_log_tail();
            show_sample(stderr);
            ret = 1;
            break;
        }
        if (now() - last_status >= 1.0) {
            last_status = now();
            printf("status %lu %lu %lu %lu %lu %lu\n", pg[C1Q3_W_FRAMES], pg[C1Q3_W_FRAME_US_AVG] / 1000,
                   pg[C1Q3_W_FRAME_MS_MAX], pg[C1Q3_W_UNDERRUNS], pg[C1Q3_W_HEAP_KB] >> 10, st);
            fflush(stdout);
            /* no frame for 60 s (a map load takes a while): say where core 1 is, once */
            if (pg[C1Q3_W_FRAMES] != last_frames) {
                last_frames = pg[C1Q3_W_FRAMES];
                still_since = now();
                hang_said = 0;
            } else if (!hang_said && now() - still_since > 60.0) {
                hang_said = 1;
                fprintf(stderr, "c1q3: core 1 made no frame for 60 s (frame %lu, heartbeat %lu, load %lu)\n",
                        pg[C1Q3_W_FRAMES], ctl[W_HEARTBEAT], ctl[W_LOAD]);
                show_sample(stderr);
            }
        }
    }
    for (t0 = now(); pg[C1Q3_W_STATE] == C1Q3_RUNNING && now() - t0 < 10.0; usleep(100000))
        ;
    show_log();
    flush();
out:
    mouse(0);
    overlay(0);
    pg[C1Q3_W_OWNER] = 0;
    return ret;
}
