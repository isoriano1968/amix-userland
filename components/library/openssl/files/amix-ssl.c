/* amix-ssl.c -- usleep for a shared library: AMIX's is in the static part of libc */
extern int poll();

int
usleep(unsigned int us)
{
	return poll(0, 0, (int)((us + 999) / 1000));
}
