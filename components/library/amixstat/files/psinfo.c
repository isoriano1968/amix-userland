/* psinfo -- every process for libamixstat, installed setuid root as
 * /usr/local/lib/amixstat/psinfo.  AMIX's /proc lets a process open only
 * its own user's process files (ps is setuid root for the same reason), so
 * amixsysmon, amixtop and amixstat get the full list from here.
 *
 * It takes no arguments and reads no environment, files or input: it scans
 * /proc and writes "APS1", the record size, the count and the records
 * (struct ast_proc, the library's own scan) to its standard output. */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include "amixstat.h"

extern char **environ;
int ast__procs_scan(struct ast_proc **list);

static int write_all(const void *buf, size_t len)
{
    const char *p = buf;

    while (len > 0) {
        ssize_t w = write(1, p, len);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static char *noenv[] = { NULL };
    struct ast_proc *v;
    long hdr[3];
    int n;

    (void)argv;
    environ = noenv;
    if (argc != 1 || geteuid() != 0)
        return 2;
    if ((n = ast__procs_scan(&v)) < 0)
        return 1;
    hdr[0] = 0x41505331L;  /* "APS1" */
    hdr[1] = (long)sizeof(struct ast_proc);
    hdr[2] = n;
    if (write_all(hdr, sizeof hdr) < 0 || (n > 0 && write_all(v, (size_t)n * sizeof *v) < 0))
        return 1;
    return 0;
}
