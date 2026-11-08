/* sys/statvfs.h for core 1: nothing to ask */
#ifndef C1_SYS_STATVFS_H
#define C1_SYS_STATVFS_H
struct statvfs { unsigned long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail, f_flag, f_namemax; };
static inline int statvfs(const char *p, struct statvfs *s) { (void)p; (void)s; return -1; }
#endif
