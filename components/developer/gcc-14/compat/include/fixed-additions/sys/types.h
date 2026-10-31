/* Appended to GCC's fixed copy of AMIX's <sys/types.h>.
 *
 * Cannot be a #include_next wrapper like the others: fixincludes has already
 * placed its own edited copy of this header in include-fixed, that copy
 * shadows the sysroot, and replacing it would throw away whatever fixincludes
 * repaired.  So this text is appended to it instead.
 *
 * ssize_t reached POSIX after SVR4.0 shipped and AMIX has it nowhere, yet it
 * is the return type of read(), write() and readlink() -- so anything doing
 * I/O in portable C or C++ needs it.  libstdc++'s own std::filesystem is
 * where it first bit.
 *
 * int is not a guess: this is an ILP32 target, so ssize_t and int are the same
 * width, and AMIX's own read() and write() are declared returning int.
 */
#ifndef _AMIX_HAVE_SSIZE_T
#define _AMIX_HAVE_SSIZE_T
typedef int ssize_t;
#endif
