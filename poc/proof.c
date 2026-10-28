/* Bounded producer/consumer proof against the experimental API. */
#include "pthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define COUNT 300
#define CAPACITY 7
#define CHECK(call) do { int code = (call); if (code) { \
    fprintf(stderr, "FAIL line %d: %s -> %d\n", __LINE__, #call, code); \
    exit(1); } } while (0)
#define REQUIRE(test) do { if (!(test)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(2); \
    } } while (0)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t readable = PTHREAD_COND_INITIALIZER;
static pthread_cond_t writable = PTHREAD_COND_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t identity;
static int *queue[CAPACITY];
static int head, tail, used, finished, produced, consumed;
static int once_calls, cleanup_calls, destructor_calls;
static unsigned long sum;
static int identifiers[4] = { 0, 1, 2, 3 };

static void initialize_once(void)
{
    ++once_calls;
    CHECK(amix_thread_yield());
}
static void cleanup(void *argument)
{
    REQUIRE(argument != 0);
    ++cleanup_calls;
}
static void destroy_identity(void *argument)
{
    REQUIRE(argument == pthread_getspecific(identity) ||
            pthread_getspecific(identity) == 0);
    ++destructor_calls;
}
static void checkpoint(int id)
{
    int value = 200 + id;
    REQUIRE(pthread_getspecific(identity) == &identifiers[id]);
    errno = value;
    CHECK(amix_thread_yield());
    REQUIRE(errno == value);
    REQUIRE(pthread_getspecific(identity) == &identifiers[id]);
}
static void *worker(void *argument)
{
    int id = *(int *)argument;
    int i;
    CHECK(pthread_setspecific(identity, argument));
    CHECK(pthread_once(&once, initialize_once));
    pthread_cleanup_push(cleanup, argument);
    if (id < 2) {
        for (i = 0; i < COUNT; ++i) {
            int *item = (int *)malloc(sizeof(int));
            REQUIRE(item != 0);
            *item = id * COUNT + i + 1;
            CHECK(pthread_mutex_lock(&lock));
            while (used == CAPACITY)
                CHECK(pthread_cond_wait(&writable, &lock));
            queue[tail] = item;
            tail = (tail + 1) % CAPACITY;
            ++used;
            ++produced;
            /* Force peers to encounter an owned mutex. */
            if (i % 7 == 0) CHECK(amix_thread_yield());
            CHECK(pthread_cond_signal(&readable));
            CHECK(pthread_mutex_unlock(&lock));
            checkpoint(id);
        }
        CHECK(pthread_mutex_lock(&lock));
        ++finished;
        CHECK(pthread_cond_broadcast(&readable));
        CHECK(pthread_mutex_unlock(&lock));
    } else {
        for (;;) {
            int *item;
            CHECK(pthread_mutex_lock(&lock));
            while (used == 0 && finished != 2)
                CHECK(pthread_cond_wait(&readable, &lock));
            if (used == 0 && finished == 2) {
                CHECK(pthread_mutex_unlock(&lock));
                break;
            }
            item = queue[head];
            head = (head + 1) % CAPACITY;
            --used;
            ++consumed;
            sum += *item;
            CHECK(pthread_cond_signal(&writable));
            CHECK(pthread_mutex_unlock(&lock));
            free(item);
            checkpoint(id);
        }
    }
    pthread_cleanup_pop(1);
    return argument;
}

static void *exit_with_cleanup(void *argument)
{
    pthread_cleanup_push(cleanup, argument);
    pthread_exit(argument);
    pthread_cleanup_pop(0);
    return 0;
}

int main(void)
{
    pthread_t workers[4], again;
    pthread_attr_t attributes;
    void *result;
    int i;
    unsigned long expected = (2UL * COUNT) * (2UL * COUNT + 1) / 2;
    alarm(60);
    CHECK(pthread_key_create(&identity, destroy_identity));
    CHECK(pthread_attr_init(&attributes));
    CHECK(pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE));
    for (i = 0; i < 4; ++i)
        CHECK(pthread_create(&workers[i], &attributes, worker, &identifiers[i]));
    CHECK(pthread_attr_destroy(&attributes));
    REQUIRE(!pthread_equal(workers[0], workers[1]));
    for (i = 0; i < 4; ++i) {
        CHECK(pthread_join(workers[i], &result));
        REQUIRE(result == &identifiers[i]);
    }
    REQUIRE(produced == 2 * COUNT && consumed == produced && used == 0);
    REQUIRE(sum == expected);
    REQUIRE(once_calls == 1 && cleanup_calls == 4 && destructor_calls == 4);
    /* Reclaim/reuse every worker slot; check cleanup during explicit exit. */
    for (i = 0; i < 24; ++i) {
        CHECK(pthread_create(&again, 0, exit_with_cleanup, &identifiers[0]));
        CHECK(pthread_join(again, &result));
        REQUIRE(result == &identifiers[0]);
    }
    REQUIRE(cleanup_calls == 28);
    REQUIRE(pthread_join(workers[0], 0) == ESRCH);
    CHECK(pthread_cond_destroy(&readable));
    CHECK(pthread_cond_destroy(&writable));
    CHECK(pthread_mutex_destroy(&lock));
    alarm(0);
    printf("produced=%d consumed=%d checksum=%lu expected=%lu\n",
           produced, consumed, sum, expected);
    printf("once=%d cleanup=%d destructors=%d reused-slots=24\n",
           once_calls, cleanup_calls, destructor_calls);
    puts("PASS: cooperative pthread API proof on AMIX");
    puts("create/join, mutex contention, condition queues, once, keys, errno");
    puts("NOT full POSIX: no preemption, blocking-I/O isolation, timed waits");
    return 0;
}
