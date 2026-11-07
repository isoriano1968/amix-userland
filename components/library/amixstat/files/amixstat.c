/*
 * amixstat.c -- libamixstat: the running AMIX system's counters (amixstat.h).
 *
 * The kernel.  Our kernels in /stand are relocatable ELF files that the boot
 * loader puts at 0x08000000, their allocated sections one after the other in
 * header order.  The file that runs is the one whose build id matches the
 * one uname -a prints, apart from the CPU (the file says 68040, a 68060
 * reports 68060).  A symbol defined in a section has its address from the
 * section map; a COMMON one (physmem, freemem, sysinfo ...) is placed at
 * load time, so its address is read out of the live kernel at the
 * R_68K_32 relocation sites in .text that use it, and they must all agree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/procfs.h>
#include <sys/mnttab.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include "amixstat.h"

#define KBASE       0x08000000UL
#define STAND       "/stand"
#define SHN_COMMON  0xFFF2

enum { K_PHYSMEM, K_FREEMEM, K_ANONINFO, K_SYSINFO, K_LBOLT, K_IFSTATS, K_IDLE, K_N };
static const char *const knames[K_N] = {
    "physmem", "freemem", "anoninfo", "sysinfo", "lbolt", "ifstats", "idle"
};
static unsigned long kaddr[K_N];
static int kmem = -1, prf = -1;

/* The CPU's idle share.  AMIX's idle() is "stop #0x2000; rts", and the clock
   books its ticks as kernel time of whatever process ran last, so neither
   sysinfo nor the processes' times show it.  The kernel profiler does: three
   buckets, the kernel's start, idle and idle's end, and prf counts at every
   clock tick whether the CPU was in user mode, in the kernel, or stopped in
   idle.  prf is set up only when it is off or already has these buckets, so
   a profiling run someone started is left alone. */
#define PRF_IDLE_LEN 6          /* stop #0x2000 (4 bytes), rts (2) */

static void prf_setup(void)
{
    unsigned long want[3], have[7];
    int n, state;

    if (!kaddr[K_IDLE] || (prf = open("/dev/prf", O_RDWR)) < 0)
        return;
    want[0] = KBASE;
    want[1] = kaddr[K_IDLE];
    want[2] = kaddr[K_IDLE] + PRF_IDLE_LEN;
    state = ioctl(prf, 1, 0);
    n = ioctl(prf, 2, 0);
    if (state > 0 && (state & 1)) {
        /* on: ours (another amixstat), or someone's profile */
        if (n == 3 && read(prf, have, 7 * 4) == 7 * 4 && !memcmp(have, want, sizeof want))
            return;
        close(prf);
        prf = -1;
        return;
    }
    if (write(prf, want, sizeof want) != sizeof want || ioctl(prf, 3, 1) < 0) {
        close(prf);
        prf = -1;
    }
}
static char kfile[300];

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static unsigned be16(const unsigned char *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static int kread(unsigned long addr, void *buf, int n)
{
    if (kmem < 0 || lseek(kmem, (off_t)addr, SEEK_SET) == (off_t)-1)
        return -1;
    return read(kmem, buf, n) == n ? 0 : -1;
}

static int kword(unsigned long addr, unsigned long *v)
{
    unsigned char b[4];

    if (kread(addr, b, 4) < 0)
        return -1;
    *v = be32(b);
    return 0;
}

/* s appended to d (size n, always terminated); no snprintf: SVR4.0's libc
   has none, and libamixc99's would be exported from this library */
static void cat(char *d, int n, const char *s)
{
    int l = (int)strlen(d);

    while (*s && l < n - 1)
        d[l++] = *s++;
    d[l] = 0;
}

static void copy(char *d, int n, const char *s)
{
    d[0] = 0;
    cat(d, n, s ? s : "");
}

static void fail(char *err, int len, const char *what, const char *arg)
{
    if (err && len > 0) {
        copy(err, len, what);
        if (arg) {
            cat(err, len, ": ");
            cat(err, len, arg);
        }
    }
}

/* the build id uname -a prints, e.g. 68060-261104-x2; "-261104-x2" is kept */
static int build_id(char *id, int len)
{
    char line[256], *p, *q;
    FILE *f = popen("uname -a", "r");

    id[0] = 0;
    if (!f)
        return -1;
    if (!fgets(line, sizeof line, f))
        line[0] = 0;
    pclose(f);
    for (p = line; (p = strstr(p, "680")) != NULL; p++) {
        if (p[3] < '0' || p[3] > '9' || p[4] != '0' || p[5] != '-')
            continue;
        for (q = p + 5; *q && *q != ' ' && *q != '\n'; q++)
            ;
        if (q - (p + 5) >= len)
            return -1;
        memcpy(id, p + 5, q - (p + 5));
        id[q - (p + 5)] = 0;
        return 0;
    }
    return -1;
}

static unsigned char *slurp(const char *path, long *size)
{
    struct stat st;
    unsigned char *d;
    int fd = open(path, O_RDONLY);

    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) < 0 || st.st_size < 52 || !(d = malloc(st.st_size))) {
        close(fd);
        return NULL;
    }
    if (read(fd, d, st.st_size) != st.st_size) {
        free(d);
        close(fd);
        return NULL;
    }
    close(fd);
    *size = st.st_size;
    return d;
}

/* does d hold 680x0 followed by id (and not by more of an id)? */
static int has_id(const unsigned char *d, long n, const char *id)
{
    long i, l = (long)strlen(id);

    for (i = 0; i + 5 + l < n; i++) {
        if (d[i] == '6' && d[i + 1] == '8' && d[i + 2] == '0' && d[i + 4] == '0' &&
            d[i + 3] >= '0' && d[i + 3] <= '9' && !memcmp(d + i + 5, id, l)) {
            int c = d[i + 5 + l];
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z'))
                return 1;
        }
    }
    return 0;
}

/* the counters' addresses out of the kernel file d, with the live kernel */
static int resolve(const unsigned char *d, long n, char *err, int errlen)
{
    unsigned long shoff = be32(d + 0x20), *vbase, run = 0;
    unsigned shent = be16(d + 0x2E), shnum = be16(d + 0x30);
    unsigned i, k, symsec = 0;
    const unsigned char *sh, *sym, *str;
    unsigned long nsym, *sites[K_N];
    int nsites[K_N], found = 1;
    int *symk;

    if (shoff + (unsigned long)shent * shnum > (unsigned long)n || shent < 40) {
        fail(err, errlen, "kernel file has no section table", kfile);
        return -1;
    }
    vbase = calloc(shnum, sizeof *vbase);
    for (i = 0; i < shnum; i++) {
        sh = d + shoff + i * shent;
        vbase[i] = (unsigned long)-1;
        if (be32(sh + 8) & 2) {         /* SHF_ALLOC */
            vbase[i] = run;
            run += be32(sh + 20);
        }
        if (be32(sh + 4) == 2)          /* SHT_SYMTAB */
            symsec = i;
    }
    if (!symsec) {
        free(vbase);
        fail(err, errlen, "kernel file has no symbol table", kfile);
        return -1;
    }
    sh = d + shoff + symsec * shent;
    sym = d + be32(sh + 16);
    nsym = be32(sh + 20) / 16;
    str = d + be32(d + shoff + be32(sh + 24) * shent + 16);
    symk = malloc(nsym * sizeof *symk);
    for (k = 0; k < K_N; k++) {
        kaddr[k] = 0;
        sites[k] = NULL;
        nsites[k] = 0;
    }
    for (i = 0; i < nsym; i++) {
        const unsigned char *s = sym + i * 16;
        unsigned shndx = be16(s + 14);

        symk[i] = -1;
        if (!be32(s) || (s[12] >> 4) != 1)     /* global ones only: there are local idle's */
            continue;
        for (k = 0; k < K_N; k++) {
            if (strcmp((const char *)str + be32(s), knames[k]))
                continue;
            symk[i] = k;
            if (shndx && shndx < shnum && vbase[shndx] != (unsigned long)-1)
                kaddr[k] = KBASE + vbase[shndx] + be32(s + 4);
        }
    }
    /* the relocation sites in .text of the COMMON ones */
    for (i = 0; i < shnum; i++) {
        unsigned long off, cnt, r, tsec;

        sh = d + shoff + i * shent;
        if (be32(sh + 4) != 4)          /* SHT_RELA */
            continue;
        tsec = be32(sh + 28);
        if (tsec >= shnum || vbase[tsec] == (unsigned long)-1 ||
            strcmp((const char *)d + be32(d + shoff + be16(d + 0x32) * shent + 16) +
                   be32(d + shoff + tsec * shent), ".text"))
            continue;
        off = be32(sh + 16);
        cnt = be32(sh + 20) / 12;
        for (r = 0; r < cnt; r++) {
            const unsigned char *e = d + off + r * 12;
            unsigned long info = be32(e + 4), si = info >> 8;
            int kk;

            if ((info & 0xFF) != 1 || be32(e + 8) || si >= nsym || (kk = symk[si]) < 0 || kaddr[kk])
                continue;
            sites[kk] = realloc(sites[kk], (nsites[kk] + 1) * sizeof **sites);
            sites[kk][nsites[kk]++] = KBASE + vbase[tsec] + be32(e);
        }
    }
    for (k = 0; k < K_N; k++) {
        int j;
        unsigned long a = 0, v;

        for (j = 0; j < nsites[k]; j++) {
            if (kword(sites[k][j], &v) < 0 || (j && v != a)) {
                a = 0;
                break;
            }
            a = v;
        }
        if (!kaddr[k])
            kaddr[k] = a;
        if (!kaddr[k] && k != K_IFSTATS && k != K_IDLE)
            found = 0;
        free(sites[k]);
    }
    free(symk);
    free(vbase);
    if (!found) {
        for (k = 0; k < K_N && kaddr[k]; k++)
            ;
        fail(err, errlen, "kernel counter not found", knames[k]);
        return -1;
    }
    return 0;
}

int ast_open(char *err, int errlen)
{
    char id[32], path[300];
    DIR *dir;
    struct dirent *e;
    unsigned char *d = NULL;
    long n = 0;

    if (kmem >= 0)
        return 0;
    if (build_id(id, sizeof id) < 0) {
        fail(err, errlen, "uname -a names no kernel build id", NULL);
        return -1;
    }
    if ((kmem = open("/dev/kmem", O_RDONLY)) < 0) {
        fail(err, errlen, "cannot read /dev/kmem (needs group sys)", NULL);
        return -1;
    }
    if (!(dir = opendir(STAND))) {
        fail(err, errlen, "cannot read", STAND);
        ast_close();
        return -1;
    }
    kfile[0] = 0;
    while ((e = readdir(dir)) != NULL) {
        struct stat st;

        copy(path, sizeof path, STAND "/");
        cat(path, sizeof path, e->d_name);
        if (stat(path, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 500000)
            continue;
        if (!(d = slurp(path, &n)))
            continue;
        if (!memcmp(d, "\177ELF", 4) && d[5] == 2 && has_id(d, n, id)) {
            copy(kfile, sizeof kfile, path);
            break;
        }
        free(d);
        d = NULL;
    }
    closedir(dir);
    if (!d) {
        fail(err, errlen, "no kernel in /stand has the build id", id);
        ast_close();
        return -1;
    }
    if (resolve(d, n, err, errlen) < 0) {
        free(d);
        ast_close();
        return -1;
    }
    free(d);
    prf_setup();
    return 0;
}

const char *ast_kernel(void)
{
    return kfile;
}

void ast_close(void)
{
    if (kmem >= 0)
        close(kmem);
    if (prf >= 0)
        close(prf);     /* prf stays on, for the next one */
    kmem = prf = -1;
}

int ast_mem(struct ast_mem *m)
{
    unsigned long phys, fr, ani[3];
    unsigned char b[12];

    memset(m, 0, sizeof *m);
    m->pagesize = (unsigned long)sysconf(_SC_PAGESIZE);
    if (kword(kaddr[K_PHYSMEM], &phys) < 0 || kword(kaddr[K_FREEMEM], &fr) < 0 ||
        kread(kaddr[K_ANONINFO], b, 12) < 0)
        return -1;
    ani[0] = be32(b);
    ani[1] = be32(b + 4);
    ani[2] = be32(b + 8);
    m->total = phys * (m->pagesize / 1024);
    m->free = fr * (m->pagesize / 1024);
    m->swap_total = ani[0] * (m->pagesize / 1024);
    m->swap_free = ani[1] * (m->pagesize / 1024);
    m->swap_resv = ani[2] * (m->pagesize / 1024);
    return 0;
}

int ast_cpu(struct ast_cpu *c)
{
    unsigned char b[20];

    memset(c, 0, sizeof *c);
    c->hz = (unsigned long)sysconf(_SC_CLK_TCK);
    if (kread(kaddr[K_SYSINFO], b, 20) < 0 || kword(kaddr[K_LBOLT], &c->lbolt) < 0)
        return -1;
    c->user = be32(b + 4);      /* cpu[CPU_USER] */
    c->kernel = be32(b + 8);    /* cpu[CPU_KERNEL], idle included */
    c->wait = be32(b + 12);     /* cpu[CPU_WAIT] */
    if (prf >= 0) {
        unsigned long v[7];

        /* the three addresses, their counts, then user mode */
        if (lseek(prf, 0, SEEK_SET) != (off_t)-1 && read(prf, v, sizeof v) == sizeof v) {
            c->prf = 1;
            c->s_kernel = v[3] + v[5];
            c->s_idle = v[4];
            c->s_user = v[6];
        }
    }
    return 0;
}

/* every process, as root: the setuid helper psinfo calls this */
int ast__procs_scan(struct ast_proc **list)
{
    DIR *dir = opendir("/proc");
    struct dirent *e;
    struct ast_proc *v = NULL;
    int n = 0, cap = 0;
    unsigned long pg = (unsigned long)sysconf(_SC_PAGESIZE);

    /* /proc opens a process's file only for a caller with that process's
     * real, effective and saved ids; a setgid sys program (amixsysmon,
     * amixtop, amixstat) has egid sys and is refused even its own processes.
     * Run the scan with the real gid; the saved gid sys brings it back. */
    gid_t egid = getegid(), rgid = getgid();

    *list = NULL;
    if (!dir)
        return -1;
    if (egid != rgid)
        setgid(rgid);
    while ((e = readdir(dir)) != NULL) {
        char path[64];
        prpsinfo_t ps;
        prstatus_t st;
        struct ast_proc *p;
        int fd;

        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        copy(path, sizeof path, "/proc/");
        cat(path, sizeof path, e->d_name);
        if ((fd = open(path, O_RDONLY)) < 0)
            continue;
        if (ioctl(fd, PIOCPSINFO, &ps) < 0) {
            close(fd);
            continue;
        }
        if (ps.pr_zomb || ioctl(fd, PIOCSTATUS, &st) < 0)
            memset(&st, 0, sizeof st);
        close(fd);
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            v = realloc(v, cap * sizeof *v);
        }
        p = &v[n++];
        memset(p, 0, sizeof *p);
        p->pid = ps.pr_pid;
        p->ppid = ps.pr_ppid;
        p->uid = ps.pr_uid;
        p->state = ps.pr_zomb ? 'Z' : ps.pr_sname;
        p->nice = ps.pr_nice;
        p->pri = ps.pr_pri;
        p->size = (unsigned long)ps.pr_size * (pg / 1024);
        p->rss = (unsigned long)ps.pr_rssize * (pg / 1024);
        p->cpu = ps.pr_time.tv_sec * 1000UL + ps.pr_time.tv_nsec / 1000000;
        p->utime = st.pr_utime.tv_sec * 1000UL + st.pr_utime.tv_nsec / 1000000;
        p->stime = st.pr_stime.tv_sec * 1000UL + st.pr_stime.tv_nsec / 1000000;
        p->start = ps.pr_start.tv_sec;
        memcpy(p->clname, ps.pr_clname, sizeof p->clname - 1);
        memcpy(p->fname, ps.pr_fname, sizeof p->fname - 1);
        memcpy(p->args, ps.pr_psargs, sizeof p->args - 1 < PRARGSZ ? sizeof p->args - 1 : PRARGSZ);
    }
    closedir(dir);
    if (egid != rgid)
        setgid(egid);
    *list = v;
    return n;
}

/*
 * AMIX's /proc lets a process open only its own user's process files.  The
 * setuid root helper AMIXSTAT_PSINFO reads every process (its scan is
 * ast__procs_scan) and writes them to a pipe: "APS1", the record size, the
 * count and the records.  Without the helper, or when it fails, the list
 * has the caller's own processes only.
 */
#ifndef AMIXSTAT_PSINFO
#define AMIXSTAT_PSINFO "/usr/local/lib/amixstat/psinfo"
#endif

static int read_all(int fd, void *buf, size_t len)
{
    char *p = buf;

    while (len > 0) {
        ssize_t r = read(fd, p, len);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static int procs_helper(struct ast_proc **list)
{
    int fd[2], status;
    long hdr[3];
    pid_t pid;
    struct ast_proc *v = NULL;
    char *noenv[] = { NULL };

    if (access(AMIXSTAT_PSINFO, X_OK) < 0 || pipe(fd) < 0)
        return -1;
    if ((pid = fork()) < 0) {
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fd[1], 1);
        close(fd[0]);
        close(fd[1]);
        execle(AMIXSTAT_PSINFO, "psinfo", (char *)NULL, noenv);
        _exit(127);
    }
    close(fd[1]);
    if (read_all(fd[0], hdr, sizeof hdr) == 0 && hdr[0] == 0x41505331L &&
        hdr[1] == (long)sizeof(struct ast_proc) && hdr[2] >= 0 && hdr[2] < 100000 &&
        (hdr[2] == 0 || (v = malloc((size_t)hdr[2] * sizeof *v)) != NULL) &&
        (hdr[2] == 0 || read_all(fd[0], v, (size_t)hdr[2] * sizeof *v) == 0)) {
        close(fd[0]);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        *list = v;
        return (int)hdr[2];
    }
    free(v);
    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return -1;
}

int ast_procs(struct ast_proc **list)
{
    int n;

    *list = NULL;
    if (geteuid() != 0 && (n = procs_helper(list)) >= 0)
        return n;
    return ast__procs_scan(list);
}

int ast_filesystems(struct ast_fs **list)
{
    FILE *f = fopen(MNTTAB, "r");
    struct mnttab mt;
    struct ast_fs *v = NULL;
    int n = 0;

    *list = NULL;
    if (!f)
        return -1;
    while (getmntent(f, &mt) == 0) {
        struct statvfs sv;
        struct ast_fs *p;
        unsigned long fr;

        if (statvfs(mt.mnt_mountp, &sv) < 0 || !sv.f_blocks)
            continue;
        v = realloc(v, (n + 1) * sizeof *v);
        p = &v[n++];
        memset(p, 0, sizeof *p);
        copy(p->dev, sizeof p->dev, mt.mnt_special);
        copy(p->dir, sizeof p->dir, mt.mnt_mountp);
        copy(p->type, sizeof p->type, mt.mnt_fstype ? mt.mnt_fstype : sv.f_basetype);
        fr = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
        /* in KB, without overflowing on a big disk with 512-byte blocks */
        p->total = fr >= 1024 ? sv.f_blocks * (fr / 1024) : sv.f_blocks / (1024 / fr);
        p->free = fr >= 1024 ? sv.f_bfree * (fr / 1024) : sv.f_bfree / (1024 / fr);
        p->avail = fr >= 1024 ? sv.f_bavail * (fr / 1024) : sv.f_bavail / (1024 / fr);
    }
    fclose(f);
    *list = v;
    return n;
}

/* the interfaces from the IP module's ifstats chain (TCP/IP is STREAMS-based:
   its counters are there, not in ifnet) */
int ast_ifs(struct ast_if **list)
{
    unsigned long a;
    struct ast_if *v = NULL;
    int n = 0;

    *list = NULL;
    if (!kaddr[K_IFSTATS] || kword(kaddr[K_IFSTATS], &a) < 0)
        return -1;
    while (a && n < 32) {
        struct ifstats ifs;
        struct ast_if *p;
        char name[12];

        if (kread(a, &ifs, sizeof ifs) < 0)
            break;
        v = realloc(v, (n + 1) * sizeof *v);
        p = &v[n++];
        memset(p, 0, sizeof *p);
        if (kread((unsigned long)ifs.ifs_name, name, sizeof name) < 0)
            strcpy(name, "?");
        name[sizeof name - 1] = 0;
        sprintf(p->name, "%.10s%d", name, ifs.ifs_unit % 100);
        p->up = ifs.ifs_active != 0;
        p->ipackets = ifs.ifs_ipackets;
        p->opackets = ifs.ifs_opackets;
        p->ierrors = ifs.ifs_ierrors;
        p->oerrors = ifs.ifs_oerrors;
        p->collisions = ifs.ifs_collisions;
        a = (unsigned long)ifs.ifs_next;
    }
    *list = v;
    return n;
}

/* ---------------------------------------------------------------- core 1's network */

/* amix-gate usr/src/firmware/core1/cores/h264bench/core1_net.h: one megabyte
   of board memory, little-endian words (the ARM's), the addresses in network
   order; /dev/zz9000's offset 0 is 2 MB into the board */
#define C1NET_OFF       0x07900000UL
#define C1NET_MAGIC     0x43314E54UL
#define ZZ_FB_PHYS      0x00200000UL
enum { W_MAGIC, W_ON, W_RXFRAMES = 8, W_RXDROPS, W_TXFRAMES, W_TXERRS,
       W_UP = 32, W_IP, W_NETMASK, W_GW, W_RXBYTES, W_TXBYTES };

static volatile unsigned long *c1n;

static unsigned long le32(unsigned long v)
{
    return (v >> 24) | (v >> 8 & 0xff00UL) | (v << 8 & 0xff0000UL) | (v << 24);
}

int ast_c1net(struct ast_c1net *n)
{
    memset(n, 0, sizeof *n);
    if (!c1n) {
        int fd = open("/dev/zz9000", O_RDONLY);
        caddr_t p;

        if (fd < 0)
            return -1;
        p = mmap((caddr_t)0, 4096, PROT_READ, MAP_SHARED, fd, (off_t)(C1NET_OFF - ZZ_FB_PHYS));
        close(fd);                      /* the mapping stays */
        if (p == (caddr_t)-1)
            return -1;
        c1n = (volatile unsigned long *)p;
    }
    n->rings = le32(c1n[W_MAGIC]) == C1NET_MAGIC;
    if (!n->rings)
        return 0;
    n->up = le32(c1n[W_UP]) == 1 && le32(c1n[W_ON]) == 1;
    n->ip = c1n[W_IP];                  /* network order: a.b.c.d reads as 0xaabbccdd here */
    n->netmask = c1n[W_NETMASK];
    n->gw = c1n[W_GW];
    n->rx_bytes = le32(c1n[W_RXBYTES]);
    n->tx_bytes = le32(c1n[W_TXBYTES]);
    n->rx_frames = le32(c1n[W_RXFRAMES]);
    n->rx_drops = le32(c1n[W_RXDROPS]);
    n->tx_frames = le32(c1n[W_TXFRAMES]);
    n->tx_errs = le32(c1n[W_TXERRS]);
    return 0;
}

