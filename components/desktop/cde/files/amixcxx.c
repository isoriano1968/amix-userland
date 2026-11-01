/*
 * libamixcxx.a: the C++ runtime that libtt and libDtSvc ask of a program, for
 * a program in C, here or with AMIX's gcc 2.7, where gcc 14's libstdc++ cannot
 * be linked.  They use new and delete and carry RTTI tables, and neither throw
 * nor catch.  A program with real libstdc++ needs no part of this.
 */
#include <stdlib.h>

void *_Znwm(unsigned long n) { return malloc(n ? n : 1); }
void *_Znam(unsigned long n) { return malloc(n ? n : 1); }
void _ZdlPv(void *p) { free(p); }
void _ZdaPv(void *p) { free(p); }
void _ZdlPvm(void *p, unsigned long n) { free(p); }
void _ZdaPvm(void *p, unsigned long n) { free(p); }
void __cxa_throw_bad_array_new_length(void) { abort(); }
void __cxa_pure_virtual(void) { abort(); }
void __gxx_personality_v0(void) { abort(); }

/* only their addresses are read, into type_info objects nothing looks at */
void *_ZTVN10__cxxabiv117__class_type_infoE[4];
void *_ZTVN10__cxxabiv120__si_class_type_infoE[4];
void *_ZTVN10__cxxabiv121__vmi_class_type_infoE[4];
