/* amix-fsv.h -- forced into every file: what AMIX's headers hide from an
 * ANSI compiler (math.h gives these only to K&R or X/Open builds) */
extern double hypot(double, double);
extern double cbrt(double);
/* sys/stat.h: the same */
#define S_ISLNK(m)	(((m) & 0170000) == 0120000)
#define S_ISSOCK(m)	(((m) & 0170000) == 0140000)
