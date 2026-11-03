/*
 * amix-upnp.h -- forced into every libupnp compile: what SVR4 on AMIX lacks.
 * Its sys/socket.h has the constants and structures but declares no
 * function; there is no IPv6 and no multicast (libupnp is built without IPv6
 * and sends its searches as broadcasts, patches/0001), but the sources keep
 * addresses in sockaddr_storage and sockaddr_in6 even so.
 */
#ifndef AMIX_UPNP_H
#define AMIX_UPNP_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

typedef int socklen_t;			/* SVR4's socket calls take an int */
typedef unsigned short sa_family_t;	/* struct sockaddr's sa_family */
struct msghdr;
struct timeval;
int socket(int, int, int);
int bind(int, const struct sockaddr *, int);
int listen(int, int);
int getsockopt(int, int, int, void *, int *);
int setsockopt(int, int, int, const void *, int);
int getsockname(int, struct sockaddr *, int *);
int getpeername(int, struct sockaddr *, int *);
int shutdown(int, int);
/* the calls that block: Pth's pthread.h (forced in first) maps them to its
   yielding versions and declares those; declared here only without it */
#ifndef connect
int connect(int, const struct sockaddr *, int);
int accept(int, struct sockaddr *, int *);
int recv(int, void *, int, int);
int recvfrom(int, void *, int, int, struct sockaddr *, int *);
int send(int, const void *, int, int);
int sendto(int, const void *, int, int, const struct sockaddr *, int);
int select(int, void *, void *, void *, struct timeval *);
#endif
/* connect: the socket library's own, blocking (amix-upnp.c), not Pth's,
   which makes the socket non-blocking and on SVR4.0's STREAMS sockets takes
   the connect for failed, or lets the first send vanish */
#undef connect
#define connect amix_upnp_connect
int amix_upnp_connect(int, const struct sockaddr *, int);
int gethostname(char *, int);

/* address storage and the IPv6 types, which nothing on AMIX fills */
struct sockaddr_storage {
	sa_family_t ss_family;
	char __ss_pad[126];
	long __ss_align;
};
struct in6_addr {
	union { unsigned char __u6_addr8[16]; unsigned long __u6_addr32[4]; } __u6_addr;
};
#define s6_addr __u6_addr.__u6_addr8
struct sockaddr_in6 {
	sa_family_t sin6_family;
	unsigned short sin6_port;
	unsigned long sin6_flowinfo;
	struct in6_addr sin6_addr;
	unsigned long sin6_scope_id;
};
#ifndef AF_INET6
#define AF_INET6 26
#endif
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IN6_IS_ADDR_LINKLOCAL(a) ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0x80)
#define IN6_IS_ADDR_SITELOCAL(a) ((a)->s6_addr[0] == 0xfe && ((a)->s6_addr[1] & 0xc0) == 0xc0)
#define IN6_IS_ADDR_ULA(a) (((a)->s6_addr[0] & 0xfe) == 0xfc)
#define IN6_IS_ADDR_UNSPECIFIED(a) ((a)->__u6_addr.__u6_addr32[0] == 0 && (a)->__u6_addr.__u6_addr32[1] == 0 && \
	(a)->__u6_addr.__u6_addr32[2] == 0 && (a)->__u6_addr.__u6_addr32[3] == 0)
#define IN6ADDR_ANY_INIT { { { 0 } } }

/* getaddrinfo, IPv4 only, on gethostbyname */
struct addrinfo {
	int ai_flags, ai_family, ai_socktype, ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};
#define AI_PASSIVE	1
#define AI_CANONNAME	2
#define AI_NUMERICHOST	4
#define AI_NUMERICSERV	8
#define EAI_FAMILY	-6
#define EAI_MEMORY	-10
#define EAI_NONAME	-2
#define EAI_SERVICE	-8
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);

/* in amix-upnp.c */
int inet_pton(int, const char *, void *);
const char *inet_ntop(int, const void *, char *, socklen_t);
int strerror_r(int, char *, size_t);
struct tm;
struct tm *gmtime_r(const time_t *, struct tm *);
long long strtoll(const char *, char **, int);
unsigned int if_nametoindex(const char *);
#endif
