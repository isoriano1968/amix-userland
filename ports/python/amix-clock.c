/* Elapsed kernel ticks, not adjustable calendar time. Physical AMIX measured
 * _SC_CLK_TCK=60. Extend the unsigned 32-bit tick counter across wraps. */
#include <sys/types.h>
#include <sys/times.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

long amix_python_clock_hz(void)
{
    static long hz;
    if (!hz) hz = sysconf(_SC_CLK_TCK);
    return hz;
}
int amix_python_monotonic(long long *ns)
{
    static int started;
    static uint32_t previous;
    static uint64_t elapsed;
    struct tms accounting;
    clock_t raw;
    uint32_t current;
    long hz = amix_python_clock_hz();
    if (hz <= 0) { errno = EINVAL; return -1; }
    errno = 0;
    raw = times(&accounting);
    if (raw == (clock_t)-1 && errno) return -1;
    current = (uint32_t)raw;
    if (!started) { elapsed = current; started = 1; }
    else elapsed += (uint32_t)(current - previous);
    previous = current;
    *ns = (long long)((elapsed / hz) * 1000000000ULL +
                     (elapsed % hz) * 1000000000ULL / hz);
    return 0;
}
