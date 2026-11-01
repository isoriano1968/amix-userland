
/* AMIX's gcc 2.7 takes no attribute before a declaration's type */
#if defined(__GNUC__) && __GNUC__ < 3
#  define PNG_NORETURN
#  define PNG_USE_RESULT
#  define PNG_DEPRECATED
#  define PNG_ALLOCATED
#endif
