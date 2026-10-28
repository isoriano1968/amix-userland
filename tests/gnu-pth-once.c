/* Regression: reset once state if an initializing thread exits. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
    } } while (0)
static pthread_once_t retry = PTHREAD_ONCE_INIT;
static pthread_once_t outer = PTHREAD_ONCE_INIT, inner = PTHREAD_ONCE_INIT;
static int attempts, completed, nested;
static int exit_token;
static void initialize_retry(void)
{
    ++attempts;
    if (attempts == 1) pthread_exit(&exit_token);
    completed = 1;
}
static void initialize_inner(void) { ++nested; }
static void initialize_outer(void)
{ REQUIRE(pthread_once(&inner, initialize_inner) == 0); }
static void *worker(void *argument)
{
    REQUIRE(pthread_once(&retry, initialize_retry) == 0);
    REQUIRE(pthread_once(&outer, initialize_outer) == 0);
    return argument;
}
int main(void)
{
    pthread_t first, second;
    void *result;
    alarm(15);
    REQUIRE(pthread_create(&first, 0, worker, 0) == 0);
    REQUIRE(pthread_join(first, &result) == 0);
    REQUIRE(result == &exit_token);
    REQUIRE(pthread_create(&second, 0, worker, &completed) == 0);
    REQUIRE(pthread_join(second, &result) == 0);
    REQUIRE(result == &completed && completed == 1);
    REQUIRE(attempts == 2 && nested == 1);
    alarm(0);
    puts("PASS: GNU Pth once retries after initializer exit and allows nested once");
    return 0;
}
