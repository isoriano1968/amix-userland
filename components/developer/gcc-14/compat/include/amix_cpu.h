/* amix_cpu.h -- the 680x0 a program runs on (libamixc99, compat/amix_cpu.c).
 *
 * Packages are built for the 68020 ISA tuned for the 68060 and run on every
 * AMIX machine.  A path that needs a 68040/68060 instruction (move16,
 * fdsqrt ...) asks amix_cpu() at run time and keeps a baseline path for the
 * 68020/030.  At compile time, __amix_cpu_baseline__ (defined by gcc 14's
 * specs unless -m68040/-m68060 is given) says that the code may run on a
 * 68020/030; gcc's __mc680x0__ macros follow -mtune and cannot say it.
 *
 * C89, for AMIX's gcc 2.7 as well.
 */
#ifndef AMIX_CPU_H
#define AMIX_CPU_H

#ifdef __cplusplus
extern "C" {
#endif

/* what amix_cpu() answers where the kernel names no CPU */
#define AMIX_CPU_BASELINE 30

/* 20, 30, 40 or 60: AMIX_CPU in the environment (tests), else the "680x0-"
 * tag of uname's machine string, else AMIX_CPU_BASELINE.  Cached. */
int amix_cpu(void);
/* the rule itself, for a machine string: 20, 30, 40, 60, or 0 for no tag */
int amix_cpu_from_machine(const char *machine);

/* move16 and the 040/060-only FPU forms (fsadd, fdsqrt ...) */
#define AMIX_CPU_IS_040_060(c) ((c) >= 40)

#ifdef __cplusplus
}
#endif

#endif /* AMIX_CPU_H */
