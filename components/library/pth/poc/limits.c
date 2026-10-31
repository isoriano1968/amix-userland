/* Error-path and finite-resource tests for the proof runtime. */
#include "pthread.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
    } } while (0)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static pthread_key_t recycled;
static int waiting, destructor_calls, token;

static void reinsert(void *value)
{
    REQUIRE(value == &token);
    REQUIRE(pthread_getspecific(recycled) == 0);
    ++destructor_calls;
    REQUIRE(pthread_setspecific(recycled, value) == 0);
}
static void *empty(void *argument) { return argument; }
static void *waiter(void *argument)
{
    REQUIRE(pthread_mutex_lock(&lock) == 0);
    waiting = 1;
    REQUIRE(pthread_cond_wait(&condition, &lock) == 0);
    REQUIRE(pthread_mutex_unlock(&lock) == 0);
    REQUIRE(pthread_setspecific(recycled, &token) == 0);
    return argument;
}
int main(void)
{
    pthread_t pool[12], worker;
    pthread_key_t keys[8], extra;
    pthread_attr_t attributes;
    void *result;
    int i;
    alarm(30);
    REQUIRE(pthread_join(pthread_self(), 0) == EDEADLK);
    REQUIRE(pthread_join(999999UL, 0) == ESRCH);
    REQUIRE(pthread_mutex_unlock(&lock) == EPERM);
    REQUIRE(pthread_mutex_lock(&lock) == 0);
    REQUIRE(pthread_mutex_lock(&lock) == EDEADLK);
    REQUIRE(pthread_mutex_destroy(&lock) == EBUSY);
    /* No ready peer: wait reports deadlock and reacquires its mutex. */
    REQUIRE(pthread_cond_wait(&condition, &lock) == EDEADLK);
    REQUIRE(pthread_mutex_unlock(&lock) == 0);
    REQUIRE(pthread_attr_init(&attributes) == 0);
    REQUIRE(pthread_attr_setdetachstate(&attributes,
            PTHREAD_CREATE_DETACHED) == EINVAL);
    for (i = 0; i < 11; ++i)
        REQUIRE(pthread_create(&pool[i], 0, empty, &token) == 0);
    REQUIRE(pthread_create(&pool[11], 0, empty, 0) == EAGAIN);
    for (i = 0; i < 11; ++i) {
        REQUIRE(pthread_join(pool[i], &result) == 0);
        REQUIRE(result == &token);
    }
    REQUIRE(pthread_key_create(&keys[0], reinsert) == 0);
    recycled = keys[0];
    for (i = 1; i < 8; ++i)
        REQUIRE(pthread_key_create(&keys[i], 0) == 0);
    REQUIRE(pthread_key_create(&extra, 0) == EAGAIN);
    REQUIRE(pthread_setspecific(99, &token) == EINVAL);
    REQUIRE(pthread_create(&worker, 0, waiter, &token) == 0);
    REQUIRE(amix_thread_yield() == 0);
    REQUIRE(waiting == 1);
    REQUIRE(pthread_cond_destroy(&condition) == EBUSY);
    REQUIRE(pthread_cond_signal(&condition) == 0);
    REQUIRE(pthread_cond_destroy(&condition) == EBUSY);
    REQUIRE(pthread_join(worker, &result) == 0);
    REQUIRE(result == &token && destructor_calls == 4);
    REQUIRE(pthread_cond_destroy(&condition) == 0);
    REQUIRE(pthread_mutex_destroy(&lock) == 0);
    alarm(0);
    puts("PASS: resource limits, deadlock errors, busy objects, destructor passes");
    return 0;
}
