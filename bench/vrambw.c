/* vrambw.c -- how fast can the 68060 write into the Z3660 RTG framebuffer?
 * (2026-10-25)  Decides what an Xzz9000 ShmPutImage fast path should use.
 *
 * Opens /dev/zz9000 next to the running X server (no SETMODE, no switch),
 * maps the off-screen test area and copies a 512x320 16-bit image (327680 bytes, the
 * LamerCubeAMIX back buffer) from ordinary cacheable memory into VRAM BELOW
 * the visible screen, where nothing is shown.  Each method runs REPS times;
 * the result is MB/s and ms per image.
 *
 *   vrambw            long loop, unrolled movem, plain fill, RAM->RAM copy
 *                     (long and move16: RAM always takes a burst)
 *   vrambw move16     adds the 060's move16 line burst (last, flushed first:
 *                     if the board cannot take a burst the machine may hang)
 *
 * Build: m68k-elf-gcc -O2 -m68060 -o vrambw vrambw.c (gcc14-cross-amix)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_GETFBINFO   (ZZ9K_IOC | 11)

struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_fbinfo {
    unsigned long fb_offset, fb_size;
    unsigned short width, height, color_mode, depth, bytes_per_pixel, stride;
    unsigned long red_mask, green_mask, blue_mask, alpha_mask;
    unsigned short red_shift, green_shift, blue_shift, alpha_shift;
};

#define IMG_W   512
#define IMG_H   320
#define REPS    20

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void say(const char *name, double ms)
{
    double per = ms / REPS;
    printf("%-14s %7.2f ms/image  %6.2f MB/s\n", name, per,
           (IMG_W * 2.0 * IMG_H) / (per * 1000.0));
    fflush(stdout);
}

/* one row each; dst rows are `stride` bytes apart */
static void row_long(unsigned long *d, const unsigned long *s, int n)
{
    while (n-- > 0)
        *d++ = *s++;
}

static void row_movem(unsigned long *d, const unsigned long *s, int n)
{
    /* 32 bytes per step: 8 registers in, 8 out */
    int k = n >> 3;
    while (k-- > 0) {
        __asm__ volatile(
            "movem.l (%0)+,%%d0-%%d3/%%a2-%%a5\n\t"
            "movem.l %%d0-%%d3/%%a2-%%a5,(%1)\n\t"
            "lea 32(%1),%1"
            : "+a"(s), "+a"(d) : : "d0", "d1", "d2", "d3", "a2", "a3", "a4", "a5", "memory");
    }
    n &= 7;
    while (n-- > 0)
        *d++ = *s++;
}

static void row_move16(unsigned long *d, const unsigned long *s, int n)
{
    /* both 16-byte aligned; 16 bytes per move16 */
    int k = n >> 2;
    while (k-- > 0)
        __asm__ volatile("move16 (%0)+,(%1)+" : "+a"(s), "+a"(d) : : "memory");
    n &= 3;
    while (n-- > 0)
        *d++ = *s++;
}

/* misaligned rows: copy into a 16-byte-aligned stage at the dst's phase
 * (cacheable, hot), then move16 the aligned middle from stage to dst */
static unsigned long stagebuf[(IMG_W * 2 + 64) / 4 + 4];

static void row_staged(unsigned short *d, const unsigned short *s, int n)
{
    int phase = ((unsigned long) d & 15) >> 1;      /* pixels into its line */
    unsigned short *st = (unsigned short *)(((unsigned long) stagebuf + 15) & ~15UL) + phase;
    unsigned short *ds, *ss;
    int i, head, k;

    for (i = 0; i < n; i++)                         /* n pixels */
        st[i] = s[i];
    head = phase ? 8 - phase : 0;
    if (head > n)
        head = n;
    for (i = 0; i < head; i++)
        d[i] = st[i];
    ds = d + head;
    ss = st + head;
    k = (n - head) >> 3;                            /* 8 pixels per move16 */
    while (k-- > 0)
        __asm__ volatile("move16 (%0)+,(%1)+" : "+a"(ss), "+a"(ds) : : "memory");
    for (i = 0; i < ((n - head) & 7); i++)
        ds[i] = ss[i];
}

static void row_fill(unsigned long *d, const unsigned long *s, int n)
{
    unsigned long v = 0x12345678;
    (void) s;
    while (n-- > 0)
        *d++ = v;
}

static void run(const char *name, void (*row)(), char *dst, int stride, char *src)
{
    int r, y;
    double t0 = now();
    for (r = 0; r < REPS; r++)
        for (y = 0; y < IMG_H; y++)
            row((unsigned long *)(dst + (long) y * stride),
                (const unsigned long *)(src + (long) y * IMG_W * 2), IMG_W / 2);
    say(name, now() - t0);
}

int main(int argc, char **argv)
{
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    int fd, stride, m16 = argc > 1 && strcmp(argv[1], "move16") == 0;
    char *base, *vram, *src, *ram, *p;
    long off, i;

    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0) { perror("/dev/zz9000"); return 1; }
    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0 || ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0) {
        perror("ioctl"); return 1;
    }
    stride = fbi.stride;
    printf("board %lu bytes, fb at %lu size %lu, %ux%u stride %d\n",
           info.board_size, fbi.fb_offset, fbi.fb_size, fbi.width, fbi.height, stride);
    /* below the visible screen, 16-byte aligned */
    off = fbi.fb_offset + (long) fbi.height * stride + 65536;
    off = (off + 15) & ~15L;
    if (off + (long) IMG_H * stride > (long) fbi.fb_offset + (long) fbi.fb_size) {
        printf("no off-screen room\n"); return 1;
    }
    /* only the test area: the whole board is over the default ulimit -v */
    base = (char *) mmap(0, (long) IMG_H * stride + 8192, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, off & ~4095L);
    if (base == (char *) -1) { perror("mmap"); return 1; }
    vram = base + (off & 4095);
    printf("test area at fb+%ld\n", off - fbi.fb_offset);

    p = malloc(IMG_W * 2 * IMG_H + 16);
    src = (char *)(((unsigned long) p + 15) & ~15UL);
    for (i = 0; i < IMG_W * 2 * IMG_H; i++)
        src[i] = (char) i;
    p = malloc(IMG_H * stride + 16);
    ram = (char *)(((unsigned long) p + 15) & ~15UL);

    run("ram->ram long", row_long, ram, stride, src);
    run("ram->ram move16", row_move16, ram, stride, src);
    run("fill vram", row_fill, vram, stride, src);
    run("long loop", row_long, vram, stride, src);
    run("movem 32B", row_movem, vram, stride, src);
    if (m16) {
        printf("move16 next...\n");
        fflush(stdout);
        sync();
        run("move16", row_move16, vram, stride, src);
    }
    if (m16) {
        int r, y;
        double t0;
        char *dv = vram + 2;                        /* one pixel right */
        printf("staged move16, dst 2 bytes off...\n");
        fflush(stdout);
        t0 = now();
        for (r = 0; r < REPS; r++)
            for (y = 0; y < IMG_H; y++)
                row_staged((unsigned short *)(dv + (long) y * stride),
                           (const unsigned short *)(src + (long) y * IMG_W * 2), IMG_W - 1);
        say("staged move16", now() - t0);
        if (memcmp(dv, src, (IMG_W - 1) * 2) != 0 ||
            memcmp(dv + 100L * stride, src + 100L * IMG_W * 2, (IMG_W - 1) * 2) != 0)
            printf("WARNING: staged rows do not match\n");
        else
            printf("staged rows verified\n");
        memcpy(vram, src, IMG_W * 2);               /* for the check below */
    }
    if (memcmp(vram, src, IMG_W * 2) != 0)
        printf("WARNING: first row does not match\n");
    else
        printf("first row verified\n");
    return 0;
}
