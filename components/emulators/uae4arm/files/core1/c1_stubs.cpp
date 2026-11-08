/*
 * c1_stubs.cpp -- what core 1 lacks: the POSIX calls newlib has no body for,
 * and the emulator parts left out of the build (CD images, CD32 FMV, file
 * system usage).
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "autoconf.h"
#include "blkdev.h"
#include "fsusage.h"
#include "cd32_fmv.h"
#include "td-sdl/thread.h"
#include "bsdsocket.h"
#include <malloc.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/mman.h>
#include "c1uae.h"

/* no CD drives on core 1 (A500, A600, A1200 only): an empty table, never
   reached without a CD slot in the configuration */
struct device_functions devicefunc_cdimage;

addrbank *cd32_fmv_init(struct autoconfig_info *aci)
{
    (void)aci;
    return NULL;
}

/* no CD32 FMV cartridge */
int cd32_fmv_active;
void cd32_fmv_reset(void) {}
void cd32_fmv_free(void) {}
void rethink_cd32fmv(void) {}
void cd32_fmv_hsync_handler(void) {}
void cd32_fmv_vsync_handler(void) {}
void cd32_fmv_state(int state) { (void)state; }
void cd32_fmv_genlock(struct vidbuffer *a, struct vidbuffer *b) { (void)a; (void)b; }
void cd32_fmv_set_sync(float s, float a) { (void)s; (void)a; }
void sub_to_interleaved(const uae_u8 *s, uae_u8 *d) { (void)s; (void)d; }

/* no bsdsocket.library: the Amiga's network is not core 1's */
int volatile bsd_int_requested;
void bsdlib_install(void) {}
void bsdlib_reset(void) {}
void bsdsock_fake_int_handler(void) {}
uae_u32 bsdlib_startup(TrapContext *ctx, uae_u32 resaddr) { (void)ctx; return resaddr; }
void init_max_signals(void) {}  /* sigsegv_handler.cpp: the JIT's fault handler (core1_main.c on the board) */

int get_fs_usage(const TCHAR *path, const TCHAR *disk, struct fs_usage *fsp)
{
    (void)path; (void)disk; (void)fsp;
    return -1;
}

extern "C" {

void *valloc(size_t n)
{
    return memalign(4096, n);
}

/* anonymous memory only: the JIT cache and the Amiga's memory */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    void *p;

    (void)addr; (void)prot; (void)off;
    /* one address space, no mapping at a chosen address: the emulator then
       takes its 16 MB fallback (no Zorro III memory) */
    if (flags & MAP_FIXED) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    if (fd != -1 && !(flags & MAP_ANON)) {
        errno = ENODEV;
        return MAP_FAILED;
    }
    p = memalign(4096, len);
    if (!p) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(p, 0, len);
    return p;
}

int munmap(void *addr, size_t len)
{
    (void)len;
    free(addr);
    return 0;
}

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr; (void)len; (void)prot;
    return 0;
}

int getpagesize(void) { return 4096; }
int chmod(const char *p, mode_t m) { (void)p; (void)m; errno = EROFS; return -1; }
int mkdir(const char *p, mode_t m) { (void)p; (void)m; errno = EROFS; return -1; }
int rmdir(const char *p) { (void)p; errno = EROFS; return -1; }
void sync(void) {}
int truncate(const char *p, off_t l) { (void)p; (void)l; errno = EROFS; return -1; }
int utimes(const char *p, const struct timeval *t) { (void)p; (void)t; errno = EROFS; return -1; }
char *getcwd(char *b, size_t n) { if (b && n > 1) { b[0] = '/'; b[1] = 0; } return b; }

int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

char *strcasestr(const char *h, const char *n)
{
    size_t l = strlen(n);

    for (; *h; h++)
        if (!strncasecmp(h, n, l))
            return (char *)h;
    return l ? NULL : (char *)h;
}

DIR *opendir(const char *p)
{
    (void)p;
    errno = ENOENT;
    return NULL;
}

struct dirent *readdir(DIR *d)
{
    (void)d;
    return NULL;
}

int closedir(DIR *d)
{
    (void)d;
    return 0;
}

void rewinddir(DIR *d)
{
    (void)d;
}

int clock_gettime(clockid_t c, struct timespec *ts)
{
    uint64_t us = c1p_now_us();

    (void)c;
    ts->tv_sec = (time_t)(us / 1000000);
    ts->tv_nsec = (long)(us % 1000000) * 1000;
    return 0;
}

int usleep(useconds_t us)
{
    uint64_t end = c1p_now_us() + us;

    while (c1p_now_us() < end)
        c1p_idle();
    return 0;
}

unsigned sleep(unsigned s)
{
    usleep(s * 1000000u);
    return 0;
}

}

/* for the test's log: where the 68k is */
#include "newcpu.h"
#include "custom.h"
extern "C" uint32_t c1_dbg_pc(void)
{
    return m68k_getpc();
}

/* a long of the Amiga's memory (the benchmark's marks), 0 outside chip RAM */
extern "C" uint32_t c1_dbg_chip_long(uint32_t addr)
{
    if (addr + 4 > currprefs.chipmem_size)
        return 0;
    return get_long(addr);
}

/* the C string at addr in chip RAM into buf */
extern "C" void c1_dbg_chip_str(uint32_t addr, char *buf, unsigned n)
{
    unsigned i;

    for (i = 0; i + 1 < n && addr + i < currprefs.chipmem_size; i++)
        if (!(buf[i] = (char)get_byte(addr + i)))
            break;
    buf[i] = 0;
}
