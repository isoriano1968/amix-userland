/* sys/statvfs.h -- add the fsblkcnt_t / fsfilcnt_t typedefs, and make the
 * header self-contained.
 *
 * Two repairs.  AMIX has statvfs and its struct, but spells the counters
 * u_long directly -- POSIX only named those types later -- and libstdc++'s
 * std::filesystem::space uses fsblkcnt_t.  unsigned long is not a guess: it is
 * exactly what f_blocks, f_bfree, f_bavail and f_frsize are declared as in
 * AMIX's own header.
 *
 * And the vendor header uses u_long without including <sys/types.h>, so it
 * only compiles if the translation unit happened to include that first.  Doing
 * it here costs nothing and removes an ordering trap.
 */
#ifndef AMIX_FIXED_SYS_STATVFS_H
#define AMIX_FIXED_SYS_STATVFS_H

#include <sys/types.h>
#include_next <sys/statvfs.h>

#ifndef _AMIX_HAVE_FSBLKCNT_T
#define _AMIX_HAVE_FSBLKCNT_T
typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;
#endif

#endif /* AMIX_FIXED_SYS_STATVFS_H */
