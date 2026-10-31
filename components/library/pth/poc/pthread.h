/* Experimental cooperative API. Use only via -I./poc in isolated programs. */
#ifndef AMIX_POC_PTHREAD_H
#define AMIX_POC_PTHREAD_H
#include <stddef.h>
typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef struct { size_t stacksize; int detachstate; } pthread_attr_t;
typedef struct { pthread_t owner; } pthread_mutex_t;
typedef struct { int initialized; } pthread_cond_t;
typedef struct { int state; } pthread_once_t;
struct amix_cleanup {
    void (*routine)(void *);
    void *argument;
    struct amix_cleanup *previous;
};
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 1 }
#define PTHREAD_ONCE_INIT { 0 }
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
void pthread_exit(void *);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
int pthread_mutex_init(pthread_mutex_t *, const void *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_cond_init(pthread_cond_t *, const void *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_once(pthread_once_t *, void (*)(void));
int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_setspecific(pthread_key_t, const void *);
void *pthread_getspecific(pthread_key_t);
int amix_thread_yield(void);
void amix_cleanup_push(struct amix_cleanup *, void (*)(void *), void *);
void amix_cleanup_pop(struct amix_cleanup *, int);
#define pthread_cleanup_push(fn, arg) do { \
    struct amix_cleanup amix_cleanup_record; \
    amix_cleanup_push(&amix_cleanup_record, (fn), (arg));
#define pthread_cleanup_pop(execute) \
    amix_cleanup_pop(&amix_cleanup_record, (execute)); } while (0)
#endif
