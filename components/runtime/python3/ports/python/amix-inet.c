/* IPv4 inet_pton/inet_ntop for AMIX, whose libnsl has only inet_addr and
 * inet_ntoa.  CPython's getaddrinfo.c and getnameinfo.c fallbacks call these;
 * the port is built --disable-ipv6, so AF_INET6 is refused. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 124
#endif
#ifndef ENOSPC
#define ENOSPC 28
#endif

/* strict dotted quad, four decimal parts 0-255, as POSIX inet_pton wants */
int inet_pton(int af, const char *src, void *dst)
{
    unsigned char out[4];
    int part = 0, digits = 0;
    unsigned value = 0;
    const char *p;

    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    for (p = src; ; p++) {
        if (*p >= '0' && *p <= '9') {
            if (digits && value == 0)       /* no leading zeros */
                return 0;
            value = value * 10 + (unsigned)(*p - '0');
            if (value > 255 || ++digits > 3)
                return 0;
        } else if (*p == '.' || *p == '\0') {
            if (!digits || part > 3)
                return 0;
            out[part++] = (unsigned char)value;
            value = 0;
            digits = 0;
            if (*p == '\0')
                break;
        } else {
            return 0;
        }
    }
    if (part != 4)
        return 0;
    memcpy(dst, out, 4);
    return 1;
}

const char *inet_ntop(int af, const void *src, char *dst, int size)
{
    const unsigned char *a = src;
    char buf[16];

    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    sprintf(buf, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    if ((int)strlen(buf) >= size) {
        errno = ENOSPC;
        return NULL;
    }
    strcpy(dst, buf);
    return dst;
}
