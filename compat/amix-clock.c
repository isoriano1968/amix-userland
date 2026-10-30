/*
 * amix-clock.c -- gettimeofday() without a system call, for ffplay on AMIX.
 *
 * A system call costs AMIX about 0.27 ms, and ffplay, SDL and gnu-pth's
 * scheduler read the clock some 500 times a second.  The kernel keeps the
 * time in hrestime (seconds, nanoseconds), advanced every clock tick; this
 * maps its page of /dev/kmem read-only and reads it there.  Resolution is
 * the tick (10 ms), plenty for frame timing and pth's timeouts.
 *
 * hrestime is a common symbol, so its address is not in the kernel file as
 * such: AMIX's loader binds the kernel at 0x08000000 (the Z3660's RAM),
 * places .text, .data and .bss there one after the other, then the commons
 * in symbol-table order, each at its alignment.  That layout is computed
 * from /stand/unix.z3 (AMIX_KERNEL names another file), and the value found
 * there is used only if it agrees with the system clock; otherwise, and
 * without read access to /dev/kmem (root), every call is the system call,
 * as in libc.  Only that one page of kernel RAM is ever read -- never a
 * search: above the kernel image the kernel maps hardware, whose registers
 * may act on a mere read.  AMIX_KCLOCK=0 turns it off.
 */
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

extern int munmap();                    /* not in AMIX's sys/mman.h */

#define KERNEL_BASE 0x08000000UL
#define SHN_COMMON  0xfff2

extern int _hrtcntl(int cmd, int clk, void *interval, void *hrt);  /* libc */

static volatile long *kclock;           /* hrestime: tv_sec, tv_nsec */
static int kstate;                      /* 0 not tried, 1 mapped, -1 system call */

static int sys_time(struct timeval *tv)
{
    struct { unsigned long secs; long rem; unsigned long res; } h;

    h.res = 1000000;                    /* microseconds, as libc's gettimeofday */
    if (_hrtcntl(1 /* HRT_TOFD */, 1 /* CLK_STD */, 0, &h) != 0)
        return -1;
    tv->tv_sec = (long)h.secs;
    tv->tv_usec = h.rem;
    return 0;
}

static unsigned long u32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static unsigned u16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

/* hrestime's address in the bound kernel, or 0 */
static unsigned long hrestime_addr(const char *kernel)
{
    unsigned char eh[52], *sh = 0, *sym = 0;
    char *str = 0, *shstr = 0;
    unsigned long addr = 0, text = 0, data = 0, bss = 0, a, symoff = 0, symsize = 0, stroff = 0, strsize = 0;
    unsigned i, shnum, shentsize, shstrndx;
    int fd = open(kernel, O_RDONLY);

    if (fd < 0)
        return 0;
    if (read(fd, eh, sizeof eh) != sizeof eh || memcmp(eh, "\177ELF\1\2", 6) || u16(eh + 16) != 1 /* ET_REL */)
        goto out;
    shentsize = u16(eh + 46);
    shnum = u16(eh + 48);
    shstrndx = u16(eh + 50);
    if (shentsize != 40 || shnum == 0 || shstrndx >= shnum)
        goto out;
    sh = (unsigned char *)malloc((size_t)shnum * 40);
    if (!sh || lseek(fd, (off_t)u32(eh + 32), SEEK_SET) < 0 || read(fd, sh, (size_t)shnum * 40) != (int)(shnum * 40))
        goto out;
    shstr = (char *)malloc(u32(sh + shstrndx * 40 + 20) + 1);
    if (!shstr || lseek(fd, (off_t)u32(sh + shstrndx * 40 + 16), SEEK_SET) < 0 ||
        read(fd, shstr, u32(sh + shstrndx * 40 + 20)) != (int)u32(sh + shstrndx * 40 + 20))
        goto out;
    for (i = 0; i < shnum; i++) {
        const unsigned char *s = sh + i * 40;
        const char *name = shstr + u32(s);

        if (!strcmp(name, ".text"))
            text = u32(s + 20);
        else if (!strcmp(name, ".data"))
            data = u32(s + 20);
        else if (!strcmp(name, ".bss"))
            bss = u32(s + 20);
        else if (u32(s + 4) == 2 /* SHT_SYMTAB */) {
            const unsigned char *l = sh + u32(s + 24) * 40;

            symoff = u32(s + 16);
            symsize = u32(s + 20);
            stroff = u32(l + 16);
            strsize = u32(l + 20);
        }
    }
    if (!text || !symsize || !strsize)
        goto out;
    sym = (unsigned char *)malloc(symsize);
    str = (char *)malloc(strsize + 1);
    if (!sym || !str || lseek(fd, (off_t)symoff, SEEK_SET) < 0 || read(fd, sym, symsize) != (int)symsize ||
        lseek(fd, (off_t)stroff, SEEK_SET) < 0 || read(fd, str, strsize) != (int)strsize)
        goto out;
    str[strsize] = 0;
    a = KERNEL_BASE + text + data + bss;
    for (i = 0; i < symsize / 16; i++) {
        const unsigned char *s = sym + i * 16;
        unsigned long align = u32(s + 4), size = u32(s + 8);

        if (u16(s + 14) != SHN_COMMON || u32(s) >= strsize)
            continue;
        if (align > 1)
            a = (a + align - 1) / align * align;
        if (!strcmp(str + u32(s), "hrestime")) {
            if (size == 8)
                addr = a;
            break;
        }
        a += size;
    }
out:
    free(str);
    free(sym);
    free(shstr);
    free(sh);
    close(fd);
    return addr;
}

static void kclock_open(void)
{
    const char *e = getenv("AMIX_KCLOCK"), *k = getenv("AMIX_KERNEL");
    unsigned long addr, page;
    struct timeval tv;
    char *p;
    long s, ns;
    int fd;

    kstate = -1;
    if ((e && !strcmp(e, "0")) || (addr = hrestime_addr(k ? k : "/stand/unix.z3")) == 0)
        return;
    fd = open("/dev/kmem", O_RDONLY);
    if (fd < 0)
        return;
    page = addr & ~4095UL;
    p = (char *)mmap(0, 4096, PROT_READ, MAP_SHARED, fd, (off_t)page);
    close(fd);
    if (p == (char *)-1)
        return;
    kclock = (volatile long *)(p + (addr - page));
    do {
        s = kclock[0];
        ns = kclock[1];
    } while (s != kclock[0]);
    if (sys_time(&tv) != 0 || ns < 0 || ns >= 1000000000L || s < tv.tv_sec - 1 || s > tv.tv_sec + 1) {
        munmap(p, 4096);                /* not the clock: that kernel is not /stand/unix.z3 */
        kclock = 0;
        return;
    }
    kstate = 1;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    long s, ns;

    (void)tz;
    if (!tv)
        return 0;
    if (kstate == 0)
        kclock_open();
    if (kstate < 0)
        return sys_time(tv);
    do {
        s = kclock[0];
        ns = kclock[1];
    } while (s != kclock[0]);
    tv->tv_sec = s;
    tv->tv_usec = ns / 1000;
    return 0;
}
