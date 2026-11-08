/* sys/statfs.h for core 1: nothing to ask */
#ifndef C1_SYS_STATFS_H
#define C1_SYS_STATFS_H
struct statfs { long f_bsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree; };
static inline int statfs(const char *p, struct statfs *s) { (void)p; (void)s; return -1; }
#endif
