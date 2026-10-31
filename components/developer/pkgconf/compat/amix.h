/* amix.h: taken in by every file; AMIX's sys/stat.h has S_IFLNK, not this */
#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_ISLNK
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#endif
