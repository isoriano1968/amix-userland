/*
 * amixstat [seconds] -- what libamixstat reads: the kernel, memory, swap, the
 * file systems, the network interfaces and the processes; with an interval,
 * the CPU's busy and user share over it and each process's share.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "amixstat.h"

#define MB(kb) ((kb) / 1024.0)
#define SEC(a, b) ((long)((a) - (b)) / 1000.0)  /* ms counters: seconds between */

static const struct ast_proc *find(const struct ast_proc *v, int n, pid_t pid)
{
    int i;

    for (i = 0; i < n; i++)
        if (v[i].pid == pid)
            return &v[i];
    return NULL;
}

int main(int argc, char **argv)
{
    char err[200];
    struct ast_mem m;
    struct ast_cpu c0, c1;
    struct ast_fs *fs;
    struct ast_if *ifs;
    struct ast_proc *p0, *p1;
    int n0, n1, nf, ni, i, secs = argc > 1 ? atoi(argv[1]) : 0;
    double busy = 0, user = 0, t0;

    if (ast_open(err, sizeof err) < 0) {
        fprintf(stderr, "amixstat: %s\n", err);
        return 1;
    }
    printf("kernel %s\n", ast_kernel());
    if (ast_mem(&m) == 0)
        printf("memory %.1f MB, free %.1f MB; swap %.1f MB, free %.1f MB, reserved %.1f MB\n",
               MB(m.total), MB(m.free), MB(m.swap_total), MB(m.swap_free), MB(m.swap_resv));
    nf = ast_filesystems(&fs);
    for (i = 0; i < nf; i++)
        printf("fs %-18s %-12s %-5s %8.1f MB, free %8.1f MB, available %8.1f MB\n",
               fs[i].dev, fs[i].dir, fs[i].type, MB(fs[i].total), MB(fs[i].free), MB(fs[i].avail));
    free(fs);
    ni = ast_ifs(&ifs);
    for (i = 0; i < ni; i++)
        printf("if %-6s %s in %lu out %lu packets, errors %lu/%lu, collisions %lu\n", ifs[i].name,
               ifs[i].up ? "up  " : "down", ifs[i].ipackets, ifs[i].opackets,
               ifs[i].ierrors, ifs[i].oerrors, ifs[i].collisions);
    free(ifs);
    ast_cpu(&c0);
    n0 = ast_procs(&p0);
    if (secs <= 0) {
        printf("cpu since boot: %lu ticks at %lu Hz, user %lu, kernel+idle %lu\n",
               c0.lbolt, c0.hz, c0.user, c0.kernel);
        for (i = 0; i < n0; i++)
            printf("%6ld %6ld %5ld %c %3d %6.1f MB %6.1f MB %9.1f s (%.1f + %.1f) %s\n",
                   (long)p0[i].pid, (long)p0[i].ppid, (long)p0[i].uid, p0[i].state, p0[i].nice,
                   MB(p0[i].size), MB(p0[i].rss), p0[i].cpu / 1000.0, p0[i].utime / 1000.0,
                   p0[i].stime / 1000.0, p0[i].args);
        free(p0);
        return 0;
    }
    sleep(secs);
    ast_cpu(&c1);
    n1 = ast_procs(&p1);
    t0 = (double)(c1.lbolt - c0.lbolt) / c1.hz;
    for (i = 0; i < n1; i++) {
        const struct ast_proc *a = find(p0, n0, p1[i].pid);
        busy += SEC(p1[i].cpu, a ? a->cpu : 0);
        user += SEC(p1[i].utime, a ? a->utime : 0);
    }
    printf("cpu over %.2f s: the processes' times %.1f %%, user %.1f %%\n", t0, 100 * busy / t0, 100 * user / t0);
    if (c1.prf) {
        double u = c1.s_user - c0.s_user, k = c1.s_kernel - c0.s_kernel, d = c1.s_idle - c0.s_idle;
        double all = u + k + d;
        if (all > 0)
            printf("cpu over %.0f ticks (prf): user %.1f %%, kernel %.1f %%, idle %.1f %%\n",
                   all, 100 * u / all, 100 * k / all, 100 * d / all);
    } else {
        printf("cpu: no prf (in use for something else, or no /dev/prf)\n");
    }
    for (i = 0; i < n1; i++) {
        const struct ast_proc *a = find(p0, n0, p1[i].pid);
        double d = SEC(p1[i].cpu, a ? a->cpu : 0);
        if (d > 0)
            printf("%6ld %5.1f %% %s\n", (long)p1[i].pid, 100 * d / t0, p1[i].fname);
    }
    free(p0);
    free(p1);
    return 0;
}
