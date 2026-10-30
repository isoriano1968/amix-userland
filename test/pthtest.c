/* pthtest -- do gnu-pth's timed sleeps wake while another thread computes?
   main: usleep(10 ms) loop, counting wake-ups; worker: ~40 ms of work, then a
   10 ms timed wait on a condition, again and again.  3 seconds. */
#include <stdio.h>
#include <sys/time.h>
#include <pthread.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c = PTHREAD_COND_INITIALIZER;
static volatile int stop;
static volatile long chunks, waits;

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static void *worker(void *a)
{
	volatile double x = 0;
	long i;

	(void)a;
	while (!stop) {
		double t0 = now();
		struct timespec ts;
		struct timeval tv;

		while (now() - t0 < 0.04)
			for (i = 0; i < 1000; i++)
				x += i * 0.5;
		chunks++;
		gettimeofday(&tv, NULL);
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = (tv.tv_usec + 10000) * 1000L;
		if (ts.tv_nsec >= 1000000000L) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000L;
		}
		pthread_mutex_lock(&m);
		pthread_cond_timedwait(&c, &m, &ts);
		pthread_mutex_unlock(&m);
		waits++;
	}
	return NULL;
}

int main(void)
{
	pthread_t t;
	double t0 = now(), next = t0 + 0.5;
	long wakes = 0;

	pthread_create(&t, NULL, worker, NULL);
	while (now() - t0 < 3.0) {
		usleep(10000);
		wakes++;
		if (now() >= next) {
			printf("%.1f s: main woke %ld times, worker did %ld chunks, %ld timed waits\n",
			    now() - t0, wakes, chunks, waits);
			fflush(stdout);
			next += 0.5;
		}
	}
	stop = 1;
	pthread_join(t, NULL);
	printf("done: main woke %ld times (ideal ~%d with the worker taking 40 of every 50 ms)\n", wakes, 60);
	return 0;
}
