/* strings.h for Grafx2 on AMIX: SVR4 keeps the BSD case-blind compares in string.h's world */
#ifndef AMIX_STRINGS_H
#define AMIX_STRINGS_H
#include <string.h>
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);
#endif
