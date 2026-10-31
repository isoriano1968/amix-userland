/* AMIX filesystem timestamps have second resolution. */
#include "amix-port.h"
#include <utime.h>
int utimes(const char *path, const struct timeval *times)
{
    struct utimbuf converted;
    if (!times)
        return utime(path, 0);
    converted.actime = times[0].tv_sec;
    converted.modtime = times[1].tv_sec;
    return utime(path, &converted);
}
