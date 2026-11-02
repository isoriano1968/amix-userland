/* glob.h -- no globbing: AMIX has none, and what uses it does without */
#ifndef AMIX_GLOB_H
#define AMIX_GLOB_H
#include <stddef.h>
typedef struct { size_t gl_pathc; char **gl_pathv; size_t gl_offs; } glob_t;
#define GLOB_NOMATCH 1
#define GLOB_NOSPACE 2
#define GLOB_ABORTED 3
#define GLOB_ERR 1
#define GLOB_MARK 2
#define GLOB_NOSORT 4
#define GLOB_DOOFFS 8
#define GLOB_NOCHECK 16
#define GLOB_APPEND 32
#define GLOB_NOESCAPE 64
#define GLOB_PERIOD 128
#define GLOB_TILDE 256
#define GLOB_NOMAGIC 512
#define GLOB_BRACE 1024
#define GLOB_ERRFUNC_ARGS const char *, int
#define glob(p, f, e, g) (GLOB_NOMATCH)
#define globfree(g) ((void)0)
#endif
