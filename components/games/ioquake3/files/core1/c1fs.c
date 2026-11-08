/*
 * c1fs.c -- files on core 1 for ioquake3: newlib's system calls over the
 * files AMIX handed in at start-up (c1q3_board.h), held whole in core 1's
 * memory, and the ones the engine writes (q3config.cfg, screenshots ...).
 * A pk3 is just a big file here: the zip code reads it with fseek/fread.
 *
 * Only files under /home/ can be written or made; each write marks the
 * whole file, and AMIX fetches written files back (C1Q3_CMD_FLUSH), so they
 * should stay small.  stdout and stderr go to the log ring.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "c1fs.h"
#include "c1q3.h"
#include <errno.h>

#define ARENA_MIN   (1u << 20)          /* read-only files from 1 MB: c1p_file_alloc */
int _unlink(const char *name);

#define MAX_FD      64

struct c1file c1fs[C1FS_MAX];
static struct { int file; off_t pos; int used, append; } fds[MAX_FD];

static int writable(const char *name)
{
    return !strncmp(name, C1FS_WRITABLE, sizeof(C1FS_WRITABLE) - 1) && !strstr(name, "/../");
}

static int slot(const char *name)
{
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        if (!c1fs[i].name) {
            c1fs[i].name = strdup(name);
            if (!c1fs[i].name)
                return -1;
            c1fs[i].data = NULL;
            c1fs[i].size = c1fs[i].cap = 0;
            c1fs[i].flags = 0;
            c1fs[i].dirty = c1fs[i].removed = c1fs[i].arena = c1fs[i].remote = 0;
            c1fs[i].blk = NULL;
            c1fs[i].stamp = NULL;
            return i;
        }
    return -1;
}

/* --- remote files: blocks from the 68k, the least recently used go first --- */
static size_t cache_bytes;
static uint32_t cache_clock;

int c1fs_add_remote(const char *name, size_t size)
{
    const size_t nb = (size + C1FS_BLOCK - 1) / C1FS_BLOCK;
    int i = c1fs_find(name);

    if (i >= 0 || (i = slot(name)) < 0)
        return -1;                      /* a remote file is never given twice */
    c1fs[i].blk = calloc(nb ? nb : 1, sizeof(*c1fs[i].blk));
    c1fs[i].stamp = calloc(nb ? nb : 1, sizeof(*c1fs[i].stamp));
    if (!c1fs[i].blk || !c1fs[i].stamp) {
        free(c1fs[i].blk);
        free(c1fs[i].stamp);
        free(c1fs[i].name);
        c1fs[i].name = NULL;
        return -1;
    }
    c1fs[i].size = c1fs[i].cap = size;
    c1fs[i].remote = 1;
    return i;
}

static void evict_one(void)
{
    uint32_t best = 0xFFFFFFFFu;
    int bf = -1, f;
    size_t bb = 0, b;

    for (f = 0; f < C1FS_MAX; f++) {
        const struct c1file *c = &c1fs[f];

        if (!c->name || !c->remote)
            continue;
        for (b = 0; b < (c->size + C1FS_BLOCK - 1) / C1FS_BLOCK; b++)
            if (c->blk[b] && c->stamp[b] < best) {
                best = c->stamp[b];
                bf = f;
                bb = b;
            }
    }
    if (bf >= 0) {
        free(c1fs[bf].blk[bb]);
        c1fs[bf].blk[bb] = NULL;
        cache_bytes -= C1FS_BLOCK;
    }
}

/* block B of remote file F, fetched if need be; NULL if it cannot be had */
static uint8_t *block(int f, size_t b)
{
    struct c1file *c = &c1fs[f];
    const size_t off = b * C1FS_BLOCK;
    const size_t len = c->size - off < C1FS_BLOCK ? c->size - off : C1FS_BLOCK;

    if (!c->blk[b]) {
        uint8_t *p;

        while (cache_bytes + C1FS_BLOCK > C1FS_CACHE_MAX && cache_bytes)
            evict_one();
        if (!(p = malloc(C1FS_BLOCK)) || c1p_fetch(f, (uint32_t)off, (uint32_t)len, p) != (int)len) {
            free(p);
            return NULL;
        }
        c->blk[b] = p;
        cache_bytes += C1FS_BLOCK;
    }
    c->stamp[b] = ++cache_clock;
    return c->blk[b];
}

static int read_remote(int f, off_t pos, void *buf, size_t n)
{
    size_t done = 0;

    while (done < n) {
        const size_t at = (size_t)pos + done, b = at / C1FS_BLOCK, in = at % C1FS_BLOCK;
        const size_t k = n - done < C1FS_BLOCK - in ? n - done : C1FS_BLOCK - in;
        const uint8_t *p = block(f, b);

        if (!p) {
            errno = EIO;
            return done ? (int)done : -1;
        }
        memcpy((uint8_t *)buf + done, p + in, k);
        done += k;
    }
    return (int)done;
}

int c1fs_find(const char *name)
{
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        if (c1fs[i].name && !c1fs[i].removed && !strcmp(c1fs[i].name, name))
            return i;
    return -1;
}

int c1fs_add(const char *name, size_t size, int flags)
{
    int i = c1fs_find(name);

    if (i < 0 && (i = slot(name)) < 0)
        return -1;
    if (!c1fs[i].arena)
        free(c1fs[i].data);
    c1fs[i].arena = 0;
    c1fs[i].data = NULL;
    if (!(flags & C1FS_RW) && size >= ARENA_MIN && (c1fs[i].data = c1p_file_alloc(size)) != NULL)
        c1fs[i].arena = 1;
    else
        c1fs[i].data = malloc(size ? size : 1);
    if (!c1fs[i].data) {
        free(c1fs[i].name);
        c1fs[i].name = NULL;
        return -1;
    }
    c1fs[i].size = c1fs[i].cap = size;
    c1fs[i].flags = (flags & C1FS_RW) && writable(name) ? C1FS_RW : 0;
    return i;
}

unsigned c1fs_dirty_count(void)
{
    unsigned n = 0;
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        n += c1fs[i].name && c1fs[i].dirty;
    return n;
}

int c1fs_next_dirty(void)
{
    int i;

    for (i = 0; i < C1FS_MAX; i++)
        if (c1fs[i].name && c1fs[i].dirty)
            return i;
    return -1;
}

int c1fs_isdir(const char *path)
{
    size_t n = strlen(path);
    int i;

    while (n > 1 && path[n - 1] == '/')
        n--;
    if (n == 1 && path[0] == '/')
        return 1;
    for (i = 0; i < C1FS_MAX; i++)
        if (c1fs[i].name && !c1fs[i].removed && !strncmp(c1fs[i].name, path, n) && c1fs[i].name[n] == '/')
            return 1;
    return 0;
}

int c1fs_opendir(struct c1dir *d, const char *path)
{
    size_t n = strlen(path);

    while (n > 1 && path[n - 1] == '/')
        n--;
    if (n + 2 > sizeof(d->path) || !c1fs_isdir(path))
        return -1;
    memcpy(d->path, path, n);
    if (!(n == 1 && path[0] == '/'))
        d->path[n++] = '/';
    d->path[n] = 0;
    d->plen = n;
    d->pos = 0;
    d->nseen = 0;
    return 0;
}

int c1fs_readdir(struct c1dir *d)
{
    for (; d->pos < C1FS_MAX; d->pos++) {
        const struct c1file *f = &c1fs[d->pos];
        const char *rest, *slash;
        size_t k;
        int j;

        if (!f->name || f->removed || strncmp(f->name, d->path, d->plen))
            continue;
        rest = f->name + d->plen;
        slash = strchr(rest, '/');
        if (!slash) {
            strncpy(d->name, rest, sizeof(d->name) - 1);
            d->name[sizeof(d->name) - 1] = 0;
            d->is_dir = 0;
            d->pos++;
            return 1;
        }
        k = (size_t)(slash - rest);
        if (!k || k >= sizeof(d->seen[0]))
            continue;
        for (j = 0; j < d->nseen; j++)
            if (!strncmp(d->seen[j], rest, k) && !d->seen[j][k])
                break;
        if (j < d->nseen)
            continue;
        if (d->nseen < (int)(sizeof(d->seen) / sizeof(d->seen[0]))) {
            memcpy(d->seen[d->nseen], rest, k);
            d->seen[d->nseen++][k] = 0;
        }
        memcpy(d->name, rest, k);
        d->name[k] = 0;
        d->is_dir = 1;
        d->pos++;
        return 1;
    }
    return 0;
}

/* --- newlib's system calls --- */

static int grow(struct c1file *f, size_t need)
{
    uint8_t *p;
    size_t cap;

    if (need <= f->cap)
        return 0;
    cap = f->cap ? f->cap : 4096;
    while (cap < need)
        cap *= 2;
    if (f->arena) {                     /* not malloc's: copy out of the arena */
        p = malloc(cap);
        if (p)
            memcpy(p, f->data, f->size);
    } else
        p = realloc(f->data, cap);
    if (!p)
        return -1;
    f->arena = 0;
    f->data = p;
    f->cap = cap;
    return 0;
}

int _open(const char *name, int flags, int mode)
{
    int f = c1fs_find(name), fd;
    const int wr = (flags & O_ACCMODE) != O_RDONLY;

    (void)mode;
    if (wr && (!writable(name) || (f >= 0 && c1fs[f].remote))) {
        errno = f < 0 ? ENOENT : EROFS;
        return -1;
    }
    if (f < 0) {
        if (!(flags & O_CREAT)) {
            errno = ENOENT;
            return -1;
        }
        if ((f = slot(name)) < 0) {
            errno = ENOSPC;
            return -1;
        }
        c1fs[f].flags = C1FS_RW;
        c1fs[f].dirty = 1;
    } else if (flags & O_EXCL && flags & O_CREAT) {
        errno = EEXIST;
        return -1;
    }
    if (wr)
        c1fs[f].flags |= C1FS_RW;
    if (wr && (flags & O_TRUNC)) {
        c1fs[f].size = 0;
        c1fs[f].dirty = 1;
    }
    for (fd = 3; fd < MAX_FD; fd++)
        if (!fds[fd].used) {
            fds[fd].used = 1;
            fds[fd].file = f;
            fds[fd].pos = 0;
            fds[fd].append = (flags & O_APPEND) != 0;
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
    if (f->remote) {
        const int r = read_remote(fds[fd].file, fds[fd].pos, buf, n);

        if (r > 0)
            fds[fd].pos += r;
        return r;
    }
    memcpy(buf, f->data + fds[fd].pos, n);
    fds[fd].pos += (off_t)n;
    return (int)n;
}

int _write(int fd, const void *buf, size_t n)
{
    struct c1file *f;

    if (fd == 1 || fd == 2) {
        char tmp[257];
        size_t k;

        for (k = 0; k < n; k += sizeof(tmp) - 1) {
            const size_t m = n - k < sizeof(tmp) - 1 ? n - k : sizeof(tmp) - 1;

            memcpy(tmp, (const char *)buf + k, m);
            tmp[m] = 0;
            c1p_log(tmp);
        }
        return (int)n;
    }
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    f = &c1fs[fds[fd].file];
    if (!(f->flags & C1FS_RW)) {
        errno = EBADF;
        return -1;
    }
    if (fds[fd].append)
        fds[fd].pos = (off_t)f->size;
    if (grow(f, (size_t)fds[fd].pos + n) < 0) {
        errno = ENOSPC;
        return -1;
    }
    if ((size_t)fds[fd].pos > f->size)
        memset(f->data + f->size, 0, (size_t)fds[fd].pos - f->size);
    memcpy(f->data + fds[fd].pos, buf, n);
    fds[fd].pos += (off_t)n;
    if ((size_t)fds[fd].pos > f->size)
        f->size = (size_t)fds[fd].pos;
    f->dirty = 1;
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
    st->st_blksize = 4096;
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
    int f = c1fs_find(name);

    if (f < 0) {
        if (c1fs_isdir(name)) {
            memset(st, 0, sizeof(*st));
            st->st_mode = S_IFDIR | 0755;
            return 0;
        }
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
    int f = c1fs_find(name), fd;

    if (f < 0) {
        errno = ENOENT;
        return -1;
    }
    if (!writable(name)) {
        errno = EROFS;
        return -1;
    }
    for (fd = 3; fd < MAX_FD; fd++)
        if (fds[fd].used && fds[fd].file == f) {
            errno = EBUSY;
            return -1;
        }
    if (!c1fs[f].arena)
        free(c1fs[f].data);
    c1fs[f].data = NULL;
    c1fs[f].arena = 0;
    c1fs[f].size = c1fs[f].cap = 0;
    c1fs[f].removed = 1;
    c1fs[f].dirty = 1;                  /* the 68k removes it, then the slot is free */
    return 0;
}

/* newlib's rename() is _link() + _unlink() */
int _link(const char *from, const char *to)
{
    int f = c1fs_find(from), t;

    if (f < 0) {
        errno = ENOENT;
        return -1;
    }
    if (!writable(to)) {
        errno = EROFS;
        return -1;
    }
    if (c1fs_find(to) >= 0 && _unlink(to) < 0)    /* rename() replaces the target */
        return -1;
    if ((t = slot(to)) < 0 || grow(&c1fs[t], c1fs[f].size ? c1fs[f].size : 1) < 0) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(c1fs[t].data, c1fs[f].data, c1fs[f].size);
    c1fs[t].size = c1fs[f].size;
    c1fs[t].flags = C1FS_RW;
    c1fs[t].dirty = 1;
    return 0;
}

mode_t umask(mode_t mask)
{
    (void)mask;
    return 022;
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
