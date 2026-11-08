/*
 * amix_compat.c -- the C99/POSIX pieces AMIX (SVR4.0) lacks, for ioquake3
 * (see include/amix_compat.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

extern int poll();

/* --- vsnprintf --------------------------------------------------------------
 * A complete one: flags - + space # 0, width and precision (also *), the
 * length modifiers hh h l ll z j t L, and d i u o x X c s p n % e E f F g G a
 * (the floating ones format one number at a time with sprintf, into a
 * buffer big enough for any double at the precision allowed here).
 */
struct out { char *s; size_t n, len; };

static void put(struct out *o, char c)
{
    if (o->len + 1 < o->n)
        o->s[o->len] = c;
    o->len++;
}

static void puts_n(struct out *o, const char *s, size_t k)
{
    while (k--)
        put(o, *s++);
}

static void pad(struct out *o, char c, int k)
{
    while (k-- > 0)
        put(o, c);
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    struct out o;
    o.s = s; o.n = n; o.len = 0;

    for (; *fmt; fmt++) {
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0, width = 0, prec = -1;
        int lng = 0;                    /* 0 int, 1 long, 2 long long, -1 short, -2 char */
        char c, buf[512], *p;
        unsigned long long u;
        long long v;
        int neg, len, base, upper;

        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        for (;;) {
            c = *++fmt;
            if (c == '-') left = 1;
            else if (c == '+') plus = 1;
            else if (c == ' ') space = 1;
            else if (c == '#') alt = 1;
            else if (c == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt++;
        } else
            while (isdigit((unsigned char) *fmt))
                width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else
                while (isdigit((unsigned char) *fmt))
                    prec = prec * 10 + (*fmt++ - '0');
        }
        for (;;) {
            if (*fmt == 'h') { lng = lng == -1 ? -2 : -1; fmt++; }
            else if (*fmt == 'l') { lng = lng == 1 ? 2 : 1; fmt++; }
            else if (*fmt == 'z' || *fmt == 't' || *fmt == 'j') { lng = *fmt == 'j' ? 2 : 1; fmt++; }
            else if (*fmt == 'L' || *fmt == 'q') { lng = 2; fmt++; }
            else break;
        }
        c = *fmt;
        if (!c)
            break;
        switch (c) {
        case 'd': case 'i':
            v = lng == 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            if (lng == -1) v = (short) v;
            if (lng == -2) v = (signed char) v;
            neg = v < 0;
            u = neg ? 0ULL - (unsigned long long) v : (unsigned long long) v;
            base = 10; upper = 0;
            goto number;
        case 'u': case 'o': case 'x': case 'X':
            u = lng == 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long)
                : va_arg(ap, unsigned int);
            if (lng == -1) u = (unsigned short) u;
            if (lng == -2) u = (unsigned char) u;
            neg = 0; plus = space = 0;
            base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
            upper = c == 'X';
        number:
            p = buf + sizeof buf;
            len = 0;
            while (u) {
                int d = (int) (u % base);
                *--p = (char) (d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
                u /= base;
                len++;
            }
            if (prec < 0)
                prec = 1;
            else
                zero = 0;
            while (len < prec) { *--p = '0'; len++; }
            if (alt && base == 8 && *p != '0') { *--p = '0'; len++; }
            {
                const char *pre = neg ? "-" : plus ? "+" : space ? " " : "";
                int plen = (int) strlen(pre);
                if (alt && base == 16 && len && !(len == 1 && *p == '0'))
                    pre = upper ? "0X" : "0x", plen = 2;
                if (!left && !zero) pad(&o, ' ', width - len - plen);
                puts_n(&o, pre, plen);
                if (!left && zero) pad(&o, '0', width - len - plen);
                puts_n(&o, p, len);
                if (left) pad(&o, ' ', width - len - plen);
            }
            break;
        case 'p':
            u = (unsigned long) va_arg(ap, void *);
            neg = 0; plus = space = 0; base = 16; upper = 0; alt = 1;
            goto number;
        case 'c':
            buf[0] = (char) va_arg(ap, int);
            if (!left) pad(&o, ' ', width - 1);
            put(&o, buf[0]);
            if (left) pad(&o, ' ', width - 1);
            break;
        case 's':
            p = va_arg(ap, char *);
            if (!p) p = "(null)";
            for (len = 0; p[len] && (prec < 0 || len < prec); len++)
                ;
            if (!left) pad(&o, ' ', width - len);
            puts_n(&o, p, len);
            if (left) pad(&o, ' ', width - len);
            break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            char f[32], *q = f;
            double d = lng == 2 ? (double) va_arg(ap, long double) : va_arg(ap, double);
            if (prec > 100) prec = 100;
            if (d > 1e300 || d < -1e300) {          /* keep %f of huge values in buf */
                if (c == 'f' || c == 'F') c = 'e';
            }
            *q++ = '%';
            if (left) *q++ = '-';
            if (plus) *q++ = '+';
            if (space) *q++ = ' ';
            if (alt) *q++ = '#';
            if (zero) *q++ = '0';
            if (prec >= 0) q += sprintf(q, ".%d", prec);
            *q++ = c == 'F' ? 'f' : c == 'a' ? 'e' : c == 'A' ? 'E' : c;
            *q = 0;
            len = sprintf(buf, f, d);
            if (!left) pad(&o, ' ', width - len);
            puts_n(&o, buf, len);
            if (left) pad(&o, ' ', width - len);
            break;
        }
        case 'n':
            *va_arg(ap, int *) = (int) o.len;
            break;
        case '%':
            put(&o, '%');
            break;
        default:
            put(&o, '%');
            put(&o, c);
            break;
        }
    }
    if (o.n)
        o.s[o.len < o.n ? o.len : o.n - 1] = 0;
    return (int) o.len;
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

/* --- strings, stdlib -------------------------------------------------------- */

int strcasecmp(const char *a, const char *b)
{
    int d;
    for (;; a++, b++) {
        d = tolower((unsigned char) *a) - tolower((unsigned char) *b);
        if (d || !*a)
            return d;
    }
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    int d;
    for (; n; n--, a++, b++) {
        d = tolower((unsigned char) *a) - tolower((unsigned char) *b);
        if (d || !*a)
            return d;
    }
    return 0;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    char *t;
    if (!s)
        s = *save;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return NULL;
    }
    t = s + strcspn(s, delim);
    if (*t)
        *t++ = 0;
    *save = t;
    return s;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    unsigned long long v = 0;
    const char *p = s;
    int neg = 0, any = 0, d;

    while (isspace((unsigned char) *p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0)
        base = *p == '0' ? 8 : 10;
    for (;; p++) {
        if (isdigit((unsigned char) *p)) d = *p - '0';
        else if (isalpha((unsigned char) *p)) d = tolower((unsigned char) *p) - 'a' + 10;
        else break;
        if (d >= base)
            break;
        v = v * base + d;
        any = 1;
    }
    if (end)
        *end = (char *) (any ? p : s);
    return neg ? 0ULL - v : v;
}

long long strtoll(const char *s, char **end, int base)
{
    return (long long) strtoull(s, end, base);
}

long long atoll(const char *s)
{
    return strtoll(s, NULL, 10);
}

int setenv(const char *name, const char *value, int overwrite)
{
    char *e;
    if (!overwrite && getenv(name))
        return 0;
    e = (char *) malloc(strlen(name) + strlen(value) + 2);
    if (!e)
        return -1;
    sprintf(e, "%s=%s", name, value);
    return putenv(e);                   /* the string stays: putenv keeps it */
}

int unsetenv(const char *name)
{
    return setenv(name, "", 1);
}

/* absolute, with . and .. folded; symbolic links are not followed */
char *realpath(char *path, char *resolved)
{
    char tmp[PATH_MAX * 2], *out = resolved ? resolved : (char *) malloc(PATH_MAX);
    char *seg, *save;
    size_t k;

    if (!out)
        return NULL;
    if (path[0] == '/')
        strcpy(tmp, path);
    else {
        if (!getcwd(tmp, PATH_MAX))
            return NULL;
        strcat(tmp, "/");
        strcat(tmp, path);
    }
    out[0] = 0;
    for (seg = strtok_r(tmp, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
        if (!strcmp(seg, ".") || !*seg)
            continue;
        if (!strcmp(seg, "..")) {
            char *sl = strrchr(out, '/');
            if (sl)
                *sl = 0;
            continue;
        }
        k = strlen(out);
        if (k + strlen(seg) + 2 > PATH_MAX)
            return NULL;
        out[k] = '/';
        strcpy(out + k + 1, seg);
    }
    if (!out[0])
        strcpy(out, "/");
    {
        struct stat st;
        if (stat(out, &st) < 0)
            return NULL;
    }
    return out;
}

int usleep(unsigned long usec)
{
    struct pollfd none;
    return poll(&none, 0, (int) ((usec + 999) / 1000));
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    if (rem)
        rem->tv_sec = rem->tv_nsec = 0;
    return usleep((unsigned int) (req->tv_sec * 1000000 + req->tv_nsec / 1000));
}

/* --- files -------------------------------------------------------------------- */

int mkstemp(char *tmpl)
{
    size_t n = strlen(tmpl);
    int i, fd;
    static unsigned long seq;

    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX"))
        return -1;
    for (i = 0; i < 100; i++) {
        sprintf(tmpl + n - 6, "%06lu", (unsigned long) ((getpid() * 1000UL + seq++) % 1000000UL));
        fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    return -1;
}

char *basename(char *path)
{
    char *s;
    size_t n;

    if (!path || !*path)
        return ".";
    n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    s = strrchr(path, '/');
    return s && s[1] ? s + 1 : path;
}

char *dirname(char *path)
{
    char *s;
    size_t n;

    if (!path || !*path)
        return ".";
    n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    s = strrchr(path, '/');
    if (!s)
        return ".";
    if (s == path)
        return "/";
    *s = 0;
    return path;
}

unsigned int if_nametoindex(const char *name)
{
    (void) name;
    return 0;                           /* no IPv6 scopes on AMIX */
}

/* --- C99 rounding (round half away from zero; lrint* to nearest even) - */

/* In the 060's FPU, not through long long: those went to libgcc's software
   64-bit conversions (__fixdfdi, __floatdidf, __fixunsdfdi), 13% of a RtCW
   frame.  fintrz truncates in hardware on the 060 (only the 040 traps on
   it); fmove.l rounds by the FPCR, to nearest even by default, which is what
   lrint/lrintf mean.  Outside the long range fmove.l saturates. */
static __inline__ double amix_fintrz(double x)
{
    double r;

    __asm__ ("fintrz.x %1,%0" : "=f" (r) : "f" (x));
    return r;
}

double trunc(double x) { return amix_fintrz(x); }
float truncf(float x) { return (float) amix_fintrz(x); }
double round(double x)
{
    double t = amix_fintrz(x), d = x - t;

    if (d >= 0.5)
        t += 1.0;
    else if (d <= -0.5)
        t -= 1.0;
    return t;
}
float roundf(float x) { return (float) round(x); }
long lrint(double x)
{
    long r;

    __asm__ ("fmove.l %1,%0" : "=d" (r) : "f" (x));
    return r;
}
long lrintf(float x)
{
    long r;

    __asm__ ("fmove.l %1,%0" : "=d" (r) : "f" ((double) x));
    return r;
}

/* --- sockets ---------------------------------------------------------------- */

const struct in6_addr in6addr_any;

const char *gai_strerror(int err)
{
    switch (err) {
    case EAI_NONAME: return "name unknown";
    case EAI_FAMILY: return "address family not supported (IPv4 only)";
    case EAI_MEMORY: return "out of memory";
    }
    return "address lookup failed";
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    unsigned long a = INADDR_ANY;

    *res = NULL;
    if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    if (node) {
        a = inet_addr(node);
        if (a == (unsigned long) -1) {
            struct hostent *h;
            if (hints && (hints->ai_flags & AI_NUMERICHOST))
                return EAI_NONAME;
            h = gethostbyname(node);
            if (!h || h->h_addrtype != AF_INET || !h->h_addr_list[0])
                return EAI_NONAME;
            memcpy(&a, h->h_addr_list[0], 4);
        }
    }
    ai = (struct addrinfo *) calloc(1, sizeof *ai + sizeof *sin);
    if (!ai)
        return EAI_MEMORY;
    sin = (struct sockaddr_in *) (ai + 1);
    sin->sin_family = AF_INET;
    sin->sin_port = service ? htons((unsigned short) atoi(service)) : 0;
    memcpy(&sin->sin_addr, &a, 4);
    ai->ai_family = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = sizeof *sin;
    ai->ai_addr = (struct sockaddr *) sin;
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *ai)
{
    while (ai) {
        struct addrinfo *n = ai->ai_next;
        free(ai);
        ai = n;
    }
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    const unsigned char *b = (const unsigned char *) src;
    char t[16];
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    sprintf(t, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    if ((socklen_t) strlen(t) >= size) {
        errno = ENOSPC;
        return NULL;
    }
    strcpy(dst, t);
    return dst;
}

int inet_pton(int af, const char *src, void *dst)
{
    unsigned long a;
    if (af != AF_INET)
        return -1;
    a = inet_addr(src);
    if (a == (unsigned long) -1 && strcmp(src, "255.255.255.255"))
        return 0;
    memcpy(dst, &a, 4);
    return 1;
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *) sa;
    (void) salen; (void) flags;
    if (sa->sa_family != AF_INET)
        return EAI_FAMILY;
    if (host && !inet_ntop(AF_INET, &sin->sin_addr, host, hostlen))
        return EAI_FAIL;
    if (serv && servlen)
        snprintf(serv, servlen, "%u", ntohs(sin->sin_port));
    return 0;
}
