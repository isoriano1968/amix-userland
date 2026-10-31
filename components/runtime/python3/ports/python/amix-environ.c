/* Private environment adapters. No cooperative scheduling point occurs while
 * modifying environ. Only strings allocated here are reclaimed here. */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
extern char **environ;
struct owned_environment { char *text; struct owned_environment *next; };
static struct owned_environment *owned;

static void collect_environment(void)
{
    struct owned_environment **entry = &owned;
    while (*entry) {
        struct owned_environment *item = *entry;
        char **current;
        for (current = environ; current && *current; current++)
            if (*current == item->text) break;
        if (current && *current) entry = &item->next;
        else { *entry = item->next; free(item->text); free(item); }
    }
}
static int valid_name(const char *name)
{
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return 0; }
    return 1;
}
int setenv(const char *name, const char *value, int overwrite)
{
    size_t n, v;
    struct owned_environment *item;
    if (!valid_name(name)) return -1;
    if (!value) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    n = strlen(name); v = strlen(value);
    if (n > (size_t)-1 - 2 || v > (size_t)-1 - n - 2) {
        errno = ENOMEM; return -1;
    }
    item = malloc(sizeof(*item));
    if (!item) return -1;
    item->text = malloc(n + v + 2);
    if (!item->text) { free(item); return -1; }
    memcpy(item->text, name, n); item->text[n] = '=';
    memcpy(item->text + n + 1, value, v + 1);
    if (putenv(item->text)) { free(item->text); free(item); return -1; }
    item->next = owned; owned = item;
    collect_environment();
    return 0;
}
int unsetenv(const char *name)
{
    char **read, **write;
    size_t n;
    if (!valid_name(name)) return -1;
    n = strlen(name);
    if (environ) {
        write = environ;
        for (read = environ; *read; read++)
            if (strncmp(*read, name, n) || (*read)[n] != '=') *write++ = *read;
        *write = NULL;
    }
    collect_environment();
    return 0;
}
