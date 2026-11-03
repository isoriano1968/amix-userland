/* sched.h for libupnp on AMIX: no scheduling policies (Pth threads run
   cooperatively), so the thread pool's priority calls are no-ops. */
#ifndef AMIX_SCHED_H
#define AMIX_SCHED_H
struct sched_param { int sched_priority; };
#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2
static inline int sched_get_priority_min(int p) { (void)p; return 0; }
static inline int sched_get_priority_max(int p) { (void)p; return 0; }
#endif
