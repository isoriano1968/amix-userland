#!/bin/sh
# Read-only, bounded inventory. Run with /bin/sh; redirect output on the host.
# Deliberately avoid recursive filesystem scans and dumping the environment.
PATH=/usr/local/bin:/usr/bin:/usr/ccs/bin:/sbin:/usr/sbin:/etc
export PATH
LC_ALL=C
export LC_ALL

echo '=== Running system (authoritative baseline) ==='
uname -a
who -b
echo '=== Boot files (not proof of the loaded image) ==='
for item in /unix /stand/unix /stand/unix.z3; do
    if test -r "$item"; then ls -l "$item"; fi
done
echo '=== Native compiler resolution ==='
type cc gcc gcc14 2>&1
if test -x /usr/local/bin/gcc; then /usr/local/bin/gcc --version; fi
echo '=== Candidate headers ==='
for item in /usr/include/pthread.h /usr/include/thread.h \
    /usr/include/ucontext.h /usr/include/sys/ucontext.h \
    /usr/include/sys/lwp.h /usr/include/semaphore.h; do
    if test -r "$item"; then ls -l "$item"; else echo "absent: $item"; fi
done
echo '=== Candidate libraries (standard locations only) ==='
for item in /usr/lib/lib*thread* /usr/lib/lib*lwp* \
    /usr/local/lib/lib*thread* /usr/lib/libc.so.1; do
    if test -r "$item"; then ls -l "$item"; fi
done
echo '=== Relevant libc symbols (presence does not prove working semantics) ==='
if test -x /usr/ccs/bin/nm && test -r /usr/lib/libc.so.1; then
    /usr/ccs/bin/nm /usr/lib/libc.so.1 |
    egrep 'context|errno|pthread|lwp|sigprocmask|setitimer|clock_gettime|gettimeofday|sigaction|sigaltstack|sigsuspend|nanosleep|poll'
fi
echo '=== Static libc context symbols ==='
if test -x /usr/ccs/bin/nm && test -r /usr/ccs/lib/libc.a; then
    /usr/ccs/bin/nm /usr/ccs/lib/libc.a | egrep 'makecontext|swapcontext'
fi
echo '=== End inventory ==='
