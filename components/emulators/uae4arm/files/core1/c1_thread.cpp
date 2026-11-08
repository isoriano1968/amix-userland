/*
 * c1_thread.cpp -- uae's threads on core 1: libco coroutines, scheduled by
 * hand.  Core 1 is one CPU with no operating system; the emulator's helper
 * threads (the renderer, the hard file device) only ever wait on
 * semaphores, so a semaphore that has nothing to give lets the other
 * threads run until it has:
 *
 *  - a worker (made by SDL_CreateThread) that waits switches back to
 *    whoever started it,
 *  - the emulator (any coroutine that is not a worker) that waits runs the
 *    workers until the count is up,
 *  - a post from the emulator to a semaphore a worker waits on runs that
 *    worker at once, so a hard file request is done before the emulator
 *    goes on (no frame of delay per block),
 *  - c1_run_workers() runs them all once (the frontend, every frame).
 *
 * The SDL names are the ones libretro/include/SDL_thread.h declares (C++).
 */
#include <stdlib.h>
#include <string.h>
#include "SDL_thread.h"
#include "libco.h"
#include "c1uae.h"

#define C1_MAX_THREADS  8
#define C1_STACK        (256 * 1024)

struct c1t {
    cothread_t co, caller;
    int (*fn)(void *);
    void *arg;
    int done;
    SDL_sem *waiting;       /* what it waits on, or NULL */
};

static struct c1t th[C1_MAX_THREADS];
static int nth;
static struct c1t *cur;     /* the worker running now; NULL outside the workers */

static void entry(void)
{
    struct c1t *t = cur;

    t->fn(t->arg);
    t->done = 1;
    for (;;)
        co_switch(t->caller);
}

static void run_one(struct c1t *t)
{
    struct c1t *prev = cur;

    if (t->done || t == cur)
        return;
    t->caller = co_active();
    cur = t;
    co_switch(t->co);
    cur = prev;
}

extern "C" void c1_run_workers(void)
{
    int i;

    for (i = 0; i < nth; i++)
        run_one(&th[i]);
}

/* nothing to take: let the others run */
static void wait_for(SDL_sem *s)
{
    while (s->sem.count <= 0) {
        if (cur) {
            struct c1t *t = cur;

            t->waiting = s;
            co_switch(t->caller);
            t->waiting = NULL;
        } else {
            int i, ran = 0;

            for (i = 0; i < nth; i++)
                if (!th[i].done) {
                    run_one(&th[i]);
                    ran = 1;
                }
            if (s->sem.count <= 0 && !ran)
                c1p_idle();         /* nobody can post: only time (or the 68k) helps */
        }
    }
    s->sem.count--;
}

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_sem *s = (SDL_sem *)calloc(1, sizeof(SDL_sem));

    if (s)
        s->sem.count = (int)initial_value;
    return s;
}

void SDL_DestroySemaphore(SDL_sem *s)
{
    free(s);
}

int SDL_SemTryWait(SDL_sem *s)
{
    if (s->sem.count <= 0)
        return SDL_MUTEX_TIMEDOUT;
    s->sem.count--;
    return 0;
}

int SDL_SemWait(SDL_sem *s)
{
    wait_for(s);
    return 0;
}

int SDL_SemWaitTimeout(SDL_sem *s, Uint32 timeout)
{
    (void)timeout;
    return SDL_SemTryWait(s);
}

Uint32 SDL_SemValue(SDL_sem *s)
{
    return s->sem.count > 0 ? (Uint32)s->sem.count : 0;
}

int SDL_SemPost(SDL_sem *s)
{
    int i;

    s->sem.count++;
    if (!cur)                   /* from the emulator: a worker waiting on it goes now */
        for (i = 0; i < nth; i++)
            if (th[i].waiting == s && !th[i].done) {
                run_one(&th[i]);
                break;
            }
    return 0;
}

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data)
{
    struct c1t *t;
    SDL_Thread *id;

    if (nth == C1_MAX_THREADS)
        return NULL;
    id = (SDL_Thread *)calloc(1, sizeof(SDL_Thread));
    if (!id)
        return NULL;
    t = &th[nth];
    memset(t, 0, sizeof(*t));
    t->fn = fn;
    t->arg = data;
    t->co = co_create(C1_STACK, entry);
    if (!t->co) {
        free(id);
        return NULL;
    }
    id->threadid = (SDL_threadID)(nth + 1);
    nth++;
    run_one(t);                 /* up to its first wait, as a real thread would get going */
    return id;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    int i = (int)thread->threadid - 1;

    if (i >= 0 && i < nth)
        while (!th[i].done)
            c1_run_workers();
    if (status)
        *status = 0;
}

/* compat/semaphore.h: the same counting, for code that uses sem_t itself */
extern "C" {
int sem_init(sem_t *s, int shared, unsigned v) { (void)shared; s->count = (int)v; return 0; }
int sem_destroy(sem_t *s) { (void)s; return 0; }
int sem_trywait(sem_t *s) { if (s->count <= 0) return -1; s->count--; return 0; }
int sem_post(sem_t *s) { s->count++; return 0; }
int sem_getvalue(sem_t *s, int *v) { *v = s->count; return 0; }
int sem_wait(sem_t *s)
{
    while (s->count <= 0)
        cur ? (void)co_switch(cur->caller) : c1_run_workers();
    s->count--;
    return 0;
}
}
