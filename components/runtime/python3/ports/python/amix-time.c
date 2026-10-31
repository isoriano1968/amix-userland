/* Copy libc's static result before any cooperative scheduling point. These
 * adapters are for GNU Pth's non-preemptive execution, not kernel threads. */
#include <time.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
struct tm *localtime_r(const time_t *value, struct tm *result)
{
    struct tm *temporary = localtime(value);
    if (!temporary) return NULL;
    *result = *temporary;
    return result;
}
struct tm *gmtime_r(const time_t *value, struct tm *result)
{
    struct tm *temporary = gmtime(value);
    if (!temporary) return NULL;
    *result = *temporary;
    return result;
}
int ttyname_r(int fd, char *buffer, size_t size)
{
    char *name = ttyname(fd);
    size_t length;
    if (!name) return errno ? errno : ENOTTY;
    length = strlen(name);
    if (length >= size) return ERANGE;
    memcpy(buffer, name, length + 1);
    return 0;
}
