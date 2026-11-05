/* Blocking connect for CPython's socket module on AMIX.
 *
 * AMIX's sockets (SVR4.0 sockmod on STREAMS) report a non-blocking connect
 * writable, with SO_ERROR 0, before the connection is up; the first send then
 * fails with ENOTCONN or vanishes.  GNU Pth's connect wrapper connects
 * non-blocking and waits for writability, so it has the same fault.  This
 * file is compiled without Pth's pthread.h: connect() is libsocket's.  The
 * descriptor is made blocking for the call and its flags restored after, as
 * curl and libupnp do on AMIX; the kernel's connect timeout applies, and the
 * other Pth threads wait meanwhile. */
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

extern int connect(int, const struct sockaddr *, int);

int amix_python_connect(int fd, const struct sockaddr *addr, int len)
{
    int flags = fcntl(fd, F_GETFL, 0);
    int rc, err;

    if (flags != -1 && (flags & (O_NONBLOCK | O_NDELAY)))
        fcntl(fd, F_SETFL, flags & ~(O_NONBLOCK | O_NDELAY));
    rc = connect(fd, addr, len);
    err = errno;
    if (flags != -1 && (flags & (O_NONBLOCK | O_NDELAY)))
        fcntl(fd, F_SETFL, flags);
    errno = err;
    return rc;
}
