/*
 * amix-upnp.c -- what libupnp needs and AMIX's libc lacks: getifaddrs on
 * SVR4's interface ioctls, if_nametoindex, IPv4 inet_pton/inet_ntop and
 * getaddrinfo, strerror_r, gmtime_r and strtoll; and a blocking connect.
 * Linked into libupnp.so.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sockio.h>
#include <stropts.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include "ifaddrs.h"

extern int ioctl(int, int, ...);

#undef connect				/* the socket library's, not Pth's */
extern int connect(int, const struct sockaddr *, int);

/* a blocking connect, whatever the socket's mode: on SVR4.0 the socket
   library builds connect from STREAMS messages, and the non-blocking form
   (Pth's wrapper, libupnp's own) loses the connection or the first send.
   On the LAN it takes milliseconds; the other threads wait meanwhile. */
int amix_upnp_connect(int fd, const struct sockaddr *addr, int len)
{
	int fl = fcntl(fd, F_GETFL, 0), rc, e;

	if (fl != -1 && (fl & O_NONBLOCK))
		fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
	rc = connect(fd, addr, len);
	e = errno;
	if (fl != -1 && (fl & O_NONBLOCK))
		fcntl(fd, F_SETFL, fl);
	errno = e;
	return rc;
}

/* SVR4.0 answers the interface ioctls as STREAMS I_STR requests to IP: a
   plain ioctl(SIOCGIFCONF) on AMIX succeeds and fills nothing */
static int sioc(int fd, int cmd, void *arg, int len)
{
	struct strioctl s;

	s.ic_cmd = cmd;
	s.ic_timout = 0;
	s.ic_len = len;
	s.ic_dp = arg;
	return ioctl(fd, I_STR, &s) < 0 ? -1 : s.ic_len;
}

struct ifa_node {			/* one allocation per interface */
	struct ifaddrs ifa;
	struct sockaddr_in addr, mask, brd;
	char name[IFNAMSIZ + 1];
};

int getifaddrs(struct ifaddrs **list)
{
	struct ifconf ifc;
	struct ifreq req[32], r;
	struct ifaddrs *head = NULL, **tail = &head;
	int s, i, n;

	*list = NULL;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return -1;
	(void)ifc;
	if ((n = sioc(s, SIOCGIFCONF, req, sizeof req)) < 0) {
		close(s);
		return -1;
	}
	n /= sizeof(struct ifreq);
	for (i = 0; i < n; i++) {
		struct ifa_node *e;

		if (req[i].ifr_addr.sa_family != AF_INET)
			continue;
		if ((e = calloc(1, sizeof *e)) == NULL)
			break;
		strncpy(e->name, req[i].ifr_name, IFNAMSIZ);
		memcpy(&e->addr, &req[i].ifr_addr, sizeof e->addr);
		r = req[i];
		if (sioc(s, SIOCGIFFLAGS, &r, sizeof r) >= 0)
			e->ifa.ifa_flags = (unsigned short)r.ifr_flags;
		if (e->ifa.ifa_flags & IFF_BROADCAST)
			e->ifa.ifa_flags |= IFF_MULTICAST;
		r = req[i];
		if (sioc(s, SIOCGIFNETMASK, &r, sizeof r) >= 0)
			memcpy(&e->mask, &r.ifr_addr, sizeof e->mask);
		e->mask.sin_family = AF_INET;
		r = req[i];
		if (sioc(s, SIOCGIFBRDADDR, &r, sizeof r) >= 0)
			memcpy(&e->brd, &r.ifr_broadaddr, sizeof e->brd);
		e->ifa.ifa_name = e->name;
		e->ifa.ifa_addr = (struct sockaddr *)&e->addr;
		e->ifa.ifa_netmask = (struct sockaddr *)&e->mask;
		e->ifa.ifa_broadaddr = (struct sockaddr *)&e->brd;
		*tail = &e->ifa;
		tail = &e->ifa.ifa_next;
	}
	close(s);
	*list = head;
	return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
	while (ifa) {
		struct ifaddrs *next = ifa->ifa_next;
		free(ifa);			/* the ifa_node it starts */
		ifa = next;
	}
}

/* an index for each interface: its place in SIOCGIFCONF's list, from 1 */
unsigned int if_nametoindex(const char *name)
{
	struct ifaddrs *list, *p;
	unsigned int i = 0, found = 0;

	if (getifaddrs(&list) < 0)
		return 0;
	for (p = list; p; p = p->ifa_next) {
		i++;
		if (strcmp(p->ifa_name, name) == 0) {
			found = i;
			break;
		}
	}
	freeifaddrs(list);
	return found;
}

int inet_pton(int af, const char *src, void *dst)
{
	unsigned int a, b, c, d;
	char end;

	if (af != AF_INET) {
		errno = EAFNOSUPPORT;
		return -1;
	}
	if (sscanf(src, "%u.%u.%u.%u%c", &a, &b, &c, &d, &end) != 4 ||
	    a > 255 || b > 255 || c > 255 || d > 255)
		return 0;
	((unsigned char *)dst)[0] = a;
	((unsigned char *)dst)[1] = b;
	((unsigned char *)dst)[2] = c;
	((unsigned char *)dst)[3] = d;
	return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	const unsigned char *p = src;
	char buf[INET_ADDRSTRLEN];

	if (af != AF_INET) {
		errno = EAFNOSUPPORT;
		return NULL;
	}
	sprintf(buf, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
	if ((socklen_t)strlen(buf) >= size) {
		errno = ENOSPC;
		return NULL;
	}
	strcpy(dst, buf);
	return dst;
}

int strerror_r(int e, char *buf, size_t len)
{
	const char *s = strerror(e);

	if (strlen(s) >= len)
		return ERANGE;
	strcpy(buf, s);
	return 0;
}

/* IPv4 only: a numeric address, or a name through gethostbyname */
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	struct {
		struct addrinfo ai;
		struct sockaddr_in sin;
	} *r;
	struct in_addr in;

	*res = NULL;
	if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
		return EAI_FAMILY;
	if ((r = calloc(1, sizeof *r)) == NULL)
		return EAI_MEMORY;
	if (node == NULL)
		in.s_addr = htonl((hints && (hints->ai_flags & AI_PASSIVE)) ? INADDR_ANY : INADDR_LOOPBACK);
	else if (inet_pton(AF_INET, node, &in) != 1) {
		struct hostent *h;

		if ((hints && (hints->ai_flags & AI_NUMERICHOST)) ||
		    (h = gethostbyname(node)) == NULL || h->h_addrtype != AF_INET) {
			free(r);
			return EAI_NONAME;
		}
		memcpy(&in, h->h_addr_list[0], sizeof in);
	}
	r->sin.sin_family = AF_INET;
	r->sin.sin_addr = in;
	if (service) {
		char *end;
		long port = strtol(service, &end, 10);

		if (*end || port < 0 || port > 65535) {
			free(r);
			return EAI_SERVICE;
		}
		r->sin.sin_port = htons((unsigned short)port);
	}
	r->ai.ai_family = AF_INET;
	r->ai.ai_socktype = hints ? hints->ai_socktype : 0;
	r->ai.ai_protocol = hints ? hints->ai_protocol : 0;
	r->ai.ai_addrlen = sizeof r->sin;
	r->ai.ai_addr = (struct sockaddr *)&r->sin;
	*res = &r->ai;
	return 0;
}

void freeaddrinfo(struct addrinfo *ai)
{
	while (ai) {
		struct addrinfo *next = ai->ai_next;
		free(ai);			/* the sockaddr is in the same block */
		ai = next;
	}
}

const char *gai_strerror(int e)
{
	switch (e) {
	case EAI_FAMILY:	return "address family not supported";
	case EAI_MEMORY:	return "out of memory";
	case EAI_NONAME:	return "name not known";
	case EAI_SERVICE:	return "service not known";
	}
	return "unknown getaddrinfo error";
}

/* gmtime's result copied: AMIX's libc has no reentrant variant */
struct tm *gmtime_r(const time_t *t, struct tm *out)
{
	struct tm *g = gmtime(t);

	if (g == NULL)
		return NULL;
	*out = *g;
	return out;
}

/* 64-bit strtoll (AMIX's libc has strtol only) */
long long strtoll(const char *s, char **end, int base)
{
	const char *p = s;
	unsigned long long v = 0, lim;
	int neg = 0, any = 0, d;

	while (isspace((unsigned char)*p))
		p++;
	if (*p == '-' || *p == '+')
		neg = *p++ == '-';
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit((unsigned char)p[2])) {
		p += 2;
		base = 16;
	} else if (base == 0)
		base = *p == '0' ? 8 : 10;
	lim = neg ? (unsigned long long)9223372036854775807LL + 1 : 9223372036854775807ULL;
	for (;; p++) {
		if (isdigit((unsigned char)*p))
			d = *p - '0';
		else if (isalpha((unsigned char)*p))
			d = tolower((unsigned char)*p) - 'a' + 10;
		else
			break;
		if (d >= base)
			break;
		any = 1;
		if (v > (lim - d) / base) {
			v = lim;
			errno = ERANGE;
			while (isalnum((unsigned char)*++p))
				;
			break;
		}
		v = v * base + d;
	}
	if (end)
		*end = (char *)(any ? p : s);
	return neg ? (long long)(0 - v) : (long long)v;
}
