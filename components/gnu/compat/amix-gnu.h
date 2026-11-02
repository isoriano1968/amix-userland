/* amix-gnu.h -- forced into every file of a GNU tool */
#ifndef AMIX_GNU_H
#define AMIX_GNU_H
int isblank(int);
#define CODESET 0
char *nl_langinfo(int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
/* AMIX's printf has no %j, %z or %t: amix-printf.c */
#define printf gnu_printf
#define fprintf gnu_fprintf
#define sprintf gnu_sprintf
#define snprintf gnu_snprintf
#define vprintf gnu_vprintf
#define vfprintf gnu_vfprintf
#define vsprintf gnu_vsprintf
#define vsnprintf gnu_vsnprintf
#ifdef AMIX_RUSAGE
#include <sys/time.h>
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN 1
struct rusage { struct timeval ru_utime, ru_stime; };
int getrusage(int, struct rusage *);
#endif
#endif
