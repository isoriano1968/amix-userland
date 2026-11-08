/*
 * c1uae -- runs the Amiga emulator on the Z3660's core 1 from AMIX: loads
 * z3660ARM_Core_uae.bin (through armbench), hands it the Kickstart and the
 * disk images, puts the picture into the RTG overlay, starts the board's
 * HDMI sound, passes input on and writes changed disk blocks back.  The
 * only part of the emulator that touches /dev/zz9000 (root): the CDE front
 * end (amixuae) runs it and talks to it through its standard input.
 *
 *   c1uae [options]
 *     -model A500|A600|A1200    the machine (default A500)
 *     -kick FILE                Kickstart ROM (.rom, or a byte-swapped .bin)
 *     -df0 FILE ... -df3 FILE   floppy images (.adf)
 *     -hdf FILE                 a hard file (.hdf), as DH0
 *     -ro                       nothing is written back
 *     -cfg LINE                 any uae config line (cpu_speed=max ...)
 *     -fast N                   Fast RAM, MB (1 2 4 8)
 *     -at X Y                   the picture's place on the screen (else centred)
 *     -scale 1|2                pixel doubling
 *     -volume N                 0-256
 *     -core FILE                the core image (default /root/z3660ARMCores/z3660ARM_Core_uae.bin)
 *     -bench FRAMES             no pacing, no picture: time FRAMES frames, print, stop
 *                               (with c1bench.adf as -df0: until CoreMark is done, its
 *                               times on core 1 and in the Amiga's own time)
 *     -v                        core 1's messages on stderr
 *
 * Installed setuid root (the board is root's): it maps the board and loads
 * the core as root, then runs as the caller before it opens any file; only
 * root may name another core image (-core).
 *
 * Standard input, one command a line (the front end's; also by hand):
 *   key DOWN CODE       a RETROK_ code pressed (1) or released (0)
 *   mouse DX DY         motion
 *   buttons MASK        mouse buttons (1 left, 2 right, 4 middle)
 *   joy PORT BITS       joystick port 0/1: RETRO_DEVICE_ID_JOYPAD_* bits
 *   place X Y           the picture moves (the window moved)
 *   hide | show         the overlay off/on (window iconified ...)
 *   pause 0|1, reset, volume N, stop
 * End of input = stop.  A status line goes to standard output once a second:
 *   status FRAMES FRAME_US UNDERRUNS DIRTY STATE
 * Exit 3: another c1uae runs the Amiga already.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "bench.h"
#include "c1uae_board.h"

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

#define PIC_W               640     /* uae4arm_resolution 640x256, line doubling: 640x512 */
#define PIC_H               512

static int fd = -1, verbose, overlay_on, ovl_x, ovl_y, scale = 1, vs = 1;
static struct zz9k_info info;
static volatile unsigned long *ctl, *c1ld, *ua;
static volatile unsigned char *uab;
static unsigned char *win, *shared;
static unsigned long log_seen, in_head, in_tail_seen;

static struct { char path[1024]; char name[C1UA_NAME_LEN]; unsigned long size, flags; } files[C1UA_MAX_FILES];
static int nfiles;

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

/* core 1's messages since the last look */
static void show_log(void)
{
    unsigned long head = ua[C1UA_W_LOG_HEAD];

    if (head - log_seen > C1UA_LOG_LEN)
        log_seen = head - C1UA_LOG_LEN;
    for (; log_seen != head; log_seen++)
        if (verbose)
            fputc(uab[C1UA_LOG_OFF + log_seen % C1UA_LOG_LEN], stderr);
}

/* where core 1 is: its timer's last sample (pc, lr, sp, code addresses on
   the stack; look them up in the image's map) */
static void show_sample(void)
{
    int k;

    if (!ua[C1UA_W_SAMPLE])
        return;
    printf("c1uae: core 1 sample %lu: pc %08lx lr %08lx sp %08lx stack",
           ua[C1UA_W_SAMPLE], ua[C1UA_W_SAMPLE + 1], ua[C1UA_W_SAMPLE + 2], ua[C1UA_W_SAMPLE + 3]);
    for (k = C1UA_W_SAMPLE + 4; k < C1UA_W_SAMPLE_END && ua[k]; k++)
        printf(" %08lx", ua[k]);
    printf("\n");
}

/* a command, and its end (core 1 clears it); polls slowly: 68k reads of
   board memory stall core 0 */
static long command(unsigned long cmd, unsigned long arg, double timeout)
{
    double t0 = now();

    ua[C1UA_W_ARG] = arg;
    ua[C1UA_W_CMD] = cmd;
    while (ua[C1UA_W_CMD] != C1UA_CMD_NONE) {
        if (now() - t0 > timeout)
            return -100;
        usleep(20000);
    }
    return (long)ua[C1UA_W_RESULT];
}

/* the core: running and idle, else (re)loaded through armbench */
static int core_ready(const char *core)
{
    char cmd[1200];
    double t0;

    if (ua[C1UA_W_MAGIC] == C1UA_MAGIC && ua[C1UA_W_STATE] == C1UA_IDLE && ctl[W_STATE] == HB_IDLE)
        return 0;
    sprintf(cmd, "/opt/amix/bin/armbench %s %s", ctl[W_MAGIC] == HB_MAGIC_READY ? "-reload" : "-load", core);
    if (verbose)
        fprintf(stderr, "c1uae: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "c1uae: %s failed\n", cmd);
        return -1;
    }
    for (t0 = now(); ua[C1UA_W_MAGIC] != C1UA_MAGIC; usleep(50000))
        if (now() - t0 > 10.0) {
            fprintf(stderr, "c1uae: the core did not come up (stage %lu)\n", ctl[W_STAGE]);
            return -1;
        }
    log_seen = ua[C1UA_W_LOG_HEAD];
    return 0;
}

static int add_file(const char *path, int rw)
{
    struct stat st;
    const char *base = strrchr(path, '/');
    unsigned char head[4];
    FILE *f;

    if (nfiles == C1UA_MAX_FILES || stat(path, &st) < 0) {
        fprintf(stderr, "c1uae: %s: %s\n", path, nfiles == C1UA_MAX_FILES ? "too many files" : strerror(errno));
        return -1;
    }
    strncpy(files[nfiles].path, path, sizeof(files[nfiles].path) - 1);
    /* by its last part, made unique by its number: the core needs no paths */
    sprintf(files[nfiles].name, "%d-%.100s", nfiles, base ? base + 1 : path);
    files[nfiles].size = (unsigned long)st.st_size;
    files[nfiles].flags = rw ? C1UA_FILE_RW : 0;
    if ((f = fopen(path, "rb")) != NULL) {     /* a ROM for EPROMs: 0x14 0x11 0xF9 0x4E */
        if (fread(head, 1, 4, f) == 4 && head[0] == 0x14 && head[1] == 0x11 && head[3] == 0x4E)
            files[nfiles].flags |= C1UA_FILE_SWAB;
        fclose(f);
    }
    return nfiles++;
}

static int upload(int i)
{
    FILE *f = fopen(files[i].path, "rb");
    unsigned long off = 0, n;
    int k;

    if (!f) {
        perror(files[i].path);
        return -1;
    }
    for (k = 0; k < C1UA_NAME_LEN; k++)
        uab[C1UA_NAMES_OFF + i * C1UA_NAME_LEN + k] = (unsigned char)files[i].name[k];
    ua[C1UA_W_FILES + i * 3] = files[i].size;
    ua[C1UA_W_FILES + i * 3 + 1] = files[i].flags;
    do {
        n = files[i].size - off < C1UA_WIN_SIZE ? files[i].size - off : C1UA_WIN_SIZE;
        if (fread(win, 1, (size_t)n, f) != (size_t)n) {
            fprintf(stderr, "c1uae: %s: short read\n", files[i].path);
            fclose(f);
            return -1;
        }
        ua[C1UA_W_CHUNK_FILE] = (unsigned long)i;
        ua[C1UA_W_CHUNK_OFF] = off;
        ua[C1UA_W_CHUNK_LEN] = n;
        if (command(C1UA_CMD_CHUNK, 0, 10.0) != 0) {
            fprintf(stderr, "c1uae: core 1 refused %s (err %lu)\n", files[i].path, ua[C1UA_W_ERR]);
            fclose(f);
            return -1;
        }
        off += n;
    } while (off < files[i].size);
    fclose(f);
    return 0;
}

/* changed blocks back into the files */
static void flush(void)
{
    int guard = 0;

    while (ua[C1UA_W_DIRTY] && guard++ < 4096) {
        unsigned long fi, off, len;
        FILE *f;

        if (command(C1UA_CMD_FLUSH, 0, 5.0) != 0)
            break;
        fi = ua[C1UA_W_FLUSH_FILE];
        off = ua[C1UA_W_FLUSH_OFF];
        len = ua[C1UA_W_FLUSH_LEN];
        if (fi >= (unsigned long)nfiles || !(files[fi].flags & C1UA_FILE_RW))
            continue;
        if ((f = fopen(files[fi].path, "r+b")) == NULL || fseek(f, (long)off, SEEK_SET) < 0 ||
            fwrite(win, 1, (size_t)len, f) != (size_t)len)
            fprintf(stderr, "c1uae: writing %s: %s\n", files[fi].path, strerror(errno));
        if (f)
            fclose(f);
    }
}

/* the overlay: on as glshow and zzgl do it (place before and after INIT) */
static void place(int x, int y)
{
    const int w = PIC_W * scale, h = PIC_H * scale;

    if (x + w > info.width)
        x = info.width - w;
    if (y + h > info.height)
        y = info.height - h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    ovl_x = x;
    ovl_y = y;
    reg32(REG_OVL_XSTART, (unsigned long)x);
    reg32(REG_OVL_YSTART, (unsigned long)y);
    reg32(REG_OVL_WIDTH, (unsigned long)(w > info.width ? info.width : w));
    reg32(REG_OVL_HEIGHT, (unsigned long)(h > info.height ? info.height : h));
    ua[C1UA_W_OVL_ROW0] = (unsigned long)(y / vs);
}

static void overlay(int on)
{
    if (on && !overlay_on) {
        vs = info.width >= 1600 && !(c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FETCH)) ? 2 : 1;
        ua[C1UA_W_OVL_PITCH] = (unsigned long)info.width * 4;
        ua[C1UA_W_OVL_MAXW] = (unsigned long)(PIC_W * scale > info.width ? info.width : PIC_W * scale);
        ua[C1UA_W_OVL_MAXH] = (unsigned long)(PIC_H * scale > info.height ? info.height : PIC_H * scale);
        ua[C1UA_W_OVL_SCALE] = (unsigned long)scale;
        place(ovl_x, ovl_y);
        memset(shared, 0, 96);
        reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
        reg32(REG_MPEG_PARAM0 + 4, info.base);
        reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
        reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
        reg32(REG_MPEG_CMD, CMD_INIT);
        usleep(100000);
        place(ovl_x, ovl_y);
        overlay_on = 1;
    } else if (!on && overlay_on) {
        ua[C1UA_W_OVL_PITCH] = 0;       /* core 1 stops drawing first */
        usleep(100000);
        reg32(REG_MPEG_CMD, CMD_CLOSE);
        overlay_on = 0;
    }
}

static void event(unsigned long e)
{
    if (in_head - in_tail_seen >= C1UA_IN_N - 8) {
        in_tail_seen = ua[C1UA_W_IN_TAIL];      /* only when it might be full */
        if (in_head - in_tail_seen >= C1UA_IN_N - 1)
            return;                             /* core 1 is not reading: dropped */
    }
    ((volatile unsigned long *)(uab + C1UA_IN_OFF))[in_head % C1UA_IN_N] = e;
    in_head++;
    ua[C1UA_W_IN_HEAD] = in_head;
}

/* one line from the front end; returns 1 for stop */
static int line(char *s)
{
    long a = 0, b = 0;
    char w[16];

    if (sscanf(s, "%15s %ld %ld", w, &a, &b) < 1)
        return 0;
    if (!strcmp(w, "key"))
        event((unsigned long)C1UA_EV_KEY << 24 | (unsigned long)(a != 0) << 16 | ((unsigned long)b & 0xFFFF));
    else if (!strcmp(w, "mouse"))
        event((unsigned long)C1UA_EV_MOUSE << 24 | ((unsigned long)a & 0xFFF) << 12 | ((unsigned long)b & 0xFFF));
    else if (!strcmp(w, "buttons"))
        event((unsigned long)C1UA_EV_BUTTONS << 24 | ((unsigned long)a & 7));
    else if (!strcmp(w, "joy"))
        event((unsigned long)C1UA_EV_JOY << 24 | ((unsigned long)a & 1) << 16 | ((unsigned long)b & 0xFFFF));
    else if (!strcmp(w, "place")) {
        if (overlay_on)
            place((int)a, (int)b);
        else
            ovl_x = (int)a, ovl_y = (int)b;
    } else if (!strcmp(w, "hide"))
        overlay(0);
    else if (!strcmp(w, "show"))
        overlay(1);
    else if (!strcmp(w, "pause"))
        command(C1UA_CMD_PAUSE, (unsigned long)a, 2.0);
    else if (!strcmp(w, "reset"))
        command(C1UA_CMD_RESET, 0, 2.0);
    else if (!strcmp(w, "volume"))
        ua[C1UA_W_VOLUME] = (unsigned long)a;
    else if (!strcmp(w, "stop"))
        return 1;
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
    fprintf(stderr, "usage: c1uae [-model A500|A600|A1200] -kick ROM [-df0..3 ADF] [-hdf HDF] [-ro]\n"
                    "             [-cfg LINE] [-fast MB] [-at X Y] [-scale 1|2] [-volume N] [-core FILE]\n"
                    "             [-bench FRAMES] [-v]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *core = "/root/z3660ARMCores/z3660ARM_Core_uae.bin", *model = "A500";
    const char *kick = NULL, *df[4] = { NULL, NULL, NULL, NULL }, *hdf = NULL, *cfg[16];
    int ncfg = 0, ro = 0, fast = 0, bench = 0, i, kf = -1, dff[4] = { -1, -1, -1, -1 }, hf = -1;
    long volume = 256;
    char text[C1UA_TEXT_LEN], *t;
    double last_status = 0;
    int ret = 0;

    ovl_x = ovl_y = -1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-model") && i + 1 < argc) model = argv[++i];
        else if (!strcmp(argv[i], "-kick") && i + 1 < argc) kick = argv[++i];
        else if (!strncmp(argv[i], "-df", 3) && argv[i][3] >= '0' && argv[i][3] <= '3' && !argv[i][4] && i + 1 < argc) {
            df[argv[i][3] - '0'] = argv[i + 1];
            i++;
        }
        else if (!strcmp(argv[i], "-hdf") && i + 1 < argc) hdf = argv[++i];
        else if (!strcmp(argv[i], "-ro")) ro = 1;
        else if (!strcmp(argv[i], "-cfg") && i + 1 < argc && ncfg < 16) cfg[ncfg++] = argv[++i];
        else if (!strcmp(argv[i], "-fast") && i + 1 < argc) fast = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-at") && i + 2 < argc) { ovl_x = atoi(argv[i + 1]); ovl_y = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "-scale") && i + 1 < argc) scale = atoi(argv[++i]) == 2 ? 2 : 1;
        else if (!strcmp(argv[i], "-volume") && i + 1 < argc) volume = atol(argv[++i]);
        else if (!strcmp(argv[i], "-core") && i + 1 < argc) core = argv[++i];
        else if (!strcmp(argv[i], "-bench") && i + 1 < argc) bench = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else usage();
    }
    if (!kick || (!df[0] && !hdf))
        usage();
    if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("c1uae: /dev/zz9000");
        return 1;
    }
    ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE);
    c1ld = (volatile unsigned long *)map(C1LD_OFF, PAGE);
    ua = (volatile unsigned long *)map(C1UA_OFF, PAGE);
    win = (unsigned char *)map(C1UA_WIN_OFF, C1UA_WIN_SIZE);
    shared = (unsigned char *)map(SHARED_OFF, PAGE);
    if (!ctl || !c1ld || !ua || !win || !shared) {
        perror("c1uae: mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    uab = (volatile unsigned char *)ua;
    if (ovl_x < 0) {
        ovl_x = (info.width - PIC_W * scale) / 2;
        ovl_y = (info.height - PIC_H * scale) / 2;
    }
    if (getuid() != 0 && strcmp(core, "/root/z3660ARMCores/z3660ARM_Core_uae.bin")) {
        fprintf(stderr, "c1uae: -core is for root only\n");
        return 2;
    }
    putenv("PATH=/bin:/usr/bin:/opt/amix/bin");
    putenv("IFS= \t\n");
    /* one Amiga at a time: another c1uae that still lives keeps it */
    if (ua[C1UA_W_MAGIC] == C1UA_MAGIC && ua[C1UA_W_OWNER] && ua[C1UA_W_OWNER] != (unsigned long)getpid() &&
        (ua[C1UA_W_STATE] == C1UA_RUNNING || ua[C1UA_W_STATE] == C1UA_LOADING) &&
        kill((pid_t)ua[C1UA_W_OWNER], 0) == 0) {
        fprintf(stderr, "c1uae: the Amiga is already running (c1uae %lu)\n", ua[C1UA_W_OWNER]);
        return 3;
    }
    if (core_ready(core) < 0)
        return 1;
    ua[C1UA_W_OWNER] = (unsigned long)getpid();
    /* from here on as the caller: the files are theirs */
    if (setgid(getgid()) < 0 || setuid(getuid()) < 0) {
        perror("c1uae: setuid");
        return 1;
    }

    /* the files, then the configuration that names them */
    kf = add_file(kick, 0);
    for (i = 0; i < 4; i++)
        if (df[i])
            dff[i] = add_file(df[i], !ro);
    if (hdf)
        hf = add_file(hdf, !ro);
    if (kf < 0 || (df[0] && dff[0] < 0) || (hdf && hf < 0))
        return 1;
    for (i = 0; i < nfiles; i++)
        if (upload(i) < 0)
            return 1;
    t = text;
    t += sprintf(t, "model=%s\nvar=uae4arm_resolution=640x256\nvar=uae4arm_linedoubling=on\n", model);
    if (fast)
        t += sprintf(t, "var=uae4arm_fastmem=%d MB\n", fast);
    t += sprintf(t, "cfg=kickstart_rom_file=%s\n", files[kf].name);
    for (i = 1; i < 4; i++)
        if (dff[i] >= 0)
            t += sprintf(t, "cfg=floppy%d=%s\ncfg=nr_floppies=%d\n", i, files[dff[i]].name, i + 1);
    if (hf >= 0 && dff[0] >= 0)
        t += sprintf(t, "cfg=hardfile2=%s,DH0:%s,32,1,2,512,0,,uae0\n", ro ? "ro" : "rw", files[hf].name);
    for (i = 0; i < ncfg && t - text < C1UA_TEXT_LEN - 200; i++)
        t += sprintf(t, "cfg=%s\n", cfg[i]);
    t += sprintf(t, "content=%s\n", dff[0] >= 0 ? files[dff[0]].name : files[hf].name);
    for (i = 0; i <= t - text; i++)
        uab[C1UA_TEXT_OFF + i] = (unsigned char)text[i];
    ua[C1UA_W_VOLUME] = (unsigned long)volume;
    in_head = in_tail_seen = 0;
    ua[C1UA_W_IN_HEAD] = 0;
    ua[C1UA_W_IN_TAIL] = 0;

    if (bench) {
        unsigned long long tk;
        double secs;

        ua[C1UA_W_OVL_PITCH] = 0;       /* no picture */
        printf("c1uae: bench %d frames, %s, ARM %lu MHz\n", bench, model, ua[C1UA_W_CPU_HZ] / 1000000);
        ua[C1UA_W_ARG] = (unsigned long)bench;
        ua[C1UA_W_CMD] = C1UA_CMD_BENCH;
        unsigned long last_frames = 0;
        int still = 0;

        for (i = 0; ua[C1UA_W_STATE] != C1UA_STOPPED && ua[C1UA_W_STATE] != C1UA_FAILED; i++) {
            sleep(1);
            show_log();
            if (i % 5 == 4) {           /* progress: a long CoreMark run must not look hung */
                printf("c1uae: %lu frames, last %lu us, avg %lu us, coremark %c, 68k pc %08lx\n",
                       ua[C1UA_W_FRAMES], ua[C1UA_W_FRAME_US], ua[C1UA_W_FRAME_US_AVG],
                       ua[C1UA_W_BM_STATE] ? (int)ua[C1UA_W_BM_STATE] : '-', ua[C1UA_W_PC]);
                show_sample();
                fflush(stdout);
            }
            still = ua[C1UA_W_FRAMES] == last_frames ? still + 1 : 0;
            last_frames = ua[C1UA_W_FRAMES];
            if (still >= 20) {
                fprintf(stderr, "c1uae: no frame for 20 s: core 1 hangs (armbench -reload -force %s)\n", core);
                show_sample();
                ua[C1UA_W_OWNER] = 0;
                return 4;
            }
        }
        show_log();
        if (ua[C1UA_W_STATE] == C1UA_FAILED) {
            fprintf(stderr, "c1uae: core 1 failed (err %lu, fault %lu at pc %08lx addr %08lx)\n",
                    ua[C1UA_W_ERR], ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
            return 1;
        }
        tk = (unsigned long long)ua[C1UA_W_BENCH_TICKS_HI] << 32 | ua[C1UA_W_BENCH_TICKS_LO];
        secs = (double)tk / (double)ua[C1UA_W_TICK_HZ];
        printf("c1uae: %d frames in %.2f s: %.2f ms a frame = %.0f %% of real time (PAL: 20 ms), last %lu us, 68k pc %08lx\n",
               bench, secs, secs * 1000.0 / bench, 2000.0 / (secs * 1000.0 / bench), ua[C1UA_W_FRAME_US], ua[C1UA_W_PC]);
        if (ua[C1UA_W_BM_STATE] == 'D') {
            const unsigned long long bt = (unsigned long long)ua[C1UA_W_BM_TICKS_HI] << 32 | ua[C1UA_W_BM_TICKS_LO];
            const double bs = (double)bt / (double)ua[C1UA_W_TICK_HZ];
            const double es = ua[C1UA_W_BM_FRAMES] * 0.02;    /* PAL frames */

            printf("c1uae: c1bench CoreMark: %.2f s on core 1, %.2f s in the emulated Amiga's own time\n"
                   "       (iterations / time = CoreMark; compare coremark.060 on the 060)\n", bs, es);
        }
        return 0;
    }

    /* sound: the HDMI ring's DMA, as vplay starts it */
    reg32(REG_AUDIO_PARAM, 0);
    reg32(REG_AUDIO_VAL, C1UA_HDMI_OFF);
    usleep(100000);
    overlay(1);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    ua[C1UA_W_CMD] = C1UA_CMD_START;
    for (;;) {
        fd_set rs;
        struct timeval tv;
        char buf[256];

        if (got_signal)
            break;
        FD_ZERO(&rs);
        FD_SET(0, &rs);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        if (select(1, &rs, NULL, NULL, &tv) > 0) {
            if (!fgets(buf, sizeof(buf), stdin) || line(buf))
                break;
            continue;
        }
        if (now() - last_status >= 1.0) {     /* a few reads a second at most */
            unsigned long st = ua[C1UA_W_STATE];

            last_status = now();
            printf("status %lu %lu %lu %lu %lu\n", ua[C1UA_W_FRAMES], ua[C1UA_W_FRAME_US_AVG],
                   ua[C1UA_W_UNDERRUNS], ua[C1UA_W_DIRTY], st);
            fflush(stdout);
            show_log();
            if (st == C1UA_FAILED) {
                fprintf(stderr, "c1uae: core 1 failed (err %lu, fault %lu at pc %08lx addr %08lx)\n",
                        ua[C1UA_W_ERR], ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
                ret = 1;
                break;
            }
            if (ua[C1UA_W_DIRTY] && !ro)
                flush();
        }
    }
    if (ua[C1UA_W_STATE] == C1UA_RUNNING)
        command(C1UA_CMD_STOP, 0, 5.0);
    if (!ro)
        flush();
    overlay(0);
    show_log();
    ua[C1UA_W_OWNER] = 0;
    return ret;
}
