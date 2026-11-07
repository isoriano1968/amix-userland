/*
 * <fenv.h> for C99 on AMIX: the 68881/68040/68060 FPU's status (FPSR) and
 * control (FPCR) registers, with the functions in libm99.so.1 (musl's m68k
 * fenv).  AMIX has only <ieeefp.h> (fpgetround, fpsetsticky ...); both work
 * on the same registers and can be mixed.  The exception bits are the FPSR's
 * accrued byte, as ieeefp.h's FP_X_*; the rounding modes the FPCR's RND
 * field.  Neither touches FPCR's precision field: AMIX starts a process with
 * FPCR 0 (extended precision, to nearest, no traps).  fesetround does not
 * update AMIX's FLT_ROUNDS variable (__flt_rounds), which fpsetround does.
 * Installed as /usr/local/include/amix-c99/fenv.h.
 */
#ifndef _AMIX_C99_FENV_H
#define _AMIX_C99_FENV_H

#ifdef __cplusplus
extern "C" {
#endif

#define FE_INEXACT    8
#define FE_DIVBYZERO  16
#define FE_UNDERFLOW  32
#define FE_OVERFLOW   64
#define FE_INVALID    128
#define FE_ALL_EXCEPT 0xf8

#define FE_TONEAREST  0
#define FE_TOWARDZERO 16
#define FE_DOWNWARD   32
#define FE_UPWARD     48

typedef unsigned fexcept_t;

typedef struct {
	unsigned __control_register, __status_register, __instruction_address;
} fenv_t;

#define FE_DFL_ENV ((const fenv_t *) -1)

int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int);
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int);
int fetestexcept(int);
int fegetround(void);
int fesetround(int);
int fegetenv(fenv_t *);
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *);
int feupdateenv(const fenv_t *);

#ifdef __cplusplus
}
#endif

#endif /* _AMIX_C99_FENV_H */
