/* semaphore.h for core 1: counting semaphores over c1_thread.c's
   cooperative threads (libco coroutines; a wait with nothing to take lets
   the others run) */
#ifndef C1_SEMAPHORE_H
#define C1_SEMAPHORE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { volatile int count; } sem_t;
int sem_init(sem_t *, int, unsigned);
int sem_destroy(sem_t *);
int sem_wait(sem_t *);
int sem_trywait(sem_t *);
int sem_post(sem_t *);
int sem_getvalue(sem_t *, int *);
#ifdef __cplusplus
}
#endif
#endif
