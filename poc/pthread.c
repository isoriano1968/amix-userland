/* Cooperative proof only: explicit yields and synchronization are switch points.
 * No timer preemption, blocking syscall interception, or production ABI.
 */
#include "pthread.h"
#include <ucontext.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define LIMIT 12
#define KEYS 8
#define STACK_SIZE 65536
enum { UNUSED, READY, RUNNING, BLOCKED, ZOMBIE };
enum { WAIT_NONE, WAIT_MUTEX, WAIT_COND, WAIT_JOIN };
struct thread {
    ucontext_t context;
    int state, saved_errno, reason;
    pthread_t id, joiner;
    void *waiting, *stack, *result, *argument;
    void *specific[KEYS];
    void *(*start)(void *);
    struct amix_cleanup *cleanup;
};
static struct thread threads[LIMIT];
static int initialized, current, key_count;
static pthread_t next_id = 1;
static void (*destructors[KEYS])(void *);

static void initialize(void)
{
    if (!initialized) {
        threads[0].state = RUNNING;
        threads[0].id = next_id++;
        initialized = 1;
    }
}

static int schedule(void)
{
    int i, next = -1, old = current;
    int saved = errno;
    for (i = 1; i <= LIMIT; ++i) {
        int candidate = (old + i) % LIMIT;
        if (threads[candidate].state == READY) {
            next = candidate;
            break;
        }
    }
    if (next == -1)
        return EDEADLK;
    if (next == old) {
        threads[old].state = RUNNING;
        return 0;
    }
    threads[old].saved_errno = saved;
    current = next;
    threads[next].state = RUNNING;
    errno = threads[next].saved_errno;
    if (swapcontext(&threads[old].context, &threads[next].context) == -1)
        _exit(90);
    errno = threads[old].saved_errno;
    return 0;
}

static int park(int reason, void *object)
{
    int status;
    struct thread *t = &threads[current];
    t->reason = reason;
    t->waiting = object;
    t->state = BLOCKED;
    status = schedule();
    if (status) t->state = RUNNING;
    t->reason = WAIT_NONE;
    t->waiting = 0;
    return status;
}

static void wake(int reason, void *object, int all)
{
    int i;
    for (i = 0; i < LIMIT; ++i) {
        if (threads[i].state == BLOCKED && threads[i].reason == reason &&
            threads[i].waiting == object) {
            threads[i].state = READY;
            if (!all) return;
        }
    }
}

pthread_t pthread_self(void) { initialize(); return threads[current].id; }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int amix_thread_yield(void)
{
    initialize();
    threads[current].state = READY;
    return schedule();
}

static void trampoline(void)
{
    struct thread *t = &threads[current];
    pthread_exit(t->start(t->argument));
}

int pthread_attr_init(pthread_attr_t *a)
{
    if (!a) return EINVAL;
    a->stacksize = STACK_SIZE;
    a->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *a) { return a ? 0 : EINVAL; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int state)
{
    if (!a || state != PTHREAD_CREATE_JOINABLE) return EINVAL;
    a->detachstate = state;
    return 0;
}

int pthread_create(pthread_t *id, const pthread_attr_t *a,
                   void *(*start)(void *), void *arg)
{
    int i, old_errno = errno;
    struct thread *t;
    initialize();
    if (!id || !start || (a && (a->detachstate != 0 ||
        a->stacksize != STACK_SIZE))) return EINVAL;
    for (i = 1; i < LIMIT && threads[i].state != UNUSED; ++i) {}
    if (i == LIMIT) return EAGAIN;
    t = &threads[i];
    t->stack = malloc(STACK_SIZE);
    if (!t->stack) { errno = old_errno; return EAGAIN; }
    if (getcontext(&t->context) == -1) {
        free(t->stack); t->stack = 0; errno = old_errno; return EAGAIN;
    }
    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = STACK_SIZE;
    t->context.uc_stack.ss_flags = 0;
    t->context.uc_link = 0;
    t->start = start;
    t->argument = arg;
    t->saved_errno = 0;
    t->id = next_id++;
    t->state = READY;
    makecontext(&t->context, trampoline, 0);
    *id = t->id;
    errno = old_errno;
    return 0;
}

int pthread_join(pthread_t id, void **result)
{
    int i, status;
    struct thread *t;
    initialize();
    if (id == pthread_self()) return EDEADLK;
    for (i = 1; i < LIMIT; ++i)
        if (threads[i].state != UNUSED && threads[i].id == id) break;
    if (i == LIMIT) return ESRCH;
    t = &threads[i];
    if (t->joiner) return EINVAL;
    t->joiner = pthread_self();
    while (t->state != ZOMBIE) {
        status = park(WAIT_JOIN, t);
        if (status) { t->joiner = 0; return status; }
    }
    if (result) *result = t->result;
    free(t->stack);
    /* Zero explicitly, avoiding a dependency on a modern libc API. */
    {
        unsigned int n;
        unsigned char *p = (unsigned char *)t;
        for (n = 0; n < sizeof(*t); ++n) p[n] = 0;
    }
    return 0;
}

void pthread_exit(void *result)
{
    int pass, key;
    struct thread *t;
    initialize();
    t = &threads[current];
    while (t->cleanup) amix_cleanup_pop(t->cleanup, 1);
    for (pass = 0; pass < 4; ++pass) {
        for (key = 0; key < key_count; ++key) {
            void *value = t->specific[key];
            if (value && destructors[key]) {
                t->specific[key] = 0;
                destructors[key](value);
            }
        }
    }
    t->result = result;
    t->state = ZOMBIE;
    wake(WAIT_JOIN, t, 1);
    if (schedule()) {
        fputs("pthreads-amix proof: no runnable context at exit\n", stderr);
        _exit(91);
    }
    _exit(92);
}

int pthread_mutex_init(pthread_mutex_t *m, const void *a)
{ if (!m || a) return EINVAL; m->owner = 0; return 0; }
int pthread_mutex_destroy(pthread_mutex_t *m)
{
    int i;
    if (!m) return EINVAL;
    if (m->owner) return EBUSY;
    for (i = 0; i < LIMIT; ++i)
        if (threads[i].reason == WAIT_MUTEX && threads[i].waiting == m)
            return EBUSY;
    return 0;
}
int pthread_mutex_lock(pthread_mutex_t *m)
{
    int status;
    if (!m) return EINVAL;
    initialize();
    if (m->owner == pthread_self()) return EDEADLK;
    while (m->owner) {
        status = park(WAIT_MUTEX, m);
        if (status) return status;
    }
    m->owner = pthread_self();
    return 0;
}
int pthread_mutex_unlock(pthread_mutex_t *m)
{
    if (!m || m->owner != pthread_self()) return EPERM;
    m->owner = 0;
    wake(WAIT_MUTEX, m, 0);
    return 0;
}
int pthread_cond_init(pthread_cond_t *c, const void *a)
{ if (!c || a) return EINVAL; c->initialized = 1; return 0; }
int pthread_cond_destroy(pthread_cond_t *c)
{
    int i;
    if (!c || !c->initialized) return EINVAL;
    for (i = 0; i < LIMIT; ++i)
        if (threads[i].reason == WAIT_COND && threads[i].waiting == c)
            return EBUSY;
    c->initialized = 0;
    return 0;
}
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    int status, relock;
    if (!c || !c->initialized) return EINVAL;
    status = pthread_mutex_unlock(m);
    if (status) return status;
    /* No scheduling occurs between unlock and park in this cooperative model. */
    status = park(WAIT_COND, c);
    relock = pthread_mutex_lock(m);
    return status ? status : relock;
}
int pthread_cond_signal(pthread_cond_t *c)
{ if (!c || !c->initialized) return EINVAL; wake(WAIT_COND, c, 0); return 0; }
int pthread_cond_broadcast(pthread_cond_t *c)
{ if (!c || !c->initialized) return EINVAL; wake(WAIT_COND, c, 1); return 0; }
int pthread_once(pthread_once_t *once, void (*function)(void))
{
    int status;
    if (!once || !function) return EINVAL;
    initialize();
    while (once->state == 1) {
        status = park(WAIT_COND, once);
        if (status) return status;
    }
    if (once->state == 0) {
        once->state = 1;
        function();
        once->state = 2;
        wake(WAIT_COND, once, 1);
    }
    return 0;
}
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
    initialize();
    if (!key) return EINVAL;
    if (key_count == KEYS) return EAGAIN;
    *key = key_count;
    destructors[key_count++] = destructor;
    return 0;
}
int pthread_setspecific(pthread_key_t key, const void *value)
{
    initialize();
    if (key >= (unsigned int)key_count) return EINVAL;
    threads[current].specific[key] = (void *)value;
    return 0;
}
void *pthread_getspecific(pthread_key_t key)
{
    initialize();
    return key < (unsigned int)key_count ? threads[current].specific[key] : 0;
}
void amix_cleanup_push(struct amix_cleanup *c, void (*fn)(void *), void *arg)
{
    initialize();
    c->routine = fn; c->argument = arg;
    c->previous = threads[current].cleanup;
    threads[current].cleanup = c;
}
void amix_cleanup_pop(struct amix_cleanup *c, int execute)
{
    threads[current].cleanup = c->previous;
    if (execute) c->routine(c->argument);
}
