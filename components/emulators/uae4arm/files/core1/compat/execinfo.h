/* execinfo.h for core 1: no backtraces */
#ifndef C1_EXECINFO_H
#define C1_EXECINFO_H
static inline int backtrace(void **b, int n) { (void)b; (void)n; return 0; }
static inline char **backtrace_symbols(void *const *b, int n) { (void)b; (void)n; return 0; }
#endif
