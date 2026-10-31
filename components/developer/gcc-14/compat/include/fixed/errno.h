/* errno.h -- add ENOTSUP, which POSIX added after AMIX shipped.
 *
 * Reaches AMIX's own <errno.h> with #include_next and adds only what is
 * missing.  See the note in fixed/math.h for why include-fixed is the right
 * place for this.
 *
 * ENOTSUP is aliased to EOPNOTSUPP (122), which is what POSIX explicitly
 * permits and what most systems do -- "operation not supported".  The other
 * candidate here was ENOSYS (89, "unsupported file system operation"), but
 * that one already has a narrower meaning in this errno list and reusing it
 * would make two distinct failures indistinguishable.
 *
 * Needed because libstdc++'s std::filesystem sets errno = ENOTSUP on paths it
 * cannot implement.
 */
#ifndef AMIX_FIXED_ERRNO_H
#define AMIX_FIXED_ERRNO_H

#include_next <errno.h>

#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#endif /* AMIX_FIXED_ERRNO_H */
