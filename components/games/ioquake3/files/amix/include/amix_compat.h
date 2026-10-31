/*
 * amix_compat.h -- what ioquake3 needs from a C99/POSIX system that AT&T
 * SVR4.0 (AMIX 2.1) does not have; included before every file
 * (build.py -include).  Implementations in amix_compat.c.
 *
 * Networking is IPv4 only: AMIX has no IPv6 and no getaddrinfo, so the
 * modern socket API ioquake3 is written against is provided on top of
 * gethostbyname, and AF_INET6 sockets simply fail to open (ioquake3 then
 * runs on IPv4, as with net_enabled 1).
 */
#ifndef AMIX_COMPAT_H
#define AMIX_COMPAT_H

#include <sys/types.h>
#include <stdarg.h>
#include <stddef.h>

#ifndef __cplusplus

/* --- stdio, strings, stdlib ---------------------------------------------- */
int snprintf(char *s, size_t n, const char *fmt, ...);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *strtok_r(char *s, const char *delim, char **save);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
long long atoll(const char *s);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
/* realpath and usleep: declared by AMIX's headers, missing from its libc */
int mkstemp(char *tmpl);
char *basename(char *path);
char *dirname(char *path);
unsigned int if_nametoindex(const char *name);

/* --- time ------------------------------------------------------------------ */
#include <sys/time.h>
#include <time.h>
struct timespec {                       /* SVR4.0 has only timestruc_t */
    time_t tv_sec;
    long tv_nsec;
};
int nanosleep(const struct timespec *req, struct timespec *rem);

/* --- float maths: AMIX's libm has the C99 float functions (sqrtf, powf,
   ...); these few it lacks -------------------------------------------- */
long lrintf(float x);           /* real functions: their address is taken */
long lrint(double x);
double round(double x);
float roundf(float x);
double trunc(double x);
float truncf(float x);
#define log2f(x)        ((float) (log(x) * 1.4426950408889634))
#define log2(x)         (log(x) * 1.4426950408889634)
#define exp2f(x)        ((float) pow(2.0, (x)))

/* --- sockets: IPv4 under the modern API --------------------------------- */
typedef int socklen_t;
typedef unsigned short sa_family_t;

#ifndef AF_INET6
#define AF_INET6        26
#endif
#ifndef PF_INET6
#define PF_INET6        AF_INET6
#endif
#define IPPROTO_IPV6    41
#define IPV6_JOIN_GROUP 20
#define IPV6_LEAVE_GROUP 21
#define IPV6_MULTICAST_IF 17
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in6 {
    unsigned short sin6_family;
    unsigned short sin6_port;
    unsigned long sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned long sin6_scope_id;
};
struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int ipv6mr_interface;
};
struct sockaddr_storage {
    unsigned short ss_family;
    char ss_pad[126];
};
extern const struct in6_addr in6addr_any;
#define IN6_IS_ADDR_UNSPECIFIED(a) (amix_in6_zero(a))
#define IN6_IS_ADDR_MULTICAST(a)   ((a)->s6_addr[0] == 0xff)
static __inline__ int amix_in6_zero(const struct in6_addr *a)
{
    int i;
    for (i = 0; i < 16; i++)
        if (a->s6_addr[i])
            return 0;
    return 1;
}

struct sockaddr;
struct addrinfo {
    int ai_flags, ai_family, ai_socktype, ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};
#define AI_PASSIVE      1
#define AI_CANONNAME    2
#define AI_NUMERICHOST  4
#define NI_NUMERICHOST  1
#define NI_NUMERICSERV  2
#define NI_MAXHOST      1025
#define EAI_NONAME      8
#define EAI_FAMILY      5
#define EAI_MEMORY      6
#define EAI_FAIL        4
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **res);
void freeaddrinfo(struct addrinfo *ai);
const char *gai_strerror(int err);
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

#endif /* !__cplusplus */
#endif
