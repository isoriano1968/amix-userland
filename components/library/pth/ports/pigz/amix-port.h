/* Private pigz portability additions; never install as a system header. */
#ifndef AMIX_PIGZ_PORT_H
#define AMIX_PIGZ_PORT_H
#include <limits.h>
#include <sys/types.h>
#include <sys/time.h>
#ifndef SSIZE_MAX
typedef char amix_pigz_ssize_is_int[
    sizeof(ssize_t) == sizeof(int) ? 1 : -1];
#define SSIZE_MAX INT_MAX
#endif
int utimes(const char *, const struct timeval *);
#endif
