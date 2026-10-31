/* GNU Pth pthread emulation: wrapped blocking read and timed condition wait. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s errno=%d\n", __LINE__, #x, errno); \
    exit(1); } } while (0)
static int pipefd[2];
static volatile int reader_entered, reader_done, heartbeat;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

static void *reader(void *argument)
{
    char value;
    (void)argument;
    reader_entered = 1;
    REQUIRE(read(pipefd[0], &value, 1) == 1);
    REQUIRE(value == 'P');
    reader_done = 1;
    return 0;
}
static void *writer(void *argument)
{
    int i;
    (void)argument;
    while (!reader_entered) REQUIRE(pthread_yield_np() == 0);
    for (i = 0; i < 100; ++i) {
        REQUIRE(reader_done == 0);
        ++heartbeat;
        REQUIRE(pthread_yield_np() == 0);
    }
    REQUIRE(write(pipefd[1], "P", 1) == 1);
    return 0;
}
int main(void)
{
    pthread_t read_thread, write_thread;
    struct timespec deadline;
    int status;
    alarm(15);
    REQUIRE(pipe(pipefd) == 0);
    REQUIRE(pthread_create(&read_thread, 0, reader, 0) == 0);
    REQUIRE(pthread_create(&write_thread, 0, writer, 0) == 0);
    REQUIRE(pthread_join(read_thread, 0) == 0);
    REQUIRE(pthread_join(write_thread, 0) == 0);
    REQUIRE(reader_done == 1 && heartbeat == 100);
    close(pipefd[0]); close(pipefd[1]);
    REQUIRE(pthread_mutex_lock(&lock) == 0);
    deadline.tv_sec = time(0) + 1;
    deadline.tv_nsec = 0;
    status = pthread_cond_timedwait(&condition, &lock, &deadline);
    REQUIRE(status == ETIMEDOUT);
    REQUIRE(pthread_mutex_unlock(&lock) == 0);
    REQUIRE(pthread_cond_destroy(&condition) == 0);
    REQUIRE(pthread_mutex_destroy(&lock) == 0);
    alarm(0);
    puts("PASS: GNU Pth wrapped read blocked while peer made progress");
    puts("PASS: GNU Pth pthread_cond_timedwait returned ETIMEDOUT");
    puts("Scope: calls compiled with GNU Pth header mappings; not arbitrary libc I/O");
    return 0;
}
