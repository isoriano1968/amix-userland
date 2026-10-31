/* usleep for the shared library: AMIX's comes from the static part of libc,
 * which is not PIC.  poll has milliseconds, which is what the drivers need. */
extern int poll();

int usleep(unsigned long usec)
{
	return poll((void *)0, 0UL, (int)((usec + 999) / 1000));
}
