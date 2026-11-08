/* core_portme.c -- CoreMark natively on AMIX: coremark 0 0 0x66 ITERATIONS */
#include <sys/time.h>
#include "coremark.h"

ee_u32 default_num_contexts = 1;
static double t0, t1;

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

void start_time(void) { t0 = now(); }
void stop_time(void) { t1 = now(); }
CORE_TICKS get_time(void) { return t1 - t0; }
secs_ret time_in_secs(CORE_TICKS ticks) { return ticks; }
void portable_init(core_portable *p, int *argc, char *argv[]) { (void)argc; (void)argv; p->portable_id = 1; }
void portable_fini(core_portable *p) { p->portable_id = 0; }
