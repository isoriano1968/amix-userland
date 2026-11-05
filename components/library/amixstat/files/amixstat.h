/*
 * amixstat.h -- what the running AMIX system is doing: memory, swap, CPU,
 * processes, file systems and network interfaces (libamixstat).
 *
 * AMIX has no kstat and its sar finds the kernel through /unix, which is not
 * the kernel that runs.  ast_open() finds the running kernel in /stand by the
 * build id uname -a prints and reads its counters through /dev/kmem, so the
 * caller needs group sys (or root).  Processes come from /proc: all of them
 * for root, one's own otherwise.
 */
#ifndef AMIXSTAT_H
#define AMIXSTAT_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The structures hold no double: AMIX's gcc 2.7 aligns a double in a struct
   to 8 bytes, GCC 14 to 4, and the library serves programs of both. */

/* memory and swap in KB */
struct ast_mem {
    unsigned long pagesize;     /* bytes */
    unsigned long total;        /* physmem */
    unsigned long free;         /* freemem */
    unsigned long swap_total;   /* anoninfo: the swap devices */
    unsigned long swap_free;
    unsigned long swap_resv;    /* reserved by processes, not yet written */
};

/* The CPU.  The kernel's own counters (user, kernel, wait, in ticks since
   boot) book idle time as kernel time, and so do the processes' times: AMIX
   charges an idle tick to the process that ran last.  The idle share comes
   from the kernel profiler instead: with prf set, s_user, s_kernel and s_idle
   count the clock ticks since ast_open() found the CPU in user mode, in the
   kernel and stopped in the idle loop; differences over an interval give the
   shares.  prf is 0 when the profiler is in use for something else. */
struct ast_cpu {
    unsigned long user, kernel, wait;
    unsigned long lbolt;    /* ticks since boot */
    unsigned long hz;
    int prf;
    unsigned long s_user, s_kernel, s_idle;
};

struct ast_proc {
    pid_t pid, ppid;
    uid_t uid;
    char state;             /* O R S T Z ... as ps prints it */
    int nice;
    long pri;
    unsigned long size;     /* KB */
    unsigned long rss;      /* KB */
    unsigned long cpu;      /* ms, user + system */
    unsigned long utime, stime; /* ms, the two apart (0 for a zombie) */
    time_t start;
    char clname[8];
    char fname[16];
    char args[80];
};

struct ast_fs {
    char dev[64], dir[64], type[16];
    unsigned long total, free, avail;  /* KB; avail: what a non-root user may still use */
};

struct ast_if {
    char name[16];
    int up;
    unsigned long ipackets, opackets, ierrors, oerrors, collisions;
};

/* find the running kernel and its counters; 0, or -1 with a reason in err */
int ast_open(char *err, int errlen);
const char *ast_kernel(void);       /* the kernel file in /stand */
void ast_close(void);

int ast_mem(struct ast_mem *m);
int ast_cpu(struct ast_cpu *c);
/* the processes, in a malloc'd array (free it); their count, or -1 */
int ast_procs(struct ast_proc **list);
int ast_filesystems(struct ast_fs **list);
int ast_ifs(struct ast_if **list);

#ifdef __cplusplus
}
#endif
#endif
