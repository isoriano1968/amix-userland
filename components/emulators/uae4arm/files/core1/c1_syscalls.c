/*
 * c1_syscalls.c -- files on core 1: newlib's system calls over the files
 * AMIX handed in at start-up (c1uae_board.h), held whole in core 1's
 * memory.  The emulator opens them by the names in its configuration, as
 * it would on a UNIX host.  Writes to a writable file (an ADF, a hard file)
 * mark 64 KB blocks; AMIX fetches them back (C1UA_CMD_FLUSH).
 *
 * stdout and stderr go to the log ring.  There are no directories.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "c1uae.h"
#include "c1fs.h"

#define MAX_FD      16
#define BLOCK_SHIFT 16

struct c1file c1fs[C1FS_MAX];
static struct { int file; off_t pos; int used; } fds[MAX_FD];

int c1fs_add(const char *name, size_t size, int flags)
{
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        if (!c1fs[i].data)
            break;
    if (i == C1FS_MAX)
        return -1;
    c1fs[i].data = malloc(size ? size : 1);
    c1fs[i].dirty = calloc((size >> BLOCK_SHIFT) / 8 + 1, 1);
    if (!c1fs[i].data || !c1fs[i].dirty) {
        free(c1fs[i].data);
        free(c1fs[i].dirty);
        c1fs[i].data = NULL;
        return -1;
    }
    strncpy(c1fs[i].name, name, sizeof(c1fs[i].name) - 1);
    c1fs[i].size = size;
    c1fs[i].flags = flags;
    return i;
}

/* the next changed block: 0 and its place, or -1 when there is none */
int c1fs_next_dirty(int *file, size_t *off, size_t *len)
{
    int i;
    size_t b;

    for (i = 0; i < C1FS_MAX; i++) {
        const size_t nb = (c1fs[i].size + (1u << BLOCK_SHIFT) - 1) >> BLOCK_SHIFT;

        if (!c1fs[i].data || !c1fs[i].ndirty)
            continue;
        for (b = 0; b < nb; b++)
            if (c1fs[i].dirty[b >> 3] & (1u << (b & 7))) {
                c1fs[i].dirty[b >> 3] &= (uint8_t)~(1u << (b & 7));
                c1fs[i].ndirty--;
                *file = i;
                *off = b << BLOCK_SHIFT;
                *len = c1fs[i].size - *off < (1u << BLOCK_SHIFT) ? c1fs[i].size - *off : (1u << BLOCK_SHIFT);
                return 0;
            }
        c1fs[i].ndirty = 0;
    }
    return -1;
}

unsigned c1fs_dirty_count(void)
{
    unsigned n = 0;
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        n += c1fs[i].ndirty;
    return n;
}

/* by the whole name, else by the last part (a config may say a path the
   front end shortened) */
static int find(const char *name)
{
    const char *base = strrchr(name, '/');
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        if (c1fs[i].data && !strcmp(c1fs[i].name, name))
            return i;
    base = base ? base + 1 : name;
    for (i = 0; i < C1FS_MAX; i++) {
        const char *b;

        if (!c1fs[i].data)
            continue;
        b = strrchr(c1fs[i].name, '/');
        if (!strcmp(b ? b + 1 : c1fs[i].name, base))
            return i;
    }
    return -1;
}

int _open(const char *name, int flags, int mode)
{
    int f = find(name), fd;

    (void)mode;
    if (f < 0) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & 3) != 0 && !(c1fs[f].flags & C1FS_RW)) {   /* O_WRONLY, O_RDWR */
        errno = EROFS;
        return -1;
    }
    for (fd = 3; fd < MAX_FD; fd++)
        if (!fds[fd].used) {
            fds[fd].used = 1;
            fds[fd].file = f;
            fds[fd].pos = 0;
            return fd;
        }
    errno = EMFILE;
    return -1;
}

int _close(int fd)
{
    if (fd >= 3 && fd < MAX_FD)
        fds[fd].used = 0;
    return 0;
}

int _read(int fd, void *buf, size_t n)
{
    struct c1file *f;

    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    f = &c1fs[fds[fd].file];
    if ((size_t)fds[fd].pos >= f->size)
        return 0;
    if (n > f->size - (size_t)fds[fd].pos)
        n = f->size - (size_t)fds[fd].pos;
    memcpy(buf, f->data + fds[fd].pos, n);
    fds[fd].pos += (off_t)n;
    return (int)n;
}

int _write(int fd, const void *buf, size_t n)
{
    struct c1file *f;
    size_t b;

    if (fd == 1 || fd == 2) {
        char tmp[128];
        size_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;

        memcpy(tmp, buf, k);
        tmp[k] = 0;
        c1p_log(tmp);
        return (int)n;
    }
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    f = &c1fs[fds[fd].file];
    if ((size_t)fds[fd].pos >= f->size)
        return 0;                       /* files keep their size */
    if (n > f->size - (size_t)fds[fd].pos)
        n = f->size - (size_t)fds[fd].pos;
    memcpy(f->data + fds[fd].pos, buf, n);
    for (b = (size_t)fds[fd].pos >> BLOCK_SHIFT; b <= ((size_t)fds[fd].pos + n - 1) >> BLOCK_SHIFT && n; b++)
        if (!(f->dirty[b >> 3] & (1u << (b & 7)))) {
            f->dirty[b >> 3] |= (uint8_t)(1u << (b & 7));
            f->ndirty++;
        }
    fds[fd].pos += (off_t)n;
    return (int)n;
}

off_t _lseek(int fd, off_t off, int whence)
{
    off_t p;

    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    p = whence == SEEK_SET ? off : whence == SEEK_CUR ? fds[fd].pos + off
                                                      : (off_t)c1fs[fds[fd].file].size + off;
    if (p < 0) {
        errno = EINVAL;
        return -1;
    }
    fds[fd].pos = p;
    return p;
}

static void fill(struct stat *st, int f)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | ((c1fs[f].flags & C1FS_RW) ? 0644 : 0444);
    st->st_size = (off_t)c1fs[f].size;
    st->st_blksize = 512;
}

int _fstat(int fd, struct stat *st)
{
    if (fd < 3) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR;
        return 0;
    }
    if (fd >= MAX_FD || !fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    fill(st, fds[fd].file);
    return 0;
}

int _stat(const char *name, struct stat *st)
{
    int f = find(name);

    if (f < 0) {
        errno = ENOENT;
        return -1;
    }
    fill(st, f);
    return 0;
}

int _isatty(int fd)
{
    return fd < 3;
}

int _unlink(const char *name)
{
    (void)name;
    errno = EROFS;
    return -1;
}

int _link(const char *a, const char *b)
{
    (void)a; (void)b;
    errno = EROFS;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}
