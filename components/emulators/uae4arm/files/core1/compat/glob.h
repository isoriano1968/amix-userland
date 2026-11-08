/* glob.h for core 1: no directories, nothing matches */
#ifndef C1_GLOB_H
#define C1_GLOB_H
#include <stddef.h>
typedef struct { size_t gl_pathc; char **gl_pathv; size_t gl_offs; } glob_t;
#define GLOB_NOMATCH 3
#define GLOB_ERR 1
#define GLOB_MARK 2
#define GLOB_NOSORT 4
static inline int glob(const char *p, int f, int (*e)(const char *, int), glob_t *g) { (void)p; (void)f; (void)e; g->gl_pathc = 0; g->gl_pathv = 0; return GLOB_NOMATCH; }
static inline void globfree(glob_t *g) { (void)g; }
#endif
