/* rand48.c -- for libXdmcp's keys: AMIX's libc has no rand48 */
static unsigned long long state;

void srand48(long seed)
{
	state = ((unsigned long long)(unsigned long)seed << 16) | 0x330E;
}

long lrand48(void)
{
	state = (state * 0x5DEECE66DULL + 0xB) & 0xFFFFFFFFFFFFULL;
	return (long)(state >> 17);
}
