/* c1fs.h -- core 1's files in memory (c1fs.c): what AMIX handed in at
 * start-up (the paks, the user's config) and what the engine writes, by
 * full path.  Directories are only implied by the paths. */
#ifndef C1FS_H
#define C1FS_H
#include <stddef.h>
#include <stdint.h>

#define C1FS_MAX        512
#define C1FS_RW         1               /* = C1Q3_FILE_RW: written back */
#define C1FS_WRITABLE   "/home/"        /* where the engine may write */

struct c1file {
    char *name;                         /* NULL: a free slot */
    uint8_t *data;
    size_t size, cap;
    int flags;
    int dirty;                          /* written since the last flush */
    int removed;                        /* unlinked: the 68k removes it too */
    int arena;                          /* data from c1p_file_alloc: never freed */
    int remote;                         /* blocks from the 68k as they are read (c1p_fetch) */
    uint8_t **blk;                      /* remote: the cached blocks, NULL where not */
    uint32_t *stamp;                    /* remote: when each block was last used */
};

extern struct c1file c1fs[C1FS_MAX];

/* a file of SIZE bytes for the 68k to fill (CMD_OPEN): its number, or -1 */
int c1fs_add(const char *name, size_t size, int flags);
/* a file of SIZE bytes the 68k serves by request (read-only): its number, or -1 */
int c1fs_add_remote(const char *name, size_t size);
#define C1FS_BLOCK      (256u << 10)    /* remote files' block */
#ifndef C1FS_CACHE_MAX
#define C1FS_CACHE_MAX  (48u << 20)     /* what remote files may hold in memory */
#endif
/* the file by its path, or -1 */
int c1fs_find(const char *name);
/* number of files written since their last flush */
unsigned c1fs_dirty_count(void);
/* the next written file (dirty is cleared by the caller when it is out), or -1 */
int c1fs_next_dirty(void);
/* 1 if PATH is a directory (some file lies below it) */
int c1fs_isdir(const char *path);

/* the names right below DIR (no "." or ".."); is_dir says which are directories */
struct c1dir {
    char path[256];
    size_t plen;
    int pos;
    char seen[64][64];                  /* directories already given */
    int nseen;
    char name[256];
    int is_dir;
};
int c1fs_opendir(struct c1dir *d, const char *path);
int c1fs_readdir(struct c1dir *d);      /* 1 and d->name, d->is_dir; 0 at the end */

#endif
